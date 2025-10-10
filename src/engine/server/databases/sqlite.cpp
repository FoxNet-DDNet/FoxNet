#include "connection.h"

#include <base/math.h>

#include <engine/console.h>

#include <sqlite3.h>

#include <atomic>

class CSqliteConnection : public IDbConnection
{
public:
	CSqliteConnection(const char *pFilename, bool Setup);
	~CSqliteConnection() override;
	void Print(IConsole *pConsole, const char *pMode) override;

	const char *BinaryCollate() const override { return "BINARY"; }
	void ToUnixTimestamp(const char *pTimestamp, char *aBuf, unsigned int BufferSize) override;
	const char *InsertTimestampAsUtc() const override { return "DATETIME(?, 'utc')"; }
	const char *CollateNocase() const override { return "? COLLATE NOCASE"; }
	const char *InsertIgnore() const override { return "INSERT OR IGNORE"; }
	const char *Random() const override { return "RANDOM()"; }
	const char *MedianMapTime(char *pBuffer, int BufferSize) const override;
	// Since SQLite 3.23.0 true/false literals are recognized, but still cleaner to use 1/0, because:
	// > For compatibility, if there exist columns named "true" or "false", then
	// > the identifiers refer to the columns rather than Boolean constants.
	const char *False() const override { return "0"; }
	const char *True() const override { return "1"; }

	bool Connect(char *pError, int ErrorSize) override;
	void Disconnect() override;

	bool PrepareStatement(const char *pStmt, char *pError, int ErrorSize) override;

	void BindString(int Idx, const char *pString) override;
	void BindBlob(int Idx, unsigned char *pBlob, int Size) override;
	void BindInt(int Idx, int Value) override;
	void BindInt64(int Idx, int64_t Value) override;
	void BindFloat(int Idx, float Value) override;
	void BindNull(int Idx) override;

	void Print() override;
	bool Step(bool *pEnd, char *pError, int ErrorSize) override;
	bool ExecuteUpdate(int *pNumUpdated, char *pError, int ErrorSize) override;

	bool IsNull(int Col) override;
	float GetFloat(int Col) override;
	int GetInt(int Col) override;
	int64_t GetInt64(int Col) override;
	void GetString(int Col, char *pBuffer, int BufferSize) override;
	// passing a negative buffer size is undefined behavior
	int GetBlob(int Col, unsigned char *pBuffer, int BufferSize) override;

	bool AddPoints(const char *pPlayer, int Points, char *pError, int ErrorSize) override;

	// fail safe
	bool CreateFailsafeTables();

private:
	// copy of config vars
	char m_aFilename[IO_MAX_PATH_LENGTH];
	bool m_Setup;

	sqlite3 *m_pDb;
	sqlite3_stmt *m_pStmt;
	bool m_Done; // no more rows available for Step
	// returns false, if the query succeeded
	bool Execute(const char *pQuery, char *pError, int ErrorSize);
	// returns true on failure
	bool ConnectImpl(char *pError, int ErrorSize);

	// returns true if an error was formatted
	bool FormatError(int Result, char *pError, int ErrorSize);
	void AssertNoError(int Result);

	std::atomic_bool m_InUse;
	// <FoxNet
	bool ApplyMigrations();
	// FoxNet>
};

CSqliteConnection::CSqliteConnection(const char *pFilename, bool Setup) :
	IDbConnection("record"),
	m_Setup(Setup),
	m_pDb(nullptr),
	m_pStmt(nullptr),
	m_Done(true),
	m_InUse(false)
{
	str_copy(m_aFilename, pFilename);
}

CSqliteConnection::~CSqliteConnection()
{
	if(m_pStmt != nullptr)
		sqlite3_finalize(m_pStmt);
	sqlite3_close(m_pDb);
	m_pDb = nullptr;
}

void CSqliteConnection::Print(IConsole *pConsole, const char *pMode)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"SQLite-%s: DB: '%s'",
		pMode, m_aFilename);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CSqliteConnection::ToUnixTimestamp(const char *pTimestamp, char *aBuf, unsigned int BufferSize)
{
	str_format(aBuf, BufferSize, "strftime('%%s', %s)", pTimestamp);
}

bool CSqliteConnection::Connect(char *pError, int ErrorSize)
{
	if(m_InUse.exchange(true))
	{
		dbg_assert(false, "Tried connecting while the connection is in use");
	}
	if(!ConnectImpl(pError, ErrorSize))
	{
		m_InUse.store(false);
		return false;
	}
	return true;
}

