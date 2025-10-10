#include "connection.h"

#include <engine/server/databases/connection_pool.h>

#if defined(CONF_MYSQL)
#include <base/tl/threading.h>

#include <engine/console.h>

#include <mysql.h>

#include <atomic>
#include <memory>
#include <vector>

// MySQL >= 8.0.1 removed my_bool, 8.0.2 accidentally reintroduced it: https://bugs.mysql.com/bug.php?id=87337
#if !defined(LIBMARIADB) && MYSQL_VERSION_ID >= 80001 && MYSQL_VERSION_ID != 80002
typedef bool my_bool;
#endif

enum
{
	MYSQLSTATE_UNINITIALIZED,
	MYSQLSTATE_INITIALIZED,
	MYSQLSTATE_SHUTTINGDOWN,
};

std::atomic_int g_MysqlState = {MYSQLSTATE_UNINITIALIZED};
std::atomic_int g_MysqlNumConnections;

bool MysqlAvailable()
{
	return true;
}

int MysqlInit()
{
	dbg_assert(mysql_thread_safe(), "MySQL library without thread safety");
	dbg_assert(g_MysqlState == MYSQLSTATE_UNINITIALIZED, "double MySQL initialization");
	if(mysql_library_init(0, nullptr, nullptr))
	{
		return 1;
	}
	int Uninitialized = MYSQLSTATE_UNINITIALIZED;
	bool Swapped = g_MysqlState.compare_exchange_strong(Uninitialized, MYSQLSTATE_INITIALIZED);
	(void)Swapped;
	dbg_assert(Swapped, "MySQL double initialization");
	return 0;
}

void MysqlUninit()
{
	int Initialized = MYSQLSTATE_INITIALIZED;
	bool Swapped = g_MysqlState.compare_exchange_strong(Initialized, MYSQLSTATE_SHUTTINGDOWN);
	(void)Swapped;
	dbg_assert(Swapped, "double MySQL free or free without initialization");
	int Counter = g_MysqlNumConnections;
	if(Counter != 0)
	{
		dbg_msg("mysql", "can't deinitialize, connections remaining: %d", Counter);
		return;
	}
	mysql_library_end();
}

class CMysqlConnection : public IDbConnection
{
public:
	explicit CMysqlConnection(CMysqlConfig m_Config);
	~CMysqlConnection();
	void Print(IConsole *pConsole, const char *pMode) override;

	const char *BinaryCollate() const override { return "utf8mb4_bin"; }
	void ToUnixTimestamp(const char *pTimestamp, char *aBuf, unsigned int BufferSize) override;
	const char *InsertTimestampAsUtc() const override { return "?"; }
	const char *CollateNocase() const override { return "CONVERT(? USING utf8mb4) COLLATE utf8mb4_general_ci"; }
	const char *InsertIgnore() const override { return "INSERT IGNORE"; }
	const char *Random() const override { return "RAND()"; }
	const char *MedianMapTime(char *pBuffer, int BufferSize) const override;
	const char *False() const override { return "FALSE"; }
	const char *True() const override { return "TRUE"; }

	bool Connect(char *pError, int ErrorSize) override;
	void Disconnect() override;

	bool PrepareStatement(const char *pStmt, char *pError, int ErrorSize) override;

	void BindString(int Idx, const char *pString) override;
	void BindBlob(int Idx, unsigned char *pBlob, int Size) override;
	void BindInt(int Idx, int Value) override;
	void BindInt64(int Idx, int64_t Value) override;
	void BindFloat(int Idx, float Value) override;
	void BindNull(int Idx) override;

	void Print() override {}
	bool Step(bool *pEnd, char *pError, int ErrorSize) override;
	bool ExecuteUpdate(int *pNumUpdated, char *pError, int ErrorSize) override;

	bool IsNull(int Col) override;
	float GetFloat(int Col) override;
	int GetInt(int Col) override;
	int64_t GetInt64(int Col) override;
	void GetString(int Col, char *pBuffer, int BufferSize) override;
	int GetBlob(int Col, unsigned char *pBuffer, int BufferSize) override;

	bool AddPoints(const char *pPlayer, int Points, char *pError, int ErrorSize) override;

private:
	class CStmtDeleter
	{
	public:
		void operator()(MYSQL_STMT *pStmt) const;
	};

	char m_aErrorDetail[128];
	void StoreErrorMysql(const char *pContext);
	void StoreErrorStmt(const char *pContext);
	bool ConnectImpl();
	bool PrepareAndExecuteStatement(const char *pStmt);
	//static void DeleteResult(MYSQL_RES *pResult);

	union UParameterExtra
	{
		int i;
		int64_t i64;
		unsigned long ul;
		float f;
	};

	bool m_NewQuery = false;
	bool m_HaveConnection = false;
	MYSQL m_Mysql;
	std::unique_ptr<MYSQL_STMT, CStmtDeleter> m_pStmt = nullptr;
	std::vector<MYSQL_BIND> m_vStmtParameters;
	std::vector<UParameterExtra> m_vStmtParameterExtras;

