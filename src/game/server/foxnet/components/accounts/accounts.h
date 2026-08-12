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
	ACC_MIN_USERNAME_LENGTH = 4,
	ACC_MAX_USERNAME_LENGTH = 32,
	ACC_MIN_PASSW_LENGTH = 6,
	ACC_MAX_PASSW_LENGTH = 128,
};

enum Configs
{
	CONFIG_AUTLOGIN,
	CONFIG_FASTINPUTS,

	CONFIG_HIDEPOWERUPS,
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
	"FastInputs",

	"HidePowerUps",
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

	// For better prediction on cosmetics
	bool m_FastInputs = false;
	int m_FastInputAmount = 20;
	bool m_SentFastInput = false;

	bool m_HidePowerUps = false;
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

	void OnClientDrop(int ClientId, const char *pReason) override;
	void OnInit() override;
	void OnConsoleInit() override;
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ACCOUNTS_ACCOUNTS_H