bool CSqliteConnection::ConnectImpl(char *pError, int ErrorSize)
{
	if(m_pDb != nullptr)
	{
		return true;
	}

	if(sqlite3_libversion_number() < 3025000)
	{
		dbg_msg("sql", "SQLite version %s is not supported, use at least version 3.25.0", sqlite3_libversion());
	}

	int Result = sqlite3_open(m_aFilename, &m_pDb);
	if(Result != SQLITE_OK)
	{
		str_format(pError, ErrorSize, "Can't open sqlite database: '%s'", sqlite3_errmsg(m_pDb));
		return false;
	}

	// wait for database to unlock so we don't have to handle SQLITE_BUSY errors
	sqlite3_busy_timeout(m_pDb, -1);

	if(m_Setup)
	{
		if(!Execute("PRAGMA journal_mode=WAL", pError, ErrorSize))
			return false;
		char aBuf[1024];
		FormatCreateRace(aBuf, sizeof(aBuf), /* Backup */ false);
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		FormatCreateTeamrace(aBuf, sizeof(aBuf), "BLOB", /* Backup */ false);
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		FormatCreateMaps(aBuf, sizeof(aBuf));
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		FormatCreateSaves(aBuf, sizeof(aBuf), /* Backup */ false);
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		FormatCreatePoints(aBuf, sizeof(aBuf));
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		// <FoxNet
		FormatCreateAccounts(aBuf, sizeof(aBuf));
		if(!Execute(aBuf, pError, ErrorSize))
			return false;

		FormatCreateAccountInventory(aBuf, sizeof(aBuf));
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		// FoxNet>

		FormatCreateRace(aBuf, sizeof(aBuf), /* Backup */ true);
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		FormatCreateTeamrace(aBuf, sizeof(aBuf), "BLOB", /* Backup */ true);
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		FormatCreateSaves(aBuf, sizeof(aBuf), /* Backup */ true);
		if(!Execute(aBuf, pError, ErrorSize))
			return false;
		m_Setup = false;
	}
	// <FoxNet
	ApplyMigrations();
	// FoxNet>
	return true;
}

void CSqliteConnection::Disconnect()
{
	if(m_pStmt != nullptr)
		sqlite3_finalize(m_pStmt);
	m_pStmt = nullptr;
	m_InUse.store(false);
}

bool CSqliteConnection::PrepareStatement(const char *pStmt, char *pError, int ErrorSize)
{
	if(m_pStmt != nullptr)
		sqlite3_finalize(m_pStmt);
	m_pStmt = nullptr;
	int Result = sqlite3_prepare_v2(
		m_pDb,
		pStmt,
		-1, // pStmt can be any length
		&m_pStmt,
		nullptr);
	if(FormatError(Result, pError, ErrorSize))
	{
		return false;
	}
	m_Done = false;
	return true;
}

void CSqliteConnection::BindString(int Idx, const char *pString)
{
	int Result = sqlite3_bind_text(m_pStmt, Idx, pString, -1, nullptr);
	AssertNoError(Result);
	m_Done = false;
}

void CSqliteConnection::BindBlob(int Idx, unsigned char *pBlob, int Size)
{
	int Result = sqlite3_bind_blob(m_pStmt, Idx, pBlob, Size, nullptr);
	AssertNoError(Result);
	m_Done = false;
}

void CSqliteConnection::BindInt(int Idx, int Value)
{
	int Result = sqlite3_bind_int(m_pStmt, Idx, Value);
	AssertNoError(Result);
	m_Done = false;
}

void CSqliteConnection::BindInt64(int Idx, int64_t Value)
{
	int Result = sqlite3_bind_int64(m_pStmt, Idx, Value);
	AssertNoError(Result);
	m_Done = false;
}

void CSqliteConnection::BindFloat(int Idx, float Value)
{
	int Result = sqlite3_bind_double(m_pStmt, Idx, (double)Value);
	AssertNoError(Result);
	m_Done = false;
}

void CSqliteConnection::BindNull(int Idx)
{
	int Result = sqlite3_bind_null(m_pStmt, Idx);
	AssertNoError(Result);
	m_Done = false;
}

// Keep support for SQLite < 3.14 on older Linux distributions
// MinGW does not support weak attribute: https://sourceware.org/bugzilla/show_bug.cgi?id=9687
#if !defined(__MINGW32__)
[[gnu::weak]] extern char *sqlite3_expanded_sql(sqlite3_stmt *pStmt); // NOLINT(readability-redundant-declaration)
#endif

void CSqliteConnection::Print()
{
	if(m_pStmt != nullptr
#if !defined(__MINGW32__)
		&& sqlite3_expanded_sql != nullptr
#endif
	)
	{
		char *pExpandedStmt = sqlite3_expanded_sql(m_pStmt);
		dbg_msg("sql", "SQLite statement: %s", pExpandedStmt);
		sqlite3_free(pExpandedStmt);
	}
}