	// copy of m_Config vars
	CMysqlConfig m_Config;

	std::atomic_bool m_InUse;

	// <FoxNet
	bool ApplyMigrations();
	// FoxNet>
};

void CMysqlConnection::CStmtDeleter::operator()(MYSQL_STMT *pStmt) const
{
	mysql_stmt_close(pStmt);
}

CMysqlConnection::CMysqlConnection(CMysqlConfig Config) :
	IDbConnection(Config.m_aPrefix),
	m_Config(Config),
	m_InUse(false)
{
	g_MysqlNumConnections += 1;
	dbg_assert(g_MysqlState == MYSQLSTATE_INITIALIZED, "MySQL library not in initialized state");

	m_aErrorDetail[0] = '\0';
	mem_zero(&m_Mysql, sizeof(m_Mysql));
	mysql_init(&m_Mysql);
}

CMysqlConnection::~CMysqlConnection()
{
	mysql_close(&m_Mysql);
	g_MysqlNumConnections -= 1;
}

void CMysqlConnection::StoreErrorMysql(const char *pContext)
{
	str_format(m_aErrorDetail, sizeof(m_aErrorDetail), "(%s:mysql:%d): %s", pContext, mysql_errno(&m_Mysql), mysql_error(&m_Mysql));
}

void CMysqlConnection::StoreErrorStmt(const char *pContext)
{
	str_format(m_aErrorDetail, sizeof(m_aErrorDetail), "(%s:stmt:%d): %s", pContext, mysql_stmt_errno(m_pStmt.get()), mysql_stmt_error(m_pStmt.get()));
}

bool CMysqlConnection::PrepareAndExecuteStatement(const char *pStmt)
{
	if(mysql_stmt_prepare(m_pStmt.get(), pStmt, str_length(pStmt)))
	{
		StoreErrorStmt("prepare");
		return false;
	}
	if(mysql_stmt_execute(m_pStmt.get()))
	{
		StoreErrorStmt("execute");
		return false;
	}
	return true;
}

void CMysqlConnection::Print(IConsole *pConsole, const char *pMode)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"MySQL-%s: DB: '%s' Prefix: '%s' User: '%s' IP: <{'%s'}> Port: %d",
		pMode, m_Config.m_aDatabase, GetPrefix(), m_Config.m_aUser, m_Config.m_aIp, m_Config.m_Port);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CMysqlConnection::ToUnixTimestamp(const char *pTimestamp, char *aBuf, unsigned int BufferSize)
{
	str_format(aBuf, BufferSize, "UNIX_TIMESTAMP(%s)", pTimestamp);
}

bool CMysqlConnection::Connect(char *pError, int ErrorSize)
{
	dbg_assert(!m_InUse.exchange(true), "Tried connecting while the connection is in use");

	m_NewQuery = true;
	if(!ConnectImpl())
	{
		str_copy(pError, m_aErrorDetail, ErrorSize);
		m_InUse.store(false);
		return false;
	}
	return true;
}

