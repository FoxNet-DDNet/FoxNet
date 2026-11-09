#include "connection_pool.h"

#include "connection.h"

#include <base/dbg.h>
#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/thread.h>

#include <engine/shared/config.h>

#include <chrono>
#include <cstring>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// helper struct to hold thread data
struct CSqlExecData
{
	CSqlExecData(
		CDbConnectionPool::FRead pFunc,
		std::unique_ptr<const ISqlData> pThreadData,
		const char *pName);
	CSqlExecData(
		CDbConnectionPool::FWrite pFunc,
		std::unique_ptr<const ISqlData> pThreadData,
		const char *pName);
	CSqlExecData(
		CDbConnectionPool::Mode m,
		const char aFilename[64]);
	CSqlExecData(
		CDbConnectionPool::Mode m,
		const CMysqlConfig *pMysqlConfig);
	CSqlExecData(CDbConnectionPool::Mode m);
	~CSqlExecData() = default;

	enum
	{
		READ_ACCESS,
		WRITE_ACCESS,
		ADD_MYSQL,
		ADD_SQLITE,
		PRINT,
	} m_Mode;
	union
	{
		CDbConnectionPool::FRead m_pReadFunc;
		CDbConnectionPool::FWrite m_pWriteFunc;
		struct
		{
			CDbConnectionPool::Mode m_Mode;
			CMysqlConfig m_Config;
		} m_Mysql;
		struct
		{
			CDbConnectionPool::Mode m_Mode;
			char m_Filename[64];
		} m_Sqlite;
		struct
		{
			CDbConnectionPool::Mode m_Mode;
		} m_Print;
	} m_Ptr;

	std::unique_ptr<const ISqlData> m_pThreadData;
	const char *m_pName;
};

CSqlExecData::CSqlExecData(
	CDbConnectionPool::FRead pFunc,
	std::unique_ptr<const ISqlData> pThreadData,
	const char *pName) :
	m_Mode(READ_ACCESS),
	m_pThreadData(std::move(pThreadData)),
	m_pName(pName)
{
	m_Ptr.m_pReadFunc = pFunc;
}

CSqlExecData::CSqlExecData(
	CDbConnectionPool::FWrite pFunc,
	std::unique_ptr<const ISqlData> pThreadData,
	const char *pName) :
	m_Mode(WRITE_ACCESS),
	m_pThreadData(std::move(pThreadData)),
	m_pName(pName)
{
	m_Ptr.m_pWriteFunc = pFunc;
}

CSqlExecData::CSqlExecData(
	CDbConnectionPool::Mode m,
	const char aFilename[64]) :
	m_Mode(ADD_SQLITE),
	m_pThreadData(nullptr),
	m_pName("add sqlite server")
{
	m_Ptr.m_Sqlite.m_Mode = m;
	str_copy(m_Ptr.m_Sqlite.m_Filename, aFilename);
}
CSqlExecData::CSqlExecData(CDbConnectionPool::Mode m,
	const CMysqlConfig *pMysqlConfig) :
	m_Mode(ADD_MYSQL),
	m_pThreadData(nullptr),
	m_pName("add mysql server")
{
	m_Ptr.m_Mysql.m_Mode = m;
	mem_copy(&m_Ptr.m_Mysql.m_Config, pMysqlConfig, sizeof(m_Ptr.m_Mysql.m_Config));
}

CSqlExecData::CSqlExecData(CDbConnectionPool::Mode m) :
	m_Mode(PRINT),
	m_pThreadData(nullptr),
	m_pName("print database server")
{
	m_Ptr.m_Print.m_Mode = m;
}

void CDbConnectionPool::Print(Mode DatabaseMode)
{
	Enqueue(std::make_unique<CSqlExecData>(DatabaseMode));
}

void CDbConnectionPool::RegisterSqliteDatabase(Mode DatabaseMode, const char aFilename[64])
{
	Enqueue(std::make_unique<CSqlExecData>(DatabaseMode, aFilename));
}

void CDbConnectionPool::RegisterMysqlDatabase(Mode DatabaseMode, const CMysqlConfig *pMysqlConfig)
{
	Enqueue(std::make_unique<CSqlExecData>(DatabaseMode, pMysqlConfig));
}