bool CSqliteConnection::Step(bool *pEnd, char *pError, int ErrorSize)
{
	if(m_Done)
	{
		*pEnd = true;
		return true;
	}
	int Result = sqlite3_step(m_pStmt);
	if(Result == SQLITE_ROW)
	{
		*pEnd = false;
		return true;
	}
	else if(Result == SQLITE_DONE)
	{
		m_Done = true;
		*pEnd = true;
		return true;
	}
	else
	{
		if(FormatError(Result, pError, ErrorSize))
		{
			return false;
		}
	}
	*pEnd = true;
	return true;
}

bool CSqliteConnection::ExecuteUpdate(int *pNumUpdated, char *pError, int ErrorSize)
{
	bool End;
	if(!Step(&End, pError, ErrorSize))
	{
		return false;
	}
	*pNumUpdated = sqlite3_changes(m_pDb);
	return true;
}

bool CSqliteConnection::IsNull(int Col)
{
	return sqlite3_column_type(m_pStmt, Col - 1) == SQLITE_NULL;
}

float CSqliteConnection::GetFloat(int Col)
{
	return (float)sqlite3_column_double(m_pStmt, Col - 1);
}

int CSqliteConnection::GetInt(int Col)
{
	return sqlite3_column_int(m_pStmt, Col - 1);
}

int64_t CSqliteConnection::GetInt64(int Col)
{
	return sqlite3_column_int64(m_pStmt, Col - 1);
}

void CSqliteConnection::GetString(int Col, char *pBuffer, int BufferSize)
{
	str_copy(pBuffer, (const char *)sqlite3_column_text(m_pStmt, Col - 1), BufferSize);
}

int CSqliteConnection::GetBlob(int Col, unsigned char *pBuffer, int BufferSize)
{
	int Size = sqlite3_column_bytes(m_pStmt, Col - 1);
	Size = minimum(Size, BufferSize);
	mem_copy(pBuffer, sqlite3_column_blob(m_pStmt, Col - 1), Size);
	return Size;
}

const char *CSqliteConnection::MedianMapTime(char *pBuffer, int BufferSize) const
{
	str_format(pBuffer, BufferSize,
		"SELECT AVG("
		"  CASE counter %% 2 "
		"    WHEN 0 THEN CASE WHEN rn IN (counter / 2, counter / 2 + 1) THEN Time END "
		"    WHEN 1 THEN CASE WHEN rn = counter / 2 + 1 THEN Time END END) "
		"  OVER (PARTITION BY Map) AS Median "
		"FROM ("
		"  SELECT *, ROW_NUMBER() "
		"  OVER (PARTITION BY Map ORDER BY Time) rn, COUNT(*) "
		"  OVER (PARTITION BY Map) counter "
		"  FROM %s_race where Map = l.Map) as r",
		GetPrefix());
	return pBuffer;
}

bool CSqliteConnection::Execute(const char *pQuery, char *pError, int ErrorSize)
{
	char *pErrorMsg;
	int Result = sqlite3_exec(m_pDb, pQuery, nullptr, nullptr, &pErrorMsg);
	if(Result != SQLITE_OK)
	{
		str_format(pError, ErrorSize, "error executing query: '%s'", pErrorMsg);
		sqlite3_free(pErrorMsg);
		return false;
	}
	return true;
}

bool CSqliteConnection::FormatError(int Result, char *pError, int ErrorSize)
{
	if(Result != SQLITE_OK)
	{
		str_copy(pError, sqlite3_errmsg(m_pDb), ErrorSize);
		return true;
	}
	return false;
}

void CSqliteConnection::AssertNoError(int Result)
{
	char aBuf[128];
	if(FormatError(Result, aBuf, sizeof(aBuf)))
	{
		dbg_msg("sqlite", "unexpected sqlite error: %s", aBuf);
		dbg_assert(0, "sqlite error");
	}
}

bool CSqliteConnection::AddPoints(const char *pPlayer, int Points, char *pError, int ErrorSize)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"INSERT INTO %s_points(Name, Points) "
		"VALUES (?, ?) "
		"ON CONFLICT(Name) DO UPDATE SET Points=Points+?",
		GetPrefix());
	if(!PrepareStatement(aBuf, pError, ErrorSize))
	{
		return false;
	}
	BindString(1, pPlayer);
	BindInt(2, Points);
	BindInt(3, Points);
	bool End;
	return Step(&End, pError, ErrorSize);
}

std::unique_ptr<IDbConnection> CreateSqliteConnection(const char *pFilename, bool Setup)
{
	return std::make_unique<CSqliteConnection>(pFilename, Setup);
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