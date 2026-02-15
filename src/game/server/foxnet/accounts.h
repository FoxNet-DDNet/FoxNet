#ifndef GAME_SERVER_FOXNET_ACCOUNTS_H
#define GAME_SERVER_FOXNET_ACCOUNTS_H

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/storage.h>

#include <game/server/player.h>

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
		char m_aSubject[64];
		char m_aMessage[512];
		char m_aCmd[256];
		char m_aCmdName[128];
		bool m_UsedCmd;
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

	// From TClient
	bool m_FastInputs = false;
	int m_FastInputAmount = 20;
	bool m_SentFastInput = false;

	bool m_HidePowerUps = false;
	int m_HatItemFlags = 0;

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
};

class CAccountSession
{
public:
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	long m_RegisterDate = 0;
	char m_pName[MAX_NAME_LENGTH] = "";
	char m_LastName[MAX_NAME_LENGTH] = "";
	char CurrentIp[128] = "";
	char LastIp[128] = "";
	bool m_LoggedIn = false;
	long m_LastLogin = 0;
	int m_Port = 0;
	int ClientId = -1;
	long m_Playtime = 0; // Minutes
	long m_Deaths = 0;
	long m_Kills = 0;
	long m_Level = 0;
	long m_XP = 0;
	long m_Money = 0;

	CInventory m_Inventory;

	int m_LoginTick = 0;
	bool m_Disabled = false;

	CMailBox m_MailBox;
	long m_LastMailboxFetch = 0; // unix seconds of last successful fetch
	bool m_MailboxFetchPending = false;

	CAccConfigs m_Configs;
};

struct CPendingAccResult
{
	std::shared_ptr<CAccResult> m_pRes;
	std::function<void(CAccResult &)> m_Callback;
};

class CAccounts
{
	CGameContext *m_pGameServer = nullptr;
	CDbConnectionPool *m_pPool;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	// Password hashing
	SHA256_DIGEST HashPassword(const char *pPassword);

	std::vector<CPendingAccResult> m_vPending;
	void AddPending(const std::shared_ptr<CAccResult> &pRes, std::function<void(CAccResult &)> &&Cb);

public:
	void Init(CGameContext *pGameServer, CDbConnectionPool *pPool);

	void Tick();

	bool Register(int ClientId, const char *pUsername, const char *pPassword);
	bool ChangePassword(int ClientId, const char *pOldPassword, const char *pNewPassword);

	void AutoLogin(int ClientId);
	bool ForceLogin(int ClientId, const char *pUsername, bool Silent = false, bool Auto = false);

	void Login(int ClientId, const char *pUsername, const char *pPassword);
	bool Logout(int ClientId); // immediate

	void OnLogin(int ClientId, const struct CAccResult &Res);
	void OnLogout(int ClientId, const CAccountSession AccInfo);

	void SaveAccountsInfo(int ClientId, const CAccountSession AccInfo);
	void DisableAccount(const char *pUsername, bool Disable);

	void LogoutAllAccountsPort(int Port);
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
	void ClaimAllMailRewards(const char *pUsername);
	void DeleteAllReadMails(const char *pUsername);

	void SetMailRead(const char *pUsername, int64_t MailId, bool Read);
	void SetMailUsedCmd(const char *pUsername, int64_t MailId, bool Used);
	void DeleteMail(const char *pUsername, int64_t MailId);

	void NewMail(const char *pUsername, const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd);
	void NewMail(int ClientId, const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd);
	void NewGlobalMail(const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd, bool IncludeDisabled = false, bool OnlyLoggedIn = false, int MinLevel = 0);
};

#endif // GAME_SERVER_FOXNET_ACCOUNTS_H