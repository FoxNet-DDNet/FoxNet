#include <base/str.h>

#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>

#include <game/server/foxnet/components/accounts/accountworker.h>

#include <gtest/gtest.h>

#include <limits>
#include <memory>

// MysqlInit() asserts on a second call, so it is deliberately left to
// score_test.cpp, which already initialises it for the whole binary.

struct FoxNetAccounts : public testing::TestWithParam<IDbConnection *> // NOLINT(readability-identifier-naming)
{
	FoxNetAccounts()
	{
		Connect();
	}

	~FoxNetAccounts() override
	{
		m_pConn->Disconnect();
	}

	void Connect()
	{
		ASSERT_TRUE(m_pConn->Connect(m_aError, sizeof(m_aError))) << m_aError;
		// Persistent databases like MySQL keep rows between runs
		ASSERT_TRUE(Wipe("foxnet_money_log"));
		ASSERT_TRUE(Wipe("foxnet_account_timers"));
		ASSERT_TRUE(Wipe("foxnet_accounts"));
		ASSERT_TRUE(AddAccount("payer", 1000));
		ASSERT_TRUE(AddAccount("receiver", 0));
	}

	bool Wipe(const char *pTable)
	{
		char aSql[128];
		str_format(aSql, sizeof(aSql), "DELETE FROM %s", pTable);
		if(!m_pConn->PrepareStatement(aSql, m_aError, sizeof(m_aError)))
			return false;
		int NumDeleted = 0;
		return m_pConn->ExecuteUpdate(&NumDeleted, m_aError, sizeof(m_aError));
	}