// <FoxNet
bool CDbConnectionPool::Enqueue(std::unique_ptr<CSqlExecData> pData)
{
	// Reusing an occupied slot would destroy a query that has not run yet, so
	// refuse the new one and make the loss visible instead.
	if(m_pShared->m_NumQueued.load() >= (int)std::size(m_pShared->m_aQueries))
	{
		log_error("sql", "query queue is full (%d pending), dropped '%s'", (int)std::size(m_pShared->m_aQueries), pData->m_pName);
		// Complete the result as failed, otherwise whoever is waiting on it
		// keeps a pending callback that never fires.
		if(pData->m_pThreadData && pData->m_pThreadData->m_pResult)
		{
			pData->m_pThreadData->m_pResult->m_Success = false;
			pData->m_pThreadData->m_pResult->m_Completed.store(true);
		}
		return false;
	}
	m_pShared->m_aQueries[m_InsertIdx++] = std::move(pData);
	m_InsertIdx %= std::size(m_pShared->m_aQueries);
	m_pShared->m_NumQueued.fetch_add(1);
	m_pShared->m_NumBackup.Signal();
	return true;
}
// FoxNet>

void CDbConnectionPool::Execute(
	FRead pFunc,
	std::unique_ptr<const ISqlData> pSqlRequestData,
	const char *pName)
{
	Enqueue(std::make_unique<CSqlExecData>(pFunc, std::move(pSqlRequestData), pName));
}

void CDbConnectionPool::ExecuteWrite(
	FWrite pFunc,
	std::unique_ptr<const ISqlData> pSqlRequestData,
	const char *pName)
{
	Enqueue(std::make_unique<CSqlExecData>(pFunc, std::move(pSqlRequestData), pName));
}

void CDbConnectionPool::OnShutdown()
{
	if(m_Shutdown)
		return;
	m_Shutdown = true;
	m_pShared->m_Shutdown.store(true);
	m_pShared->m_NumBackup.Signal();
	int i = 0;
	while(m_pShared->m_Shutdown.load())
	{
		// print a log about every two seconds
		if(i % 20 == 0 && i > 0)
		{
			dbg_msg("sql", "Waiting for score threads to complete (%ds)", i / 10);
		}
		++i;
		std::this_thread::sleep_for(100ms);
	}
}

// The backup worker thread looks at write queries and stores them
// in the sqlite database (WRITE_BACKUP). It skips over read queries.
// After processing the query, it gets passed on to the Worker thread.
// This is done to not loose ranks when the server shuts down before all
// queries are executed on the mysql server
class CBackup
{
public:
	CBackup(std::shared_ptr<CDbConnectionPool::CSharedData> pShared, int DebugSql) :
		m_DebugSql(DebugSql), m_pShared(std::move(pShared)) {}
	static void Start(void *pUser);

private:
	bool m_DebugSql;

	void ProcessQueries();

	std::unique_ptr<IDbConnection> m_pWriteBackup;

	std::shared_ptr<CDbConnectionPool::CSharedData> m_pShared;
};

/* static */
void CBackup::Start(void *pUser)
{
	CBackup *pThis = (CBackup *)pUser;
	pThis->ProcessQueries();
	delete pThis;
}

void CBackup::ProcessQueries()
{
	for(int JobNum = 0;; JobNum++)
	{
		m_pShared->m_NumBackup.Wait();
		CSqlExecData *pThreadData = m_pShared->m_aQueries[JobNum % std::size(m_pShared->m_aQueries)].get();

		// work through all database jobs after OnShutdown is called before exiting the thread
		if(pThreadData == nullptr)
		{
			m_pShared->m_NumWorker.Signal();
			return;
		}

		if(pThreadData->m_Mode == CSqlExecData::ADD_SQLITE &&
			pThreadData->m_Ptr.m_Sqlite.m_Mode == CDbConnectionPool::Mode::WRITE_BACKUP)
		{
			m_pWriteBackup = CreateSqliteConnection(pThreadData->m_Ptr.m_Sqlite.m_Filename, true);
		}
		else if(pThreadData->m_Mode == CSqlExecData::WRITE_ACCESS && m_pWriteBackup.get())
		{
			bool Success = CDbConnectionPool::ExecSqlFunc(m_pWriteBackup.get(), pThreadData, Write::BACKUP_FIRST);
			if(m_DebugSql || !Success)
				dbg_msg("sql", "[%i] %s done on write backup database, Success=%i", JobNum, pThreadData->m_pName, Success);
		}
		m_pShared->m_NumWorker.Signal();
	}
}

// the worker threads executes queries on mysql or sqlite. If we write on
// a mysql server and have a backup server configured, we'll remove the
// entry from the backup server after completing it on the write server.
// static void Worker(void *pUser);
class CWorker
{
public:
	CWorker(std::shared_ptr<CDbConnectionPool::CSharedData> pShared, int DebugSql) :
		m_DebugSql(DebugSql), m_pShared(std::move(pShared)) {}
	static void Start(void *pUser);
	void ProcessQueries();

private:
	void Print(CDbConnectionPool::Mode DatabaseMode);

	bool m_DebugSql;

	// There are two possible configurations
	//  * sqlite mode: There exists exactly one READ and the same WRITE server
	//                 with no WRITE_BACKUP server
	//  * mysql mode: there can exist multiple READ server, there must be at
	//                most one WRITE server. The WRITE server for all DDNet
	//                Servers must be the same (to counteract double loads).
	//                There may be one WRITE_BACKUP sqlite server.
	// This variable should only change, before the worker threads
	std::vector<std::unique_ptr<IDbConnection>> m_vpReadConnections;
	std::unique_ptr<IDbConnection> m_pWriteConnection;
	std::unique_ptr<IDbConnection> m_pWriteBackup;

	std::shared_ptr<CDbConnectionPool::CSharedData> m_pShared;
};

/* static */
void CWorker::Start(void *pUser)
{
	CWorker *pThis = (CWorker *)pUser;
	pThis->ProcessQueries();
	delete pThis;
}

void CWorker::ProcessQueries()
{
	// remember last working server and try to connect to it first
	int ReadServer = 0;
	// enter fail mode when a sql request fails, skip read request during it and
	// write to the backup database until all requests are handled
	bool FailMode = false;
	// <FoxNet
	// When the FailMode breaker was last half-opened to retest the database.
	auto LastProbe = std::chrono::steady_clock::now();
	constexpr auto FailModeProbeInterval = 5s;
	// FoxNet>
	for(int JobNum = 0;; JobNum++)
	{
		if(FailMode && m_pShared->m_NumWorker.GetApproximateValue() == 0)
		{
			FailMode = false;
		}
		m_pShared->m_NumWorker.Wait();
		auto pThreadData = std::move(m_pShared->m_aQueries[JobNum % std::size(m_pShared->m_aQueries)]);
		// work through all database jobs after OnShutdown is called before exiting the thread
		if(pThreadData == nullptr)
		{
			m_pShared->m_Shutdown.store(false);
			return;
		}
		// <FoxNet: the slot is consumed, the main thread may fill it again
		m_pShared->m_NumQueued.fetch_sub(1);
		// FoxNet>
		bool Success = false;
		switch(pThreadData->m_Mode)
		{
		case CSqlExecData::READ_ACCESS:
		{
			for(size_t i = 0; i < m_vpReadConnections.size(); i++)
			{
				if(m_pShared->m_Shutdown)
				{
					dbg_msg("sql", "[%i] %s dismissed read request during shutdown", JobNum, pThreadData->m_pName);
					break;
				}
				if(FailMode)
				{
					dbg_msg("sql", "[%i] %s dismissed read request during FailMode", JobNum, pThreadData->m_pName);
					break;
				}
				int CurServer = (ReadServer + i) % (int)m_vpReadConnections.size();
				if(CDbConnectionPool::ExecSqlFunc(m_vpReadConnections[CurServer].get(), pThreadData.get(), Write::NORMAL))
				{
					ReadServer = CurServer;
					if(m_DebugSql)
						dbg_msg("sql", "[%i] %s done on read database %d", JobNum, pThreadData->m_pName, CurServer);
					Success = true;
					break;
				}
			}
			if(!Success)
			{
				// <FoxNet: back off before the next probe, see WRITE_ACCESS
				if(!FailMode)
					LastProbe = std::chrono::steady_clock::now();
				// FoxNet>
				FailMode = true;
			}
		}
		break;
		case CSqlExecData::WRITE_ACCESS:
		{
			// <FoxNet
			// Nothing clears FailMode while it is on, because writes go to the
			// backup and reads are dismissed, so neither can ever succeed on
			// the primary. Let one write through every few seconds to retest it.
			const bool Probe = FailMode && std::chrono::steady_clock::now() - LastProbe > FailModeProbeInterval;
			if(Probe)
				LastProbe = std::chrono::steady_clock::now();
			bool PrimaryTried = false;
			// FoxNet>
			if(m_pShared->m_Shutdown && m_pWriteBackup != nullptr)
			{
				dbg_msg("sql", "[%i] %s skipped to backup database during shutdown", JobNum, pThreadData->m_pName);
			}
			else if(FailMode && !Probe && m_pWriteBackup != nullptr)
			{
				dbg_msg("sql", "[%i] %s skipped to backup database during FailMode", JobNum, pThreadData->m_pName);
			}
			else
			{
				// <FoxNet
				// Retry here rather than dropping the query: the next Connect()
				// reestablishes a dropped connection, and holding it in place
				// keeps later writes for the same row behind it.
				PrimaryTried = true;
				const int MaxAttempts = FailMode ? 1 : 3;
				const auto Started = std::chrono::steady_clock::now();
				for(int Attempt = 0; Attempt < MaxAttempts; Attempt++)
				{
					if(Attempt > 0)
					{
						// A failure that took a while means the server is
						// unreachable rather than the connection being stale.
						// Retrying through its connect timeout would stall the
						// queue for minutes, so give up and let FailMode take
						// over.
						if(std::chrono::steady_clock::now() - Started > 2s)
						{
							dbg_msg("sql", "[%i] %s gave up retrying, the write database is not responding", JobNum, pThreadData->m_pName);
							break;
						}
						dbg_msg("sql", "[%i] %s retrying on write database (attempt %d/%d)", JobNum, pThreadData->m_pName, Attempt + 1, MaxAttempts);
						std::this_thread::sleep_for(100ms * Attempt);
					}
					if(CDbConnectionPool::ExecSqlFunc(m_pWriteConnection.get(), pThreadData.get(), Write::NORMAL))
					{
						if(m_DebugSql)
							dbg_msg("sql", "[%i] %s done on write database", JobNum, pThreadData->m_pName);
						Success = true;
						break;
					}
					if(m_pShared->m_Shutdown.load())
						break;
				}
				if(!Success)
					log_error("sql", "%s failed on write database after %d attempt(s)", pThreadData->m_pName, MaxAttempts);
				// FoxNet>
			}
			// enter fail mode if not successful
			// <FoxNet: a write that reached the primary means it is healthy again
			if(PrimaryTried)
			{
				FailMode = !Success;
				// Measure the probe interval from this attempt, so entering
				// FailMode waits before retesting instead of probing at once.
				if(FailMode)
					LastProbe = std::chrono::steady_clock::now();
			}
			// FoxNet>
			const Write w = Success ? Write::NORMAL_SUCCEEDED : Write::NORMAL_FAILED;
			if(m_pWriteBackup && CDbConnectionPool::ExecSqlFunc(m_pWriteBackup.get(), pThreadData.get(), w))
			{
				if(m_DebugSql)
					dbg_msg("sql", "[%i] %s done move write on backup database to non-backup table", JobNum, pThreadData->m_pName);
				Success = true;
			}
		}
		break;
		case CSqlExecData::ADD_MYSQL:
		{
			auto pMysql = CreateMysqlConnection(pThreadData->m_Ptr.m_Mysql.m_Config);
			switch(pThreadData->m_Ptr.m_Mysql.m_Mode)
			{
			case CDbConnectionPool::Mode::READ:
				m_vpReadConnections.push_back(std::move(pMysql));
				break;
			case CDbConnectionPool::Mode::WRITE:
				m_pWriteConnection = std::move(pMysql);
				break;
			case CDbConnectionPool::Mode::WRITE_BACKUP:
				m_pWriteBackup = std::move(pMysql);
				break;
			case CDbConnectionPool::Mode::NUM_MODES:
				break;
			}
			Success = true;
			break;
		}
		case CSqlExecData::ADD_SQLITE:
		{
			auto pSqlite = CreateSqliteConnection(pThreadData->m_Ptr.m_Sqlite.m_Filename, true);
			switch(pThreadData->m_Ptr.m_Sqlite.m_Mode)
			{
			case CDbConnectionPool::Mode::READ:
				m_vpReadConnections.push_back(std::move(pSqlite));
				break;
			case CDbConnectionPool::Mode::WRITE:
				m_pWriteConnection = std::move(pSqlite);
				break;
			case CDbConnectionPool::Mode::WRITE_BACKUP:
				m_pWriteBackup = std::move(pSqlite);
				break;
			case CDbConnectionPool::Mode::NUM_MODES:
				break;
			}
			Success = true;
			break;
		}
		case CSqlExecData::PRINT:
			Print(pThreadData->m_Ptr.m_Print.m_Mode);
			Success = true;
			break;
		}
		if(!Success)
			dbg_msg("sql", "[%i] %s failed on all databases", JobNum, pThreadData->m_pName);
		if(pThreadData->m_pThreadData != nullptr && pThreadData->m_pThreadData->m_pResult != nullptr)
		{
			pThreadData->m_pThreadData->m_pResult->m_Success = Success;
			pThreadData->m_pThreadData->m_pResult->m_Completed.store(true);
		}
	}
}