bool CMysqlConnection::ConnectImpl()
{
	if(m_HaveConnection)
	{
		if(m_pStmt && mysql_stmt_free_result(m_pStmt.get()))
		{
			StoreErrorStmt("free_result");
			dbg_msg("mysql", "can't free last result %s", m_aErrorDetail);
		}
		if(!mysql_select_db(&m_Mysql, m_Config.m_aDatabase))
		{
			// Success.
			return true;
		}
		StoreErrorMysql("select_db");
		dbg_msg("mysql", "ping error, trying to reconnect %s", m_aErrorDetail);
		mysql_close(&m_Mysql);
		mem_zero(&m_Mysql, sizeof(m_Mysql));
		mysql_init(&m_Mysql);
	}

	m_pStmt = nullptr;
	unsigned int OptConnectTimeout = 60;
	unsigned int OptReadTimeout = 60;
	unsigned int OptWriteTimeout = 120;
	my_bool OptReconnect = true;
	mysql_options(&m_Mysql, MYSQL_OPT_CONNECT_TIMEOUT, &OptConnectTimeout);
	mysql_options(&m_Mysql, MYSQL_OPT_READ_TIMEOUT, &OptReadTimeout);
	mysql_options(&m_Mysql, MYSQL_OPT_WRITE_TIMEOUT, &OptWriteTimeout);
	mysql_options(&m_Mysql, MYSQL_OPT_RECONNECT, &OptReconnect);
	mysql_options(&m_Mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");
	if(m_Config.m_aBindaddr[0] != '\0')
	{
		mysql_options(&m_Mysql, MYSQL_OPT_BIND, m_Config.m_aBindaddr);
	}

	if(!mysql_real_connect(&m_Mysql, m_Config.m_aIp, m_Config.m_aUser, m_Config.m_aPass, nullptr, m_Config.m_Port, nullptr, CLIENT_IGNORE_SIGPIPE))
	{
		StoreErrorMysql("real_connect");
		return false;
	}
	m_HaveConnection = true;

	m_pStmt = std::unique_ptr<MYSQL_STMT, CStmtDeleter>(mysql_stmt_init(&m_Mysql));

	// Apparently MYSQL_SET_CHARSET_NAME is not enough
	if(!PrepareAndExecuteStatement("SET CHARACTER SET utf8mb4"))
	{
		return false;
	}

	if(m_Config.m_Setup)
	{
		char aCreateDatabase[1024];
		// create database
		str_format(aCreateDatabase, sizeof(aCreateDatabase), "CREATE DATABASE IF NOT EXISTS %s CHARACTER SET utf8mb4", m_Config.m_aDatabase);
		if(!PrepareAndExecuteStatement(aCreateDatabase))
		{
			return false;
		}
	}

	// Connect to specific database
	if(mysql_select_db(&m_Mysql, m_Config.m_aDatabase))
	{
		StoreErrorMysql("select_db");
		return false;
	}

	if(m_Config.m_Setup)
	{
		char aCreateRace[1024];
		char aCreateTeamrace[1024];
		char aCreateMaps[1024];
		char aCreateSaves[1024];
		char aCreatePoints[1024];
		// <FoxNet
		char aCreateAccounts[1024];
		char aCreateInventory[1024];
		// FoxNet>
		FormatCreateRace(aCreateRace, sizeof(aCreateRace), /* Backup */ false);
		FormatCreateTeamrace(aCreateTeamrace, sizeof(aCreateTeamrace), "VARBINARY(16)", /* Backup */ false);
		FormatCreateMaps(aCreateMaps, sizeof(aCreateMaps));
		FormatCreateSaves(aCreateSaves, sizeof(aCreateSaves), /* Backup */ false);
		FormatCreatePoints(aCreatePoints, sizeof(aCreatePoints));
		// <FoxNet
		FormatCreateAccounts(aCreateAccounts, sizeof(aCreateAccounts));
		FormatCreateAccountInventory(aCreateInventory, sizeof(aCreateInventory));
		// FoxNet>

		if(!PrepareAndExecuteStatement(aCreateRace) ||
			!PrepareAndExecuteStatement(aCreateTeamrace) ||
			!PrepareAndExecuteStatement(aCreateMaps) ||
			!PrepareAndExecuteStatement(aCreateSaves) ||
			!PrepareAndExecuteStatement(aCreatePoints) ||
			// <FoxNet
			!PrepareAndExecuteStatement(aCreateAccounts) ||
			!PrepareAndExecuteStatement(aCreateInventory)
			// FoxNet>
			) 
		{
			return false;
		}
		m_Config.m_Setup = false;
	}
	// <FoxNet
	ApplyMigrations();
	// FoxNet>

	dbg_msg("mysql", "connection established");
	return true;
}

void CMysqlConnection::Disconnect()
{
	m_InUse.store(false);
}

bool CMysqlConnection::PrepareStatement(const char *pStmt, char *pError, int ErrorSize)
{
	if(mysql_stmt_prepare(m_pStmt.get(), pStmt, str_length(pStmt)))
	{
		StoreErrorStmt("prepare");
		str_copy(pError, m_aErrorDetail, ErrorSize);
		return false;
	}
	m_NewQuery = true;
	unsigned NumParameters = mysql_stmt_param_count(m_pStmt.get());
	m_vStmtParameters.resize(NumParameters);
	m_vStmtParameterExtras.resize(NumParameters);
	if(NumParameters)
	{
		mem_zero(m_vStmtParameters.data(), sizeof(m_vStmtParameters[0]) * m_vStmtParameters.size());
		mem_zero(m_vStmtParameterExtras.data(), sizeof(m_vStmtParameterExtras[0]) * m_vStmtParameterExtras.size());
	}
	return true;
}

void CMysqlConnection::BindString(int Idx, const char *pString)
{
	m_NewQuery = true;
	Idx -= 1;
	dbg_assert(0 <= Idx && Idx < (int)m_vStmtParameters.size(), "Error in BindString: index out of bounds: %d", Idx);

	int Length = str_length(pString);
	m_vStmtParameterExtras[Idx].ul = Length;
	MYSQL_BIND *pParam = &m_vStmtParameters[Idx];
	pParam->buffer_type = MYSQL_TYPE_STRING;
	pParam->buffer = (void *)pString;
	pParam->buffer_length = Length + 1;
	pParam->length = &m_vStmtParameterExtras[Idx].ul;
	pParam->is_null = nullptr;
	pParam->is_unsigned = false;
	pParam->error = nullptr;
}

void CMysqlConnection::BindBlob(int Idx, unsigned char *pBlob, int Size)
{
	m_NewQuery = true;
	Idx -= 1;
	dbg_assert(0 <= Idx && Idx < (int)m_vStmtParameters.size(), "Error in BindBlob: index out of bounds: %d", Idx);

	m_vStmtParameterExtras[Idx].ul = Size;
	MYSQL_BIND *pParam = &m_vStmtParameters[Idx];
	pParam->buffer_type = MYSQL_TYPE_BLOB;
	pParam->buffer = pBlob;
	pParam->buffer_length = Size;
	pParam->length = &m_vStmtParameterExtras[Idx].ul;
	pParam->is_null = nullptr;
	pParam->is_unsigned = false;
	pParam->error = nullptr;
}

void CMysqlConnection::BindInt(int Idx, int Value)
{
	m_NewQuery = true;
	Idx -= 1;
	dbg_assert(0 <= Idx && Idx < (int)m_vStmtParameters.size(), "Error in BindInt: index out of bounds: %d", Idx);

	m_vStmtParameterExtras[Idx].i = Value;
	MYSQL_BIND *pParam = &m_vStmtParameters[Idx];
	pParam->buffer_type = MYSQL_TYPE_LONG;
	pParam->buffer = &m_vStmtParameterExtras[Idx].i;
	pParam->buffer_length = sizeof(m_vStmtParameterExtras[Idx].i);
	pParam->length = nullptr;
	pParam->is_null = nullptr;
	pParam->is_unsigned = false;
	pParam->error = nullptr;
}

void CMysqlConnection::BindInt64(int Idx, int64_t Value)
{
	m_NewQuery = true;
	Idx -= 1;
	dbg_assert(0 <= Idx && Idx < (int)m_vStmtParameters.size(), "Error in BindInt64: index out of bounds: %d", Idx);

	m_vStmtParameterExtras[Idx].i64 = Value;
	MYSQL_BIND *pParam = &m_vStmtParameters[Idx];
	pParam->buffer_type = MYSQL_TYPE_LONGLONG;
	pParam->buffer = &m_vStmtParameterExtras[Idx].i64;
	pParam->buffer_length = sizeof(m_vStmtParameterExtras[Idx].i64);
	pParam->length = nullptr;
	pParam->is_null = nullptr;
	pParam->is_unsigned = false;
	pParam->error = nullptr;
}

void CMysqlConnection::BindFloat(int Idx, float Value)
{
	m_NewQuery = true;
	Idx -= 1;
	dbg_assert(0 <= Idx && Idx < (int)m_vStmtParameters.size(), "Error in BindFloat: index out of bounds: %d", Idx);

	m_vStmtParameterExtras[Idx].f = Value;
	MYSQL_BIND *pParam = &m_vStmtParameters[Idx];
	pParam->buffer_type = MYSQL_TYPE_FLOAT;
	pParam->buffer = &m_vStmtParameterExtras[Idx].f;
	pParam->buffer_length = sizeof(m_vStmtParameterExtras[Idx].i);
	pParam->length = nullptr;
	pParam->is_null = nullptr;
	pParam->is_unsigned = false;
	pParam->error = nullptr;
}

void CMysqlConnection::BindNull(int Idx)
{
	m_NewQuery = true;
	Idx -= 1;
	dbg_assert(0 <= Idx && Idx < (int)m_vStmtParameters.size(), "Error in BindNull: index out of bounds: %d", Idx);

	MYSQL_BIND *pParam = &m_vStmtParameters[Idx];
	pParam->buffer_type = MYSQL_TYPE_NULL;
	pParam->buffer = nullptr;
	pParam->buffer_length = 0;
	pParam->length = nullptr;
	pParam->is_null = nullptr;
	pParam->is_unsigned = false;
	pParam->error = nullptr;
}

bool CMysqlConnection::Step(bool *pEnd, char *pError, int ErrorSize)
{
	if(m_NewQuery)
	{
		m_NewQuery = false;
		if(mysql_stmt_bind_param(m_pStmt.get(), m_vStmtParameters.data()))
		{
			StoreErrorStmt("bind_param");
			str_copy(pError, m_aErrorDetail, ErrorSize);
			return false;
		}
		if(mysql_stmt_execute(m_pStmt.get()))
		{
			StoreErrorStmt("execute");
			str_copy(pError, m_aErrorDetail, ErrorSize);
			return false;
		}
	}
	int Result = mysql_stmt_fetch(m_pStmt.get());
	if(Result == 1)
	{
		StoreErrorStmt("fetch");
		str_copy(pError, m_aErrorDetail, ErrorSize);
		return false;
	}
	*pEnd = (Result == MYSQL_NO_DATA);
	// `Result` is now either `MYSQL_DATA_TRUNCATED` (which we ignore, we
	// fetch our columns in a different way) or `0` aka success.
	return true;
}

bool CMysqlConnection::ExecuteUpdate(int *pNumUpdated, char *pError, int ErrorSize)
{
	if(m_NewQuery)
	{
		m_NewQuery = false;
		if(mysql_stmt_bind_param(m_pStmt.get(), m_vStmtParameters.data()))
		{
			StoreErrorStmt("bind_param");
			str_copy(pError, m_aErrorDetail, ErrorSize);
			return false;
		}
		if(mysql_stmt_execute(m_pStmt.get()))
		{
			StoreErrorStmt("execute");
			str_copy(pError, m_aErrorDetail, ErrorSize);
			return false;
		}
		*pNumUpdated = mysql_stmt_affected_rows(m_pStmt.get());
		return true;
	}
	str_copy(pError, "tried to execute update without query", ErrorSize);
	return false;
}

bool CMysqlConnection::IsNull(int Col)
{
	Col -= 1;

	MYSQL_BIND Bind;
	my_bool IsNull;
	mem_zero(&Bind, sizeof(Bind));
	Bind.buffer_type = MYSQL_TYPE_NULL;
	Bind.buffer = nullptr;
	Bind.buffer_length = 0;
	Bind.length = nullptr;
	Bind.is_null = &IsNull;
	Bind.is_unsigned = false;
	Bind.error = nullptr;
	if(mysql_stmt_fetch_column(m_pStmt.get(), &Bind, Col, 0))
	{
		StoreErrorStmt("fetch_column:null");
		dbg_assert(false, "Error in IsNull: error fetching column %s", m_aErrorDetail);
	}
	return IsNull;
}

float CMysqlConnection::GetFloat(int Col)
{
	Col -= 1;

	MYSQL_BIND Bind;
	float Value;
	my_bool IsNull;
	mem_zero(&Bind, sizeof(Bind));
	Bind.buffer_type = MYSQL_TYPE_FLOAT;
	Bind.buffer = &Value;
	Bind.buffer_length = sizeof(Value);
	Bind.length = nullptr;
	Bind.is_null = &IsNull;
	Bind.is_unsigned = false;
	Bind.error = nullptr;
	if(mysql_stmt_fetch_column(m_pStmt.get(), &Bind, Col, 0))
	{
		StoreErrorStmt("fetch_column:float");
		dbg_assert(false, "Error in GetFloat: error fetching column %s", m_aErrorDetail);
	}
	dbg_assert(!IsNull, "Error in GetFloat: NULL");
	return Value;
}

int CMysqlConnection::GetInt(int Col)
{
	Col -= 1;

	MYSQL_BIND Bind;
	int Value;
	my_bool IsNull;
	mem_zero(&Bind, sizeof(Bind));
	Bind.buffer_type = MYSQL_TYPE_LONG;
	Bind.buffer = &Value;
	Bind.buffer_length = sizeof(Value);
	Bind.length = nullptr;
	Bind.is_null = &IsNull;
	Bind.is_unsigned = false;
	Bind.error = nullptr;
	if(mysql_stmt_fetch_column(m_pStmt.get(), &Bind, Col, 0))
	{
		StoreErrorStmt("fetch_column:int");
		dbg_assert(false, "Error in GetInt: error fetching column %s", m_aErrorDetail);
	}
	dbg_assert(!IsNull, "Error in GetInt: NULL");
	return Value;
}

int64_t CMysqlConnection::GetInt64(int Col)
{
	Col -= 1;

	MYSQL_BIND Bind;
	int64_t Value;
	my_bool IsNull;
	mem_zero(&Bind, sizeof(Bind));
	Bind.buffer_type = MYSQL_TYPE_LONGLONG;
	Bind.buffer = &Value;
	Bind.buffer_length = sizeof(Value);
	Bind.length = nullptr;
	Bind.is_null = &IsNull;
	Bind.is_unsigned = false;
	Bind.error = nullptr;
	if(mysql_stmt_fetch_column(m_pStmt.get(), &Bind, Col, 0))
	{
		StoreErrorStmt("fetch_column:int64");
		dbg_assert(false, "Error in GetInt64: error fetching column %s", m_aErrorDetail);
	}
	dbg_assert(!IsNull, "Error in GetInt64: NULL");
	return Value;
}

void CMysqlConnection::GetString(int Col, char *pBuffer, int BufferSize)
{
	Col -= 1;

	for(int i = 0; i < BufferSize; i++)
	{
		pBuffer[i] = '\0';
	}

	MYSQL_BIND Bind;
	unsigned long Length;
	my_bool IsNull;
	my_bool Error;
	mem_zero(&Bind, sizeof(Bind));
	Bind.buffer_type = MYSQL_TYPE_STRING;
	Bind.buffer = pBuffer;
	// leave one character for null-termination
	Bind.buffer_length = BufferSize - 1;
	Bind.length = &Length;
	Bind.is_null = &IsNull;
	Bind.is_unsigned = false;
	Bind.error = &Error;
	if(mysql_stmt_fetch_column(m_pStmt.get(), &Bind, Col, 0))
	{
		StoreErrorStmt("fetch_column:string");
		dbg_assert(false, "Error in GetString: error fetching column %s", m_aErrorDetail);
	}
	dbg_assert(!IsNull, "Error in GetString: NULL");
	dbg_assert(!Error, "Error in GetString: truncation occurred");
}

int CMysqlConnection::GetBlob(int Col, unsigned char *pBuffer, int BufferSize)
{
	Col -= 1;

	MYSQL_BIND Bind;
	unsigned long Length;
	my_bool IsNull;
	my_bool Error;
	mem_zero(&Bind, sizeof(Bind));
	Bind.buffer_type = MYSQL_TYPE_BLOB;
	Bind.buffer = pBuffer;
	Bind.buffer_length = BufferSize;
	Bind.length = &Length;
	Bind.is_null = &IsNull;
	Bind.is_unsigned = false;
	Bind.error = &Error;
	if(mysql_stmt_fetch_column(m_pStmt.get(), &Bind, Col, 0))
	{
		StoreErrorStmt("fetch_column:blob");
		dbg_assert(false, "Error in GetBlob: error fetching column %s", m_aErrorDetail);
	}
	dbg_assert(!IsNull, "Error in GetBlob: NULL");
	dbg_assert(!Error, "Error in GetBlob: truncation occurred");
	return Length;
}

const char *CMysqlConnection::MedianMapTime(char *pBuffer, int BufferSize) const
{
	str_format(pBuffer, BufferSize,
		"SELECT MEDIAN(Time) "
		"OVER (PARTITION BY Map) "
		"FROM %s_race "
		"WHERE Map = l.Map "
		"LIMIT 1",
		GetPrefix());
	return pBuffer;
}

bool CMysqlConnection::AddPoints(const char *pPlayer, int Points, char *pError, int ErrorSize)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"INSERT INTO %s_points(Name, Points) "
		"VALUES (?, ?) "
		"ON DUPLICATE KEY UPDATE Points=Points+?",
		GetPrefix());
	if(!PrepareStatement(aBuf, pError, ErrorSize))
	{
		return false;
	}
	BindString(1, pPlayer);
	BindInt(2, Points);
	BindInt(3, Points);
	int NumUpdated;
	return ExecuteUpdate(&NumUpdated, pError, ErrorSize);
}