	bool AddAccount(const char *pUsername, int64_t StartMoney)
	{
		if(!m_pConn->PrepareStatement(
			   "INSERT INTO foxnet_accounts (Username, Password, RegisterDate, Money) VALUES (?, ?, ?, ?)",
			   m_aError, sizeof(m_aError)))
			return false;
		m_pConn->BindString(1, pUsername);
		m_pConn->BindString(2, "x");
		m_pConn->BindInt64(3, 0);
		m_pConn->BindInt64(4, StartMoney);
		int NumInserted = 0;
		return m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError)) && NumInserted == 1;
	}

	// Balance in the database, or -1 when the account is missing
	int64_t Money(const char *pUsername)
	{
		if(!m_pConn->PrepareStatement("SELECT Money FROM foxnet_accounts WHERE Username = ?", m_aError, sizeof(m_aError)))
			return -2;
		m_pConn->BindString(1, pUsername);
		return SingleInt64();
	}

	// ExpiresAt of a timer row, or -1 when there is none
	int64_t Timer(const char *pUsername, const char *pKey)
	{
		if(!m_pConn->PrepareStatement("SELECT ExpiresAt FROM foxnet_account_timers WHERE Username = ? AND `Key` = ?", m_aError, sizeof(m_aError)))
			return -2;
		m_pConn->BindString(1, pUsername);
		m_pConn->BindString(2, pKey);
		return SingleInt64();
	}

	int64_t LogRows()
	{
		if(!m_pConn->PrepareStatement("SELECT COUNT(*) FROM foxnet_money_log", m_aError, sizeof(m_aError)))
			return -2;
		return SingleInt64();
	}

	bool SetMoneyDirect(const char *pUsername, int64_t NewMoney)
	{
		if(!m_pConn->PrepareStatement("UPDATE foxnet_accounts SET Money = ? WHERE Username = ?", m_aError, sizeof(m_aError)))
			return false;
		m_pConn->BindInt64(1, NewMoney);
		m_pConn->BindString(2, pUsername);
		int NumUpdated = 0;
		return m_pConn->ExecuteUpdate(&NumUpdated, m_aError, sizeof(m_aError));
	}

	struct SDelta
	{
		bool m_Ok = false;
		bool m_Applied = false;
		int64_t m_Money = 0;
	};

	SDelta Delta(const char *pUsername, int64_t Amount, bool RequireFunds = false, bool Absolute = false, Write w = Write::NORMAL)
	{
		auto pRes = std::make_shared<CAccMoneyDeltaResult>();
		CAccMoneyDelta Req(pRes);
		str_copy(Req.m_aUsername, pUsername, sizeof(Req.m_aUsername));
		Req.m_Delta = Amount;
		Req.m_RequireFunds = RequireFunds;
		Req.m_Absolute = Absolute;

		SDelta Out;
		if(!CAccountsWorker::MoneyDelta(m_pConn, &Req, w, m_aError, sizeof(m_aError)))
			return Out;
		Out.m_Ok = w != Write::NORMAL || pRes->m_PrimarySucceeded;
		Out.m_Applied = pRes->m_Applied;
		Out.m_Money = pRes->m_Money;
		return Out;
	}

	CAccPayResult::EStatus Pay(int64_t Amount, int64_t Now = 1000, int64_t Cooldown = 300, Write w = Write::NORMAL,
		const char *pPayer = "payer", const char *pReceiver = "receiver")
	{
		auto pRes = std::make_shared<CAccPayResult>();
		CAccPay Req(pRes);
		str_copy(Req.m_aPayer, pPayer, sizeof(Req.m_aPayer));
		str_copy(Req.m_aReceiver, pReceiver, sizeof(Req.m_aReceiver));
		Req.m_Amount = Amount;
		Req.m_Now = Now;
		Req.m_Cooldown = Cooldown;

		if(!CAccountsWorker::Pay(m_pConn, &Req, w, m_aError, sizeof(m_aError)))
			return CAccPayResult::EStatus::FAILED;
		return pRes->m_Status;
	}

	// Streak the claim landed on, or -1 when the period was already claimed
	int64_t ClaimDaily(int64_t Period, Write w = Write::NORMAL, const char *pUsername = "payer")
	{
		auto pRes = std::make_shared<CAccDailyRewardResult>();
		CAccClaimDailyReward Req(pRes);
		str_copy(Req.m_aUsername, pUsername, sizeof(Req.m_aUsername));
		Req.m_Period = Period;
		Req.m_Now = Period * 86400;

		if(!CAccountsWorker::ClaimDailyReward(m_pConn, &Req, w, m_aError, sizeof(m_aError)))
			return -2;
		if(w != Write::NORMAL)
			return -1;
		if(!pRes->m_PrimarySucceeded)
			return -2;
		return pRes->m_Claimed ? pRes->m_Streak : -1;
	}

	IDbConnection *m_pConn{GetParam()};
	char m_aError[256] = {};

private:
	int64_t SingleInt64()
	{
		bool End = true;
		if(!m_pConn->Step(&End, m_aError, sizeof(m_aError)))
			return -2;
		if(End)
			return -1;
		const int64_t Value = m_pConn->GetInt64(1);
		while(!End)
		{
			if(!m_pConn->Step(&End, m_aError, sizeof(m_aError)))
				return -2;
		}
		return Value;
	}
};

// money deltas

TEST_P(FoxNetAccounts, CreditAdds)
{
	const auto Res = Delta("payer", 250);
	EXPECT_TRUE(Res.m_Ok);
	EXPECT_TRUE(Res.m_Applied);
	EXPECT_EQ(Res.m_Money, 1250);
	EXPECT_EQ(Money("payer"), 1250);
}

TEST_P(FoxNetAccounts, UnguardedDebitClampsAtZero)
{
	const auto Res = Delta("payer", -5000);
	EXPECT_TRUE(Res.m_Ok);
	EXPECT_EQ(Money("payer"), 0) << "a clamped debit must not go negative";
}

TEST_P(FoxNetAccounts, GuardedDebitRefusesWhenShort)
{
	const auto Res = Delta("payer", -5000, true);
	EXPECT_TRUE(Res.m_Ok);
	EXPECT_FALSE(Res.m_Applied);
	EXPECT_EQ(Money("payer"), 1000) << "a refused spend must leave the balance alone";
}

TEST_P(FoxNetAccounts, GuardedDebitTakesWhenAffordable)
{
	const auto Res = Delta("payer", -400, true);
	EXPECT_TRUE(Res.m_Applied);
	EXPECT_EQ(Res.m_Money, 600);
	EXPECT_EQ(Money("payer"), 600);
}

TEST_P(FoxNetAccounts, GuardedDebitOfExactBalanceSucceeds)
{
	const auto Res = Delta("payer", -1000, true);
	EXPECT_TRUE(Res.m_Applied);
	EXPECT_EQ(Money("payer"), 0);
}

TEST_P(FoxNetAccounts, AbsoluteOverwrites)
{
	const auto Res = Delta("payer", 77, false, true);
	EXPECT_TRUE(Res.m_Applied);
	EXPECT_EQ(Money("payer"), 77);
}

TEST_P(FoxNetAccounts, CreditIsRefusedRatherThanWrapping)
{
	ASSERT_TRUE(SetMoneyDirect("payer", std::numeric_limits<int64_t>::max() - 5));
	const auto Res = Delta("payer", 100);
	EXPECT_TRUE(Res.m_Ok);
	EXPECT_FALSE(Res.m_Applied);
	EXPECT_EQ(Money("payer"), std::numeric_limits<int64_t>::max() - 5) << "an overflowing credit must not wrap the balance negative";
}

TEST_P(FoxNetAccounts, DeltaOnBackupReplayDoesNothing)
{
	// The pool runs the worker again on the backup after the primary took it
	Delta("payer", 250, false, false, Write::NORMAL_SUCCEEDED);
	EXPECT_EQ(Money("payer"), 1000) << "a replayed delta would be applied twice";
}

TEST_P(FoxNetAccounts, DeltaOnFailedPrimaryDoesNothing)
{
	Delta("payer", 250, false, false, Write::NORMAL_FAILED);
	EXPECT_EQ(Money("payer"), 1000);
}

// transfers

TEST_P(FoxNetAccounts, PayMovesMoneyAndArmsCooldown)
{
	EXPECT_EQ(Pay(400), CAccPayResult::EStatus::OK);
	EXPECT_EQ(Money("payer"), 600);
	EXPECT_EQ(Money("receiver"), 400);
	EXPECT_EQ(Timer("payer", PAY_TIMER_KEY), 1300) << "cooldown should end at Now plus Cooldown";
}

TEST_P(FoxNetAccounts, PayWritesAnAuditRow)
{
	ASSERT_EQ(Pay(400), CAccPayResult::EStatus::OK);
	EXPECT_EQ(LogRows(), 1);
}

TEST_P(FoxNetAccounts, PayRefusedWhileOnCooldown)
{
	ASSERT_EQ(Pay(100, 1000, 300), CAccPayResult::EStatus::OK);
	EXPECT_EQ(Pay(100, 1200), CAccPayResult::EStatus::ON_COOLDOWN);
	EXPECT_EQ(Money("payer"), 900) << "a refused transfer must not move money";
	EXPECT_EQ(Money("receiver"), 100);
	EXPECT_EQ(LogRows(), 1);
}

TEST_P(FoxNetAccounts, PayAllowedOnceCooldownExpired)
{
	ASSERT_EQ(Pay(100, 1000, 300), CAccPayResult::EStatus::OK);
	EXPECT_EQ(Pay(100, 1301), CAccPayResult::EStatus::OK);
	EXPECT_EQ(Money("receiver"), 200);
}

TEST_P(FoxNetAccounts, PayRefusedWhenShort)
{
	EXPECT_EQ(Pay(5000), CAccPayResult::EStatus::INSUFFICIENT_FUNDS);
	EXPECT_EQ(Money("payer"), 1000);
	EXPECT_EQ(Money("receiver"), 0);
	EXPECT_EQ(Timer("payer", PAY_TIMER_KEY), -1) << "a refused transfer must not arm the cooldown";
	EXPECT_EQ(LogRows(), 0);
}

TEST_P(FoxNetAccounts, PayRollsBackWhenReceiverMissing)
{
	// The case the old two-call version could not get right: the payer was
	// debited before anything checked the receiver existed.
	EXPECT_EQ(Pay(400, 1000, 300, Write::NORMAL, "payer", "nobody"), CAccPayResult::EStatus::NO_RECEIVER);
	EXPECT_EQ(Money("payer"), 1000) << "the debit must be rolled back";
	EXPECT_EQ(Timer("payer", PAY_TIMER_KEY), -1);
	EXPECT_EQ(LogRows(), 0);
}

TEST_P(FoxNetAccounts, PayOnBackupReplayDoesNothing)
{
	Pay(400, 1000, 300, Write::NORMAL_SUCCEEDED);
	EXPECT_EQ(Money("payer"), 1000) << "a replayed transfer would move money twice";
	EXPECT_EQ(Money("receiver"), 0);
}

TEST_P(FoxNetAccounts, PayOnFailedPrimaryDoesNothing)
{
	Pay(400, 1000, 300, Write::NORMAL_FAILED);
	EXPECT_EQ(Money("payer"), 1000);
	EXPECT_EQ(Money("receiver"), 0);
}

// daily login streak

TEST_P(FoxNetAccounts, DailyFirstClaimStartsStreak)
{
	EXPECT_EQ(ClaimDaily(1000), 1);
}

TEST_P(FoxNetAccounts, DailySecondClaimSamePeriodRefused)
{
	ASSERT_EQ(ClaimDaily(1000), 1);
	EXPECT_EQ(ClaimDaily(1000), -1);
}

TEST_P(FoxNetAccounts, DailyConsecutivePeriodsGrowStreak)
{
	ASSERT_EQ(ClaimDaily(1000), 1);
	EXPECT_EQ(ClaimDaily(1001), 2);
	EXPECT_EQ(ClaimDaily(1002), 3);
}

TEST_P(FoxNetAccounts, DailyMissedPeriodResetsStreak)
{
	ASSERT_EQ(ClaimDaily(1000), 1);
	ASSERT_EQ(ClaimDaily(1001), 2);
	EXPECT_EQ(ClaimDaily(1003), 1) << "skipping a period should start over";
}

TEST_P(FoxNetAccounts, DailyPeriodInThePastRefused)
{
	ASSERT_EQ(ClaimDaily(1000), 1);
	EXPECT_EQ(ClaimDaily(999), -1) << "a backwards clock must not hand out another reward";
}

TEST_P(FoxNetAccounts, DailyOnFailedPrimaryGrantsNothing)
{
	ClaimDaily(1000, Write::NORMAL_FAILED);
	EXPECT_EQ(Timer("payer", DAILY_REWARD_TIMER_KEY), -1);
}

static auto g_pFoxNetSqliteConn = CreateSqliteConnection(":memory:", true);
#if defined(CONF_TEST_MYSQL)
// Same server the CI job provisions for score_test.cpp
CMysqlConfig gFoxNetMysqlConfig{
	"ddnet", // database
	"foxnet_test", // prefix
	"ddnet", // user
	"thebestpassword", // password
	"localhost", // ip
	"", // bindaddr
	3306, // port
	true, // setup
};
static auto g_pFoxNetMysqlConn = CreateMysqlConnection(gFoxNetMysqlConfig);
#endif

// Preprocessor directives cannot sit inside a macro argument list, so the
// conditional stays out here where testing::Values is an ordinary call.
static auto g_FoxNetTestValues{
	testing::Values(
#if defined(CONF_TEST_MYSQL)
		g_pFoxNetMysqlConn.get(),
#endif
		g_pFoxNetSqliteConn.get())};

INSTANTIATE_TEST_SUITE_P(Sql, FoxNetAccounts, g_FoxNetTestValues,
	[](const testing::TestParamInfo<FoxNetAccounts::ParamType> &Info) {
		// Named off the connection rather than the index, so the suffix is
		// right whether or not MySQL is compiled in. CI filters out -*SQLite*
		// on the job that has a MySQL server.
		return Info.param == g_pFoxNetSqliteConn.get() ? "SQLite" : "MySQL";
	});
