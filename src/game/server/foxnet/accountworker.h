#ifndef GAME_SERVER_FOXNET_ACCOUNTWORKER_H
#define GAME_SERVER_FOXNET_ACCOUNTWORKER_H

#include "accounts.h"

#include <engine/server/databases/connection_pool.h>
#include <engine/shared/protocol.h>

#include <game/server/player.h>

class CGameContext;
class IDbConnection;
class IGameController;

struct CAccResult : ISqlResult
{
	bool m_Found = false;
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	char m_PlayerName[MAX_NAME_LENGTH] = "";
	char m_LastPlayerName[MAX_NAME_LENGTH] = "";
	char m_CurrentIP[46] = "";
	char m_LastIP[46] = "";
	long m_RegisterDate = 0;
	int m_LoggedIn = 0;
	long m_LastLogin = 0;
	int m_Port = 0;
	int m_ClientId = -1;
	long m_Playtime = 0;
	long m_Deaths = 0;
	long m_Kills = 0;
	long m_Level = 0;
	long m_XP = 0;
	long m_Money = 0;
	bool m_Disabled = false;
	CInventory m_Inventory;
	CMailBox m_MailBox;
	CAccConfigs m_Configs;
};

struct CAccRegisterRequest : ISqlData
{
	CAccRegisterRequest(std::shared_ptr<CAccResult> pRes) :
		ISqlData(std::move(pRes)) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	char m_PasswordHash[ACC_MAX_PASSW_LENGTH] = "";
	long m_RegisterDate = 0;
};
struct CAccLoginRequest : ISqlData
{
	CAccLoginRequest(std::shared_ptr<CAccResult> pRes) :
		ISqlData(std::move(pRes)) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	char m_PasswordHash[256] = "";
};
struct CAccSelectByUser : ISqlData
{
	CAccSelectByUser(std::shared_ptr<ISqlResult> pRes) :
		ISqlData(std::move(pRes)) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
};
struct CAccSelectByLastName : ISqlData
{
	CAccSelectByLastName(std::shared_ptr<ISqlResult> pRes) :
		ISqlData(std::move(pRes)) {}
	char m_LastPlayerName[MAX_NAME_LENGTH] = "";
};
struct CAccSelectPortByUser : ISqlData
{
	CAccSelectPortByUser(std::shared_ptr<ISqlResult> pRes) :
		ISqlData(std::move(pRes)) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
};
struct CAccUpdLoginState : ISqlData
{
	CAccUpdLoginState() :
		ISqlData(nullptr) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	char m_PlayerName[MAX_NAME_LENGTH] = "";
	char m_CurrentIP[46] = "";
	long m_LastLogin = 0;
	int m_Port = 0;
	int m_ClientId = -1;
};
struct CAccSaveInfo : ISqlData
{
	CAccSaveInfo() :
		ISqlData(nullptr) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	long m_Playtime = 0;
	long m_Deaths = 0;
	long m_Kills = 0;
	long m_Level = 0;
	long m_XP = 0;
	long m_Money = 0;
	CInventory m_Inventory;
	CAccConfigs m_Configs;
};
struct CAccSetNameReq : ISqlData
{
	CAccSetNameReq() :
		ISqlData(nullptr) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	char m_NewPlayerName[MAX_NAME_LENGTH] = "";
};
struct CAccShowTop5 : ISqlData
{
	CAccShowTop5() :
		ISqlData(nullptr) {}
	int m_ClientId;
	char m_Type[16];
	int m_Offset = 0;
	CGameContext *m_pGameServer;
};
struct CAccDisable : ISqlData
{
	CAccDisable() :
		ISqlData(nullptr) {}
	bool m_Disable;
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
};
struct CAccRemoveItem : ISqlData
{
	CAccRemoveItem() :
		ISqlData(nullptr) {}
	char m_aItemName[64];
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
};

struct CAccSetPassword : ISqlData
{
	CAccSetPassword() :
		ISqlData(nullptr) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	char m_aNewPasswordHash[ACC_MAX_PASSW_LENGTH] = "";
};

struct CAccMailAcknowledge : CAccResult
{
	int64_t m_MailId = 0;
};

// Request stays the same, carries a result ptr
struct CAccNewMail : ISqlData
{
	CAccNewMail(std::shared_ptr<CAccMailAcknowledge> pRes) :
		ISqlData(std::move(pRes))
	{
		m_MailId = 0;
		m_aUsername[0] = 0;
		m_aSubject[0] = 0;
		m_aMessage[0] = 0;
		m_aCmd[0] = 0;
		m_aCmdName[0] = 0;
		m_UsedCmd = false;
		m_Unread = true;
	}
	int64_t m_MailId;
	char m_aUsername[ACC_MAX_USERNAME_LENGTH];
	char m_aSubject[64];
	char m_aMessage[1024];
	char m_aCmd[512];
	char m_aCmdName[512];
	bool m_UsedCmd;
	bool m_Unread;
};

struct CAccSetMailRead : ISqlData
{
	CAccSetMailRead() :
		ISqlData(nullptr) {}
	int64_t m_MailId = 0;
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	int m_Read = 0;
};

struct CAccMarkAllMailsRead : ISqlData
{
	CAccMarkAllMailsRead() :
		ISqlData(nullptr) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
};

struct CAccClaimAllMailRewards : ISqlData
{
	CAccClaimAllMailRewards() :
		ISqlData(nullptr) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
};
struct CAccDeleteAllRead : ISqlData
{
	CAccDeleteAllRead() :
		ISqlData(nullptr) {}
	char m_aUsername[ACC_MAX_USERNAME_LENGTH];
};

struct CAccSetMailUsedCmd : ISqlData
{
	CAccSetMailUsedCmd() :
		ISqlData(nullptr) {}
	int64_t m_MailId = 0;
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
	int m_UsedCmd = 0;
};

struct CAccDeleteMail : ISqlData
{
	CAccDeleteMail() :
		ISqlData(nullptr) {}
	int64_t m_MailId = 0;
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
};

struct CBulkMailResult : public CAccResult
{
	int m_Inserted = 0;
	char m_aSubject[64]{};

	CBulkMailResult()
	{
		m_Inserted = 0;
		m_aSubject[0] = 0;
		m_Success = false;
		m_Completed.store(false);
	}
};

struct CAccBulkNewMail : ISqlData
{
	CAccBulkNewMail(std::shared_ptr<CBulkMailResult> pRes) :
		ISqlData(std::move(pRes))
	{
		m_IncludeDisabled = false;
		m_OnlyLoggedIn = false;
		m_MinLevel = -1;
		m_aSubject[0] = 0;
		m_aMessage[0] = 0;
		m_aCmd[0] = 0;
		m_aCmdName[0] = 0;
	}
	char m_aSubject[64];
	char m_aMessage[512];
	char m_aCmd[256];
	char m_aCmdName[64];
	bool m_IncludeDisabled;
	bool m_OnlyLoggedIn;
	int m_MinLevel;
};

struct CAccountsWorker
{
	static bool Register(IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize);
	static bool Login(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize);
	static bool SelectByUsername(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize);
	static bool SelectByLastPlayerName(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize);
	static bool SelectPortByUsername(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize);
	static bool UpdateLoginState(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool UpdateLogoutState(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool SetPlayerName(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool SaveInfo(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool ShowTop5(IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize);
	static bool DisableAccount(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool RemoveItem(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool SetPassword(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);

	static bool MarkAllMailsRead(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool ClaimAllMailRewards(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool DeleteAllReadMails(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);

	static bool SetMailRead(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool SetMailUsedCmd(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool DeleteMail(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);

	static bool NewMail(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
	static bool NewGlobalMail(IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize);
};

#endif