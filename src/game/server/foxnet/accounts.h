#ifndef GAME_SERVER_FOXNET_ACCOUNTS_H
#define GAME_SERVER_FOXNET_ACCOUNTS_H

#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/storage.h>
#include <optional>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <game/server/player.h>

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

	ACC_FLAG_AUTOLOGIN = 1 << 0,
	ACC_FLAG_HIDE_COSMETICS = 1 << 1,
	ACC_FLAG_HIDE_POWERUPS = 1 << 2,
};

struct CAccountSession
{
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	long m_RegisterDate = 0;
	char m_Name[MAX_NAME_LENGTH] = "";
	char m_LastName[MAX_NAME_LENGTH] = "";
	char CurrentIp[128] = "";
	char LastIp[128] = "";
	bool m_LoggedIn = false;
	long m_LastLogin = 0;
	int m_Port = 0;
	int ClientId = -1;
	long m_Flags = 0;
	int m_VoteMenuPage = 0;
	long m_Playtime = 0; // Minutes
	long m_Deaths = 0;
	long m_Kills = 0;
	long m_Level = 0;
	long m_XP = 0;
	long m_Money = 0;

	CInventory m_Inventory;

	int m_LoginTick = 0;
	bool m_Disabled = false;
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
	bool ChangePassword(int ClientId, const char *pOldPassword, const char *pNewPassword, const char *pNewPassword2);

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
	void EditAccount(const char *pUsername, const char *pVariable, const char *pValue);
	void RemoveItem(const char *pUsername, const char *pItemName);

	// Returns XP needed for next level
	int NeededXP(int Level);
};

#endif // GAME_SERVER_FOXNET_ACCOUNTS_H