void CWorker::Print(CDbConnectionPool::Mode DatabaseMode)
{
	if(DatabaseMode == CDbConnectionPool::Mode::READ)
	{
		for(auto &pReadConnection : m_vpReadConnections)
			pReadConnection->Print("Read");
		if(m_vpReadConnections.empty())
			log_info("server", "There are no read databases");
	}
	else if(DatabaseMode == CDbConnectionPool::Mode::WRITE)
	{
		if(m_pWriteConnection)
			m_pWriteConnection->Print("Write");
		else
			log_info("server", "There are no write databases");
	}
	else if(DatabaseMode == CDbConnectionPool::Mode::WRITE_BACKUP)
	{
		if(m_pWriteBackup)
			m_pWriteBackup->Print("WriteBackup");
		else
			log_info("server", "There are no write backup databases");
	}
}

/* static */
bool CDbConnectionPool::ExecSqlFunc(IDbConnection *pConnection, CSqlExecData *pData, Write w)
{
	if(pConnection == nullptr)
	{
		dbg_msg("sql", "No database given");
		return false;
	}
	char aError[256] = "unknown error";
	if(!pConnection->Connect(aError, sizeof(aError)))
	{
		dbg_msg("sql", "failed connecting to db: %s", aError);
		return false;
	}
	bool Success = false;
	switch(pData->m_Mode)
	{
	case CSqlExecData::READ_ACCESS:
		Success = pData->m_Ptr.m_pReadFunc(pConnection, pData->m_pThreadData.get(), aError, sizeof(aError));
		break;
	case CSqlExecData::WRITE_ACCESS:
		Success = pData->m_Ptr.m_pWriteFunc(pConnection, pData->m_pThreadData.get(), w, aError, sizeof(aError));
		break;
	default:
		dbg_assert_failed("unreachable");
	}
	pConnection->Disconnect();
	if(!Success)
	{
		dbg_msg("sql", "%s failed: %s", pData->m_pName, aError);
	}
	return Success;
}

CDbConnectionPool::CDbConnectionPool()
{
	m_pShared = std::make_shared<CSharedData>();
	m_pWorkerThread = thread_init(CWorker::Start, new CWorker(m_pShared, g_Config.m_DbgSql), "database worker thread");
	m_pBackupThread = thread_init(CBackup::Start, new CBackup(m_pShared, g_Config.m_DbgSql), "database backup worker thread");
}

CDbConnectionPool::~CDbConnectionPool()
{
	OnShutdown();
	if(m_pWorkerThread)
		thread_wait(m_pWorkerThread);
	if(m_pBackupThread)
		thread_wait(m_pBackupThread);
}