std::unique_ptr<IDbConnection> CreateMysqlConnection(CMysqlConfig Config)
{
	return std::make_unique<CMysqlConnection>(Config);
}
// <FoxNet
bool CSqliteConnection::ApplyMigrations()
{
	char aErr[256];

	auto AddColumnIfMissing = [&](const char *pTable, const char *pStmtIfExists, const char *pStmt) -> bool {
		if(Execute(pStmtIfExists, aErr, sizeof(aErr)))
			return true;
		if(Execute(pStmt, aErr, sizeof(aErr)))
			return true;
		if(str_find_nocase(aErr, "duplicate column") || str_find_nocase(aErr, "already exists"))
			return true;
		dbg_msg("sqlite", "migration add column failed on %s: %s", pTable, aErr);
		return false;
	};

	if(!AddColumnIfMissing("foxnet_accounts",
		   "ALTER TABLE foxnet_accounts ADD COLUMN IF NOT EXISTS Disabled INTEGER NOT NULL DEFAULT 0",
		   "ALTER TABLE foxnet_accounts ADD COLUMN Disabled INTEGER NOT NULL DEFAULT 0"))
		return false;

	Execute("UPDATE foxnet_accounts SET Version = 2 WHERE Version < 2", aErr, sizeof(aErr));

	if(!AddColumnIfMissing("foxnet_account_inventory",
		   "ALTER TABLE foxnet_account_inventory ADD COLUMN IF NOT EXISTS Value INTEGER NOT NULL DEFAULT 0",
		   "ALTER TABLE foxnet_account_inventory ADD COLUMN Value INTEGER NOT NULL DEFAULT 0"))
		return false;

	bool EquippedExists = false;
	{
		if(!PrepareStatement("SELECT 1 FROM sqlite_master WHERE type='table' AND name='foxnet_account_equipped' LIMIT 1", aErr, sizeof(aErr)))
			return false;
		bool End = true;
		if(!Step(&End, aErr, sizeof(aErr)))
			return false;
		EquippedExists = !End;
	}
	if(EquippedExists)
	{
		Execute(
			"UPDATE foxnet_account_inventory AS i "
			"SET Value = COALESCE((SELECT e.Value "
			"                      FROM foxnet_account_equipped e "
			"                      WHERE e.Username=i.Username AND e.CosmeticId=i.CosmeticId), i.Value)",
			aErr, sizeof(aErr));
		Execute("DROP TABLE IF EXISTS foxnet_account_equipped", aErr, sizeof(aErr));
	}

	auto ShortcutToName = [](const char *pShortcut) -> const char * {
		struct Pair
		{
			const char *Short;
			const char *Name;
		};
		static const Pair s_Map[] = {
			{"R_F", "Rainbow Feet"},
			{"R_B", "Rainbow Body"},
			{"R_H", "Rainbow Hook"},
			{"G_E", "Emoticon Gun"},
			{"G_C", "Confetti Gun"},
			{"G_P", "Phase Gun"},
			{"I_C", "Clockwise Indicator"},
			{"I_CC", "Counter Clockwise Indicator"},
			{"I_IT", "Inward Turning Indicator"},
			{"I_OT", "Outward Turning Indicator"},
			{"I_L", "Line Indicator"},
			{"I_CrCs", "Criss Cross Indicator"},
			{"D_E", "Explosive Death"},
			{"D_HH", "Hammer Hit Death"},
			{"D_I", "Indicator Death"},
			{"D_L", "Laser Death"},
			{"T_S", "Star Trail"},
			{"T_D", "Dot Trail"},
			{"O_S", "Sparkle"},
			{"O_HH", "Heart Hat"},
			{"O_IA", "Inverse Aim"},
			{"O_L", "Lovely"},
			{"O_RB", "Rotating Ball"},
			{"O_EC", "Epic Circle"},
		};
		for(const auto &p : s_Map)
			if(str_comp(pShortcut, p.Short) == 0)
				return p.Name;
		return pShortcut;
	};

	struct Row
	{
		char User[33]{};
		char Inv[1028]{};
		char Active[1028]{};
		long long Acq{};
	};
	std::vector<Row> vRows;

	{
		const char *pSel =
			"SELECT Username, IFNULL(Inventory,''), IFNULL(LastActiveItems,''), "
			"       COALESCE(NULLIF(LastLogin,0), RegisterDate, strftime('%s','now')) "
			"FROM foxnet_accounts WHERE Version < 3";
		if(!PrepareStatement(pSel, aErr, sizeof(aErr)))
			return false;
		bool End = true;
		if(!Step(&End, aErr, sizeof(aErr)))
			return false;
		while(!End)
		{
			Row r{};
			GetString(1, r.User, sizeof(r.User));
			GetString(2, r.Inv, sizeof(r.Inv));
			GetString(3, r.Active, sizeof(r.Active));
			r.Acq = GetInt64(4);
			if(r.User[0])
				vRows.push_back(r);
			if(!Step(&End, aErr, sizeof(aErr)))
				return false;
		}
	}

	auto Trim = [](char *s) {
		int n = (int)str_length(s), i = 0, j = n - 1;
		while(i <= j && (s[i] == ' ' || s[i] == '\t'))
			i++;
		while(j >= i && (s[j] == ' ' || s[j] == '\t'))
			j--;
		int k = 0;
		for(int p = i; p <= j; ++p)
			s[k++] = s[p];
		s[k] = '\0';
	};
	auto ForEachSpaceToken = [&](char *buf, auto f) {
		for(char *p = buf;;)
		{
			while(*p == ' ')
				++p;
			if(*p == '\0')
				break;
			char *q = p;
			while(*q && *q != ' ')
				++q;
			char saved = *q;
			*q = '\0';
			Trim(p);
			if(p[0])
				f(p);
			if(saved == '\0')
				break;
			*q = saved;
			p = q;
		}
	};

	auto InsertInventory = [&](const Row &r, const char *pFull) -> bool {
		const char *pIns =
			"INSERT OR IGNORE INTO foxnet_account_inventory"
			"(Username, CosmeticId, Quantity, AcquiredAt, ExpiresAt, Meta) "
			"VALUES (?, ?, 1, ?, 0, '')";
		if(!PrepareStatement(pIns, aErr, sizeof(aErr)))
			return false;
		BindString(1, r.User);
		BindString(2, pFull);
		BindInt64(3, r.Acq);
		int Num = 0;
		return ExecuteUpdate(&Num, aErr, sizeof(aErr));
	};
	auto UpsertValue = [&](const Row &r, const char *pFull, int Val) -> bool {
		const char *pIns =
			"INSERT INTO foxnet_account_inventory"
			"(Username, CosmeticId, Quantity, AcquiredAt, ExpiresAt, Meta, Value) "
			"VALUES (?, ?, 1, ?, 0, '', ?) "
			"ON CONFLICT(Username, CosmeticId) DO UPDATE SET Value=excluded.Value";
		if(!PrepareStatement(pIns, aErr, sizeof(aErr)))
			return false;
		BindString(1, r.User);
		BindString(2, pFull);
		BindInt64(3, r.Acq);
		BindInt(4, Val);
		int Num = 0;
		return ExecuteUpdate(&Num, aErr, sizeof(aErr));
	};
	auto BumpToV3 = [&](const Row &r) -> bool {
		const char *pUpd = "UPDATE foxnet_accounts SET Version = 3 WHERE Username = ?";
		if(!PrepareStatement(pUpd, aErr, sizeof(aErr)))
			return false;
		BindString(1, r.User);
		int Num = 0;
		return ExecuteUpdate(&Num, aErr, sizeof(aErr));
	};

	for(const auto &r : vRows)
	{
		if(r.Inv[0])
		{
			char buf[1028];
			str_copy(buf, r.Inv, sizeof(buf));
			ForEachSpaceToken(buf, [&](char *tok) {
				InsertInventory(r, ShortcutToName(tok));
			});
		}
		if(r.Active[0])
		{
			char buf[1028];
			str_copy(buf, r.Active, sizeof(buf));
			ForEachSpaceToken(buf, [&](char *tok) {
				int Val = 1;
				if(char *eq = (char *)str_find(tok, "="))
				{
					*eq = '\0';
					Val = maximum(1, atoi(eq + 1));
				}
				UpsertValue(r, ShortcutToName(tok), Val);
			});
		}
		BumpToV3(r);
	}

	auto ColumnExists = [&](const char *pCol) -> bool {
		if(!PrepareStatement("PRAGMA table_info(foxnet_accounts)", aErr, sizeof(aErr)))
			return false;
		bool End = true;
		if(!Step(&End, aErr, sizeof(aErr)))
			return false;
		while(!End)
		{
			char aName[128]{};
			GetString(2, aName, sizeof(aName));
			if(str_comp(aName, pCol) == 0)
				return true;
			if(!Step(&End, aErr, sizeof(aErr)))
				return false;
		}
		return false;
	};

	if(ColumnExists("Inventory") || ColumnExists("LastActiveItems"))
	{
		bool Dropped = Execute("ALTER TABLE foxnet_accounts DROP COLUMN Inventory", aErr, sizeof(aErr)) &&
			       Execute("ALTER TABLE foxnet_accounts DROP COLUMN LastActiveItems", aErr, sizeof(aErr));

		if(!Dropped)
		{
			if(!Execute("BEGIN TRANSACTION", aErr, sizeof(aErr)))
				return false;
			char aCreate[1024];
			str_format(aCreate, sizeof(aCreate),
				"CREATE TABLE foxnet_accounts ("
				"  Version INTEGER NOT NULL DEFAULT 2, "
				"  Username VARCHAR(32) COLLATE %s NOT NULL, "
				"  Password VARCHAR(128) COLLATE %s NOT NULL, "
				"  RegisterDate INTEGER NOT NULL, "
				"  PlayerName VARCHAR(%d) COLLATE %s DEFAULT '', "
				"  LastPlayerName VARCHAR(%d) COLLATE %s DEFAULT '', "
				"  CurrentIP VARCHAR(45) COLLATE %s DEFAULT '', "
				"  LastIP VARCHAR(45) COLLATE %s DEFAULT '', "
				"  LoggedIn INTEGER DEFAULT 0, "
				"  LastLogin INTEGER DEFAULT 0, "
				"  Port INTEGER DEFAULT 0, "
				"  ClientId INTEGER DEFAULT -1, "
				"  Flags INTEGER DEFAULT -1, "
				"  VoteMenuPage INTEGER DEFAULT -1, "
				"  Playtime INTEGER DEFAULT 0, "
				"  Deaths INTEGER DEFAULT 0, "
				"  Kills INTEGER DEFAULT 0, "
				"  Level INTEGER DEFAULT 0, "
				"  XP INTEGER DEFAULT 0, "
				"  Money INTEGER DEFAULT 0, "
				"  Disabled INTEGER NOT NULL DEFAULT %s, "
				"  PRIMARY KEY (Username))",
				BinaryCollate(), BinaryCollate(),
				MAX_NAME_LENGTH_SQL, BinaryCollate(),
				MAX_NAME_LENGTH_SQL, BinaryCollate(),
				BinaryCollate(), BinaryCollate(), False());
			if(!Execute(aCreate, aErr, sizeof(aErr)))
			{
				Execute("ROLLBACK", aErr, sizeof(aErr));
				return false;
			}

			const char *pCopy =
				"INSERT INTO foxnet_accounts("
				"Version, Username, Password, RegisterDate, PlayerName, LastPlayerName, "
				"CurrentIP, LastIP, LoggedIn, LastLogin, Port, ClientId, Flags, VoteMenuPage, "
				"Playtime, Deaths, Kills, Level, XP, Money, Disabled)"
				" SELECT "
				"Version, Username, Password, RegisterDate, PlayerName, LastPlayerName, "
				"CurrentIP, LastIP, LoggedIn, LastLogin, Port, ClientId, Flags, VoteMenuPage, "
				"Playtime, Deaths, Kills, Level, XP, Money, Disabled"
				" FROM temp.foxnet_accounts_backup";

			if(!Execute("ALTER TABLE foxnet_accounts RENAME TO foxnet_accounts_backup", aErr, sizeof(aErr)))
			{
				Execute("ROLLBACK", aErr, sizeof(aErr));
				return false;
			}
			if(!Execute(pCopy, aErr, sizeof(aErr)))
			{
				Execute("ROLLBACK", aErr, sizeof(aErr));
				return false;
			}
			if(!Execute("DROP TABLE foxnet_accounts_backup", aErr, sizeof(aErr)))
			{
				Execute("ROLLBACK", aErr, sizeof(aErr));
				return false;
			}
			if(!Execute("COMMIT", aErr, sizeof(aErr)))
				return false;
		}
	}

	return true;
}
// FoxNet>
#else
bool MysqlAvailable()
{
	return false;
}
int MysqlInit()
{
	return 0;
}
void MysqlUninit()
{
}
std::unique_ptr<IDbConnection> CreateMysqlConnection(CMysqlConfig Config)
{
	return nullptr;
}
#endif
