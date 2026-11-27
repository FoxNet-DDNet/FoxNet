#ifndef GAME_SERVER_FOXNET_COMPONENTS_ACCOUNTS_ACCOUNTS_H
#define GAME_SERVER_FOXNET_COMPONENTS_ACCOUNTS_ACCOUNTS_H

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/storage.h>

#include <game/server/player.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

struct CAccResult;
class CDbConnectionPool;
class CGameContext;
class IDbConnection;
class IServer;
struct ISqlData;

enum
{
	ACC_MIN_USERNAME_LENGTH = 3,
	ACC_MAX_USERNAME_LENGTH = 32,
	ACC_MIN_PASSW_LENGTH = 6,
	ACC_MAX_PASSW_LENGTH = 128,
};

// Row key in foxnet_account_timers holding the daily login streak
constexpr const char *DAILY_REWARD_TIMER_KEY = "daily_login";
// Row key holding when a player may transfer money again
constexpr const char *PAY_TIMER_KEY = "pay";
// Row key prefix of the advent calendar, one row per year, see CAccounts::AdventKey
constexpr const char *ADVENT_TIMER_KEY_PREFIX = "advent_";

// Highest December day the calendar can cover. The collected days are a
// bitmask in an INTEGER column, which is 32 bit signed on MySQL.
constexpr int ADVENT_MAX_DAYS = 24;

// Hardcoded reward range for one December day. What a player actually gets is
// rolled out of these per account, so no two players get the same.
class CAdventDay
{
public:
	int64_t m_MoneyMin;
	int64_t m_MoneyMax;
	int64_t m_XpMin;
	int64_t m_XpMax;
	int m_Items;
	// Items roll from this rarity range, and one of them is always of
	// m_MaxRarity. Keep m_Items within the number of items the range holds,
	// a draw is distinct and simply runs short otherwise.
	EItemRarity m_MinRarity;
	EItemRarity m_MaxRarity;
};

// One rolled item in a reward mail
class CRewardItem
{
public:
	const CItemConfig *m_pItem = nullptr;
	int m_Days = 0;
};

// How long a drawn reward item lasts: a count rolled between m_MinUnits and
// m_MaxUnits inclusive, times m_UnitDays. Keeping the unit separate lets a
// range stay on whole weeks or months instead of landing on odd day counts.
// The default is the 7, 14 or 21 days rewards have always handed out, and
// m_MinUnits == m_MaxUnits makes every item last the same.
class CRewardDuration
{
public:
	int m_MinUnits = 1;
	int m_MaxUnits = 3;
	int m_UnitDays = 7;
};

enum Configs
{
	CONFIG_AUTLOGIN,
	CONFIG_SPAWNSOLO,
	CONFIG_FASTINPUTS,

	CONFIG_HIDEPOWERUPS,
	CONFIG_HIDEDISTRACTINGEFFECTS,
	CONFIG_HATITEMFLAGS,

	CONFIG_SHOWWEAPONDROPS,
	CONFIG_WEAPONDROPSUSINGVOTENO,

	CONFIG_COSMETIC_RAINBOW,
	CONFIG_COSMETIC_GUNS,
	CONFIG_COSMETIC_INDICATORS,
	CONFIG_COSMETIC_DEATHS,
	CONFIG_COSMETIC_TRAILS,
	CONFIG_COSMETIC_HATS,
	CONFIG_COSMETIC_EFFECTS,
	NUM_CONFIGS
};

constexpr const char *g_apAccConfigNames[NUM_CONFIGS] = {
	"AutoLogin",
	"Spawnsolo",

	"FastInputs",

	"HidePowerUps",
	"HideDistractingEffects",
	"HatItemFlags",

	"ShowWeaponDrops",
	"WeaponDropsUsingVoteNo",

	"CosmeticShowRainbow",
	"CosmeticShowGuns",
	"CosmeticShowIndicators",
	"CosmeticShowDeaths",
	"CosmeticShowTrails",
	"CosmeticShowHats",
	"CosmeticShowEffects",
};

class CMailBox
{
public:
	CMailBox() = default;
	class CMail
	{
	public:
		int64_t m_MailId;
		char m_aSubject[512];
		char m_aMessage[2048];
		char m_aCmd[4096];
		char m_aCmdName[4096];
		bool m_UsedCmd;
		bool m_ClaimPending = false;
		bool m_Unread;
	};
	std::vector<CMail> m_vMails;
	void Clear()
	{
		m_vMails.clear();
	}
};

class CAccConfigs
{
public:
	bool m_AutoLogin = false;

	bool m_UseSpawnSolo = true;
	
	// For better prediction on cosmetics
	bool m_FastInputs = false;
	int m_FastInputAmount = 20;
	bool m_SentFastInput = false;

	bool m_HidePowerUps = false;
	// Hides progression effects that are not cosmetic snapped (level ups, case opening, money text)
	bool m_HideDistractingEffects = false;
	int m_HatItemFlags = 0;

	bool m_ShowWeaponDrops = true;
	bool m_WeaponDropsUsingVoteNo = true;

	class CCosmeticConfig
	{
	public:
		bool m_ShowRainbow = true;
		bool m_ShowGuns = true;
		bool m_ShowIndicators = true;
		bool m_ShowDeaths = true;
		bool m_ShowTrails = true;
		bool m_ShowHats = true;
		bool m_ShowEffects = true;
	} m_Cosmetics;

	void ToggleCosmetics()
	{
		bool NewState = !(m_Cosmetics.m_ShowRainbow && m_Cosmetics.m_ShowGuns &&
				  m_Cosmetics.m_ShowIndicators && m_Cosmetics.m_ShowDeaths &&
				  m_Cosmetics.m_ShowTrails && m_Cosmetics.m_ShowHats &&
				  m_Cosmetics.m_ShowEffects);
		m_Cosmetics.m_ShowRainbow = NewState;
		m_Cosmetics.m_ShowGuns = NewState;
		m_Cosmetics.m_ShowIndicators = NewState;
		m_Cosmetics.m_ShowDeaths = NewState;
		m_Cosmetics.m_ShowTrails = NewState;
		m_Cosmetics.m_ShowHats = NewState;
		m_Cosmetics.m_ShowEffects = NewState;
	}
};

class CAccountSession
{
public:
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	int64_t m_RegisterDate = 0;
	char m_aName[MAX_NAME_LENGTH] = "";
	char m_aLastName[MAX_NAME_LENGTH] = "";
	char m_aCurrentIp[NETADDR_MAXSTRSIZE] = "";
	char m_aLastIp[NETADDR_MAXSTRSIZE] = "";
	bool m_LoggedIn = false;
	int64_t m_LastLogin = 0;
	int m_Port = 0;
	int m_ClientId = -1;
	int64_t m_Playtime = 0; // Minutes
	int64_t m_Deaths = 0;
	int64_t m_Kills = 0;
	int64_t m_Level = 0;
	int64_t m_XP = 0;
	int64_t m_Money = 0;

	CInventory m_Inventory;

	int m_LoginTick = 0;
	bool m_Disabled = false;

	CMailBox m_MailBox;
	int64_t m_LastMailboxFetch = 0; // unix seconds of last successful fetch
	bool m_MailboxFetchPending = false;

	// Money is owned by the database. This is a cached copy for display, and
	// m_MoneyInFlight counts deltas sent but not acknowledged, so a refresh
	// does not overwrite the balance with a value that predates them.
	int m_MoneyInFlight = 0;
	int64_t m_LastMoneyFetch = 0;
	bool m_MoneyFetchPending = false;

	// Advent calendar days already collected, bit 0 is December 1. Owned by the
	// database, m_AdventYear is the year the mask was read for.
	int m_AdventMask = 0;
	int64_t m_AdventYear = 0;
	int64_t m_LastAdventFetch = 0;
	bool m_AdventFetchPending = false;
	bool m_AdventClaimPending = false;

	CAccConfigs m_Configs;
};

struct CPendingAccResult
{
	std::shared_ptr<CAccResult> m_pRes;
	std::function<void(CAccResult &)> m_Callback;
};

class CAccounts : public CServerComponent
{
	// Password hashing
	SHA256_DIGEST HashPassword(const char *pPassword);

	std::vector<CPendingAccResult> m_vPending;
	void AddPending(const std::shared_ptr<CAccResult> &pRes, std::function<void(CAccResult &)> &&Cb);

	CDbConnectionPool *DbPool();

	static void ConRegister(IConsole::IResult *pResult, void *pUserData);
	static void ConPassword(IConsole::IResult *pResult, void *pUserData);
	static void ConLogin(IConsole::IResult *pResult, void *pUserData);
	static void ConLogout(IConsole::IResult *pResult, void *pUserData);
	static void ConProfile(IConsole::IResult *pResult, void *pUserData);
	static void ConDisable(IConsole::IResult *pResult, void *pUserData);
	static void ConDelete(IConsole::IResult *pResult, void *pUserData);
	static void ConForcePassword(IConsole::IResult *pResult, void *pUserData);
	static void ConForceLogin(IConsole::IResult *pResult, void *pUserData);
	static void ConForceLogout(IConsole::IResult *pResult, void *pUserData);

	static void ConPayMoney(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveMoney(IConsole::IResult *pResult, void *pUserData);
	static void ConSetMoney(IConsole::IResult *pResult, void *pUserData);
	static void ConGiveXp(IConsole::IResult *pResult, void *pUserData);
	static void ConSetXp(IConsole::IResult *pResult, void *pUserData);
	static void ConGivePlaytime(IConsole::IResult *pResult, void *pUserData);
	static void ConSetPlaytime(IConsole::IResult *pResult, void *pUserData);
	static void ConSetLevel(IConsole::IResult *pResult, void *pUserData);
	static void ConSetDeaths(IConsole::IResult *pResult, void *pUserData);

	static void ConTop5Money(IConsole::IResult *pResult, void *pUserData);
	static void ConTop5Level(IConsole::IResult *pResult, void *pUserData);
	static void ConTop5Playtime(IConsole::IResult *pResult, void *pUserData);

	static void ConNewMail(IConsole::IResult *pResult, void *pUserData);
	static void ConNewGlobalMail(IConsole::IResult *pResult, void *pUserData);

	static void ConReport(IConsole::IResult *pResult, void *pUserData);

public:
	bool Register(int ClientId, const char *pUsername, const char *pPassword);
	bool ChangePassword(int ClientId, const char *pOldPassword, const char *pNewPassword);

	void AutoLogin(int ClientId);
	bool ForceLogin(int ClientId, const char *pUsername, bool Silent = false, bool Auto = false);

	void Login(int ClientId, const char *pUsername, const char *pPassword);
	bool Logout(int ClientId, bool WaitForCompletion = false); // immediate

	void OnLogin(int ClientId, struct CAccResult &Res, const char *pSuccessMessage);
	void OnLogout(int ClientId, CAccountSession &AccInfo, bool WaitForCompletion = false);

	void SaveAccountsInfo(int ClientId, CAccountSession &AccInfo);
	void DisableAccount(const char *pUsername, bool Disable);
	void DeleteAccount(int ClientId, const char *pUsername);

	void LogoutAllAccountsPort(int Port, const char *pInstance, bool WaitForCompletion = false);
	void ShowAccProfile(int ClientId, const char *pName);

	void SaveAllAccounts();

	void Top5(int ClientId, const char *pType, int Offset = 0);

	void SetPlayerName(int ClientId, const char *pName);
	void RemoveItem(const char *pUsername, const char *pItemName);

	void SetPassword(const char *pUsername, const char *pNewPassword);

	// Returns XP needed for next level
	int NeededXP(int Level);

	void FetchMailBox();

	// Bulk mail operations
	void MarkAllMailsRead(const char *pUsername);
	void DeleteAllReadMails(const char *pUsername);

	void SetMailRead(const char *pUsername, int64_t MailId, bool Read);
	void SetMailUsedCmd(const char *pUsername, int64_t MailId, bool Used);
	void ClaimMailReward(int ClientId, int64_t MailId, std::function<void(bool DbSuccess, bool Claimed)> &&Cb);
	void DeleteMail(const char *pUsername, int64_t MailId);

	void NewMail(const char *pUsername, const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd);
	void NewMail(int ClientId, const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd);
	void NewGlobalMail(const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd, bool IncludeDisabled = false, bool OnlyLoggedIn = false, int MinLevel = 0);

	// Moves money in the database and reports the balance it ended up at. The
	// callback gets (DbSuccess, Applied, NewBalance) and only runs while the
	// client is still logged into the same account.
	void ApplyMoneyDelta(int ClientId, int64_t Delta, bool RequireFunds, std::function<void(bool, bool, int64_t)> &&Cb, bool Absolute = false);
	// Overwrites the balance, for an admin set_money
	void SetMoney(int ClientId, int64_t Money);
	// Money change not tied to a session, used to refund a spend whose session left
	void MoneyDeltaByUsername(const char *pUsername, int64_t Delta);
	// Transfers money between two logged in accounts in one database transaction
	void PayMoney(int PayerClientId, int ReceiverClientId, int64_t Amount);
	// Re-reads balances from the write database, so changes made straight in the
	// database show up without a relog
	void RefreshMoney();

	// Claims today's login streak day and mails the reward if it was not claimed yet
	void ClaimDailyReward(int ClientId);
	void SendDailyRewardMail(const char *pUsername, int64_t Streak);

	// Draws Count distinct random buyable items from the given rarities, each
	// lasting a duration rolled out of Duration. Runs short rather than
	// repeating when the rarities hold fewer items than asked for, and is empty
	// when none match.
	std::vector<CRewardItem> PickRandomRewardItems(const std::vector<EItemRarity> &vAllowedRarities, int Count, const CRewardDuration &Duration = CRewardDuration());
	// Buyable, non upgrade items of the given rarities, ordered by name so that a
	// caller with a seeded generator draws the same set every time
	std::vector<const CItemConfig *> RewardCandidates(const std::vector<EItemRarity> &vAllowedRarities);
	// Appends up to Count items that are not in *pvOut yet, so a reward built
	// out of several draws still never lists the same item twice
	void DrawRewardItems(const std::vector<const CItemConfig *> &vCandidates, int Count, std::mt19937 &Rng, std::vector<CRewardItem> *pvOut, const CRewardDuration &Duration = CRewardDuration());
	// Writes one drawn item into a mail's reward command and its contents
	// listing. Items that do not expire are granted and listed without a
	// duration, see CShop::GiveItem.
	static void AppendRewardItem(const CRewardItem &Item, char *pCmd, int CmdSize, char *pCmdName, int CmdNameSize);

	// Advent calendar. The window runs December 1 to sv_advent_calendar_days,
	// shifted by sv_advent_calendar_reset_hour. Returns false outside it.
	static bool AdventWindow(int *pDay, int64_t *pYear);
	static void AdventKey(char *pBuf, int Size, int64_t Year);
	static const CAdventDay &AdventDay(int Day);
	// Rolls a day's hardcoded range into a concrete reward. Deterministic per
	// account, year and day, so a collected day's money and XP can be shown
	// again without storing them. Pass nullptr for pvItems to skip the item
	// roll, which depends on the shop registry and so is not stable over time.
	void RollAdventReward(const char *pUsername, int64_t Year, int Day, int64_t *pMoney, int64_t *pXp, std::vector<CRewardItem> *pvItems);
	void FetchAdvent();
	// Flips today's bit if it is still clear, then mails what it rolled
	void ClaimAdventDay(int ClientId, int Day);
	void SendAdventMail(const char *pUsername, int64_t Year, int Day);
	// Shared by the mail itself and by the claim waiting to open that mail
	static void AdventMailSubject(char *pBuf, int Size, int Day);

	void OnClientDrop(int ClientId, const char *pReason) override;
	void OnInit() override;
	void OnConsoleInit() override;
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ACCOUNTS_ACCOUNTS_H
