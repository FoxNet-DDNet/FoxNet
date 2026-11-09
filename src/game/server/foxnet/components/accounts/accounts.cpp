#include "accounts.h"

#include "accountworker.h"

#include <base/hash.h>
#include <base/hash_ctxt.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/server.h>
#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/server/server.h>
#include <engine/server/webhook_format.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/foxnet/components/shop.h>
#include <game/server/foxnet/components/votemenu.h>
#include <game/server/foxnet/fontconvert.h>
#include <game/server/foxnet/item_registry.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

void CAccounts::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Accounts are disabled");
		return;
	}
	if(!g_Config.m_SvAccountsAllowRegister)
	{
		pSelf->SendChatTarget(ClientId, "Account registration is disabled");
		return;
	}

	if(pSelf->GetAcc(ClientId)->m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You are already logged in");
		return;
	}
	const char *pUser = pResult->GetString(0);
	const char *pPass = pResult->GetString(1);

	pSelf->Register(ClientId, pUser, pPass);
}

void CAccounts::ConPassword(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Accounts are disabled");
		return;
	}

	if(!pSelf->GetAcc(ClientId)->m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You aren't logged in");
		return;
	}
	const char *pOldPass = pResult->GetString(0);
	const char *pPass = pResult->GetString(1);

	pSelf->ChangePassword(ClientId, pOldPass, pPass);
}

void CAccounts::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Accounts are disabled");
		return;
	}

	if(pSelf->GetAcc(ClientId)->m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You are already logged in");
		return;
	}

	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	if(pPlayer->m_AccLoginAttempts >= g_Config.m_SvAccountsMaxLoginAttempts)
	{
		const char *pAddr = pSelf->Server()->ClientAddrString(ClientId, false);
		char aCmdBuf[512];
		str_format(aCmdBuf, sizeof(aCmdBuf), "ban %s %d %s", pAddr, g_Config.m_SvRconBantime, "Too many '/login' attempts.");
		pSelf->Console()->ExecuteLineFlag(aCmdBuf, CFGFLAG_SERVER, IConsole::CLIENT_ID_FOXNET);
		return;
	}

	const char *pUser = pResult->GetString(0);
	const char *pPass = pResult->GetString(1);

	pSelf->Login(ClientId, pUser, pPass);
}

void CAccounts::ConLogout(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(pSelf->Logout(ClientId))
		pSelf->SendChatTarget(ClientId, "Logged out");
	else
		pSelf->SendChatTarget(ClientId, "Not logged in");
}

void CAccounts::ConProfile(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const char *pName = pResult->NumArguments() ? pResult->GetString(0) : pSelf->Server()->ClientName(pResult->m_ClientId);
	pSelf->ShowAccProfile(pResult->m_ClientId, pName);
}

void CAccounts::ConDisable(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const char *pUser = pResult->GetString(0);
	const int pValue = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : 1;

	pSelf->DisableAccount(pUser, pValue);
}

void CAccounts::ConDelete(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const char *pUser = pResult->GetString(0);

	pSelf->DeleteAccount(pResult->m_ClientId, pUser);
}

void CAccounts::ConForcePassword(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const char *pUser = pResult->GetString(0);
	const char *pNewPassword = pResult->GetString(1);

	pSelf->SetPassword(pUser, pNewPassword);
}

void CAccounts::ConForceLogin(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	const char *pName = pResult->GetString(0);
	pSelf->ForceLogin(Victim, pName);
}

void CAccounts::ConForceLogout(IConsole::IResult *pResult, void *pUserData)
{ // Logs out Current Acc Session, does not work across servers
	CAccounts *pSelf = (CAccounts *)pUserData;
	int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	pSelf->Logout(ClientId);
}

void CAccounts::ConPayMoney(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	int UserId = pResult->m_ClientId;
	const char *pName = pResult->GetString(0);
	if(!CheckClientId(UserId))
		return;

	if(!g_Config.m_SvAccounts)
		return;

	CPlayer *pPlayer = pSelf->GetPlayer(UserId);
	if(!pPlayer)
		return;
	if(!pPlayer->Acc()->m_LoggedIn)
	{
		pPlayer->SendChat("You need to be logged in for this.");
		return;
	}
	int Victim = pSelf->GameServer()->ClientIdByName(pName);
	if(!CheckClientId(Victim))
	{
		pPlayer->SendChat("Player not found");
		return;
	}
	CPlayer *pVictim = pSelf->GetPlayer(Victim);
	if(!pVictim)
	{
		pPlayer->SendChat("Player not found");
		return;
	}
	if(Victim == UserId)
	{
		pPlayer->SendChat("You can't pay yourself");
		return;
	}
	if(!pVictim->Acc()->m_LoggedIn)
	{
		pPlayer->SendChat("Player isn't logged in");
		return;
	}

	const int64_t Amount = pResult->GetInteger64(1);
	pPlayer->PayMoney(pVictim, Amount);
}

void CAccounts::ConGiveMoney(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;

	if(!g_Config.m_SvAccounts)
		return;

	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Amount = pResult->GetInteger64(1);
	pPlayer->GiveMoney(Amount, false);
	log_info("account", "Gave %" PRId64 " Money to player %s", Amount, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConSetMoney(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	const CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Money = std::max<int64_t>(0, pResult->GetInteger64(1));
	pSelf->SetMoney(ClientId, Money);
	log_info("account", "Set money to %" PRId64 " for player %s", Money, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConGiveXp(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	const CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Amount = pResult->GetInteger64(1);
	pPlayer->GiveXP(Amount, "", false);
	log_info("account", "Gave %" PRId64 " Xp to player %s", Amount, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConSetXp(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	const CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Amount = std::max<int64_t>(0, pResult->GetInteger64(1));
	pAcc->m_XP = Amount;
	pSelf->SaveAccountsInfo(ClientId, *pAcc);
	log_info("account", "Set xp to %" PRId64 " for player %s", Amount, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConGivePlaytime(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	const CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Amount = pResult->GetInteger64(1);

	pPlayer->GivePlaytime(Amount);

	log_info("account", "Gave %" PRId64 " Playtime to player %s", Amount, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConSetPlaytime(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	const CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Amount = std::max<int64_t>(0, pResult->GetInteger64(1));
	pAcc->m_Playtime = Amount;
	pSelf->SaveAccountsInfo(ClientId, *pAcc);
	log_info("account", "Set playtime to %" PRId64 " for player %s", Amount, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConSetLevel(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	const CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Amount = std::max<int64_t>(0, pResult->GetInteger64(1));
	pAcc->m_Level = Amount;
	pSelf->SaveAccountsInfo(ClientId, *pAcc);
	log_info("account", "Set level to %" PRId64 " for player %s", Amount, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConSetDeaths(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	const CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	CAccountSession *pAcc = pSelf->GetAcc(ClientId);
	if(!pAcc->m_LoggedIn)
		return;

	const int64_t Amount = std::max<int64_t>(0, pResult->GetInteger64(1));
	pAcc->m_Deaths = Amount;
	pSelf->SaveAccountsInfo(ClientId, *pAcc);
	log_info("account", "Set deaths to %" PRId64 " for player %s", Amount, pSelf->Server()->ClientName(ClientId));
}

void CAccounts::ConTop5Money(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	pSelf->Top5(pResult->m_ClientId, "money", pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0);
}
void CAccounts::ConTop5Level(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	pSelf->Top5(pResult->m_ClientId, "level", pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0);
}
void CAccounts::ConTop5Playtime(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	pSelf->Top5(pResult->m_ClientId, "playtime", pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0);
}

void CAccounts::ConNewMail(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const char *pUsername = pResult->GetString(0);
	const char *pSubject = pResult->GetString(1);
	const char *pMessage = pResult->GetString(2);
	const char *pCmdName = pResult->GetString(3);
	const char *pCmd = pResult->GetString(4);

	pSelf->NewMail(pUsername, pSubject, pMessage, pCmdName, pCmd);
}

void CAccounts::ConNewGlobalMail(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	int Param = 0;
	const char *pSubject = pResult->GetString(Param++);
	const char *pMessage = pResult->GetString(Param++);
	const char *pCmdName = pResult->GetString(Param++);
	const char *pCmd = pResult->GetString(Param++);

	int MinLevel = pResult->NumArguments() >= Param + 1 ? pResult->GetInteger(Param++) : 0;
	bool OnlyOnline = pResult->NumArguments() >= Param + 1 ? pResult->GetInteger(Param++) != 0 : 0;
	bool IncludeDisabled = pResult->NumArguments() >= Param + 1 ? pResult->GetInteger(Param++) != 0 : 0;

	pSelf->NewGlobalMail(pSubject, pMessage, pCmdName, pCmd, IncludeDisabled, OnlyOnline, MinLevel);
}

CDbConnectionPool *CAccounts::DbPool()
{
	return ((CServer *)Server())->DbPool();
}

void CAccounts::AddPending(const std::shared_ptr<CAccResult> &pRes, std::function<void(CAccResult &)> &&Cb)
{
	m_vPending.push_back({pRes, std::move(Cb)});
}

static bool IsExpectedPlayerStillInSlot(CGameContext *pGameServer, int ClientId, const CPlayer *pExpectedPlayer)
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && !pGameServer->Server()->ClientSlotEmpty(ClientId) && pGameServer->m_apPlayers[ClientId] == pExpectedPlayer;
}

static bool WaitForSqlCompletion(const std::shared_ptr<ISqlResult> &pResult, const char *pName)
{
	if(!pResult)
		return false;

	constexpr int64_t TimeoutSeconds = 10;
	const int64_t Start = time_get();
	const int64_t Timeout = TimeoutSeconds * time_freq();
	while(!pResult->m_Completed.load(std::memory_order_acquire))
	{
		if(time_get() - Start >= Timeout)
		{
			log_error("sql", "%s timed out during shutdown", pName);
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	if(!pResult->m_Success)
		log_error("sql", "%s failed during shutdown", pName);

	return pResult->m_Success;
}

SHA256_DIGEST CAccounts::HashPassword(const char *pPassword)
{
	SHA256_CTX Sha256Ctx;
	sha256_init(&Sha256Ctx);
	sha256_update(&Sha256Ctx, pPassword, str_length(pPassword));
	return sha256_finish(&Sha256Ctx);
}

void CAccounts::OnInit()
{
	if(GameServer()->IsMapReload())
		return;
	LogoutAllAccountsPort(Server()->Port(), g_Config.m_SvAccountsInstance);
}

void CAccounts::OnClientDrop(int ClientId, const char *pReason)
{
	if(!Logout(ClientId))
		GameServer()->m_aAccounts[ClientId] = CAccountSession();
}

void CAccounts::OnTick()
{
	if(m_vPending.empty())
	{
		FetchMailBox();
		RefreshMoney();
		return;
	}
	std::vector<std::pair<std::shared_ptr<CAccResult>, std::function<void(CAccResult &)>>> vReady;
	vReady.reserve(m_vPending.size());
	for(auto it = m_vPending.begin(); it != m_vPending.end();)
	{
		if(it->m_pRes->m_Completed.load())
		{
			vReady.emplace_back(it->m_pRes, it->m_Callback);
			it = m_vPending.erase(it);
		}
		else
			++it;
	}
	for(auto &Ready : vReady)
	{
		if(Ready.second)
			Ready.second(*Ready.first);
	}

	// <FoxNet: a busy pending list must not starve the money refresh, which is
	// what picks up a balance changed straight in the database. Safe here
	// because the callbacks above have already run.
	RefreshMoney();
	// FoxNet>
}

void CAccounts::AutoLogin(int ClientId)
{
	if(!DbPool())
		return;
	if(!g_Config.m_SvAccounts)
		return;
	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pExpectedPlayer)
		return;
	const char *pName = Server()->ClientName(ClientId);
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccSelectByLastName>(pRes);
	str_copy(pReq->m_LastPlayerName, pName, sizeof(pReq->m_LastPlayerName));
	AddPending(pRes, [this, ClientId, pExpectedPlayer, Name = std::string(pName)](CAccResult &Res) {
		if(!Res.m_Success || !Res.m_Found || Res.m_LastLogin == 0)
			return;
		if(!IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
			return;
		if(pExpectedPlayer->Acc()->m_LoggedIn)
			return;
		const char *pAddr = Server()->ClientAddrString(ClientId, false);
		if(str_comp(Res.m_LastIP, pAddr) != 0)
			return;
		if(str_comp(Res.m_LastPlayerName, Name.c_str()) != 0)
			return;
		if(!Res.m_Configs.m_AutoLogin)
			return;
		if(Res.m_LoggedIn)
		{
			if(!pExpectedPlayer->m_RetryAutoLogin)
			{
				pExpectedPlayer->m_LastAutoLoginAttempt = Server()->Tick();
				pExpectedPlayer->m_RetryAutoLogin = true;
				pExpectedPlayer->SendChat("Auto-login failed, retrying in a bit.");
			}
			else
				pExpectedPlayer->SendChat("Auto-login failed, account is already logged in.");

			return;
		}

		pExpectedPlayer->m_LastAutoLoginAttempt.reset();
		pExpectedPlayer->m_RetryAutoLogin = false;

		ForceLogin(ClientId, Res.m_aUsername, true, true);
	});
	DbPool()->Execute(CAccountsWorker::SelectByLastPlayerName, std::move(pReq), "acc select by last name");
}

bool CAccounts::ForceLogin(int ClientId, const char *pUsername, bool Silent, bool Auto)
{
	if(!DbPool() || !pUsername[0])
		return false;
	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pExpectedPlayer)
		return false;
	if(pExpectedPlayer->Acc()->m_LoggedIn)
	{
		if(!Silent)
			GameServer()->SendChatTarget(ClientId, "You are already logged in");
		return false;
	}
	if(pExpectedPlayer->m_LoginPending)
	{
		if(!Silent)
			GameServer()->SendChatTarget(ClientId, "Login already in progress");
		return false;
	}
	pExpectedPlayer->m_LoginPending = true;
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccSelectByUser>(pRes);
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	AddPending(pRes, [this, ClientId, pExpectedPlayer, Silent, Auto](CAccResult &Res) {
		if(!IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
			return;
		if(!Res.m_Success || !Res.m_Found)
		{
			pExpectedPlayer->m_LoginPending = false;
			return;
		}
		if(pExpectedPlayer->Acc()->m_LoggedIn)
		{
			pExpectedPlayer->m_LoginPending = false;
			GameServer()->SendChatTarget(ClientId, "You are already logged in");
			return;
		}
		if(Res.m_LoggedIn)
		{
			pExpectedPlayer->m_LoginPending = false;
			if(!Silent)
				GameServer()->SendChatTarget(ClientId, "Account is already logged in");
			return;
		}
		if(Res.m_Disabled && Auto)
		{
			pExpectedPlayer->m_LoginPending = false;
			if(!Silent)
				GameServer()->SendChatTarget(ClientId, "Your account is disabled");
			return;
		}
		const char *pSuccessMessage = Silent ? "" : Auto ? "Automatically logged into your account" :
								   "Logged in successfully";
		OnLogin(ClientId, Res, pSuccessMessage);
	});
	DbPool()->Execute(CAccountsWorker::SelectByUsername, std::move(pReq), "acc select by username");
	return true;
}

void CAccounts::Login(int ClientId, const char *pUsername, const char *pPassword)
{
	if(!DbPool())
		return;
	if(!pUsername[0] || !pPassword[0])
		return;
	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pExpectedPlayer)
		return;
	if(pExpectedPlayer->Acc()->m_LoggedIn)
	{
		GameServer()->SendChatTarget(ClientId, "You are already logged in");
		return;
	}
	if(pExpectedPlayer->m_LoginPending)
	{
		GameServer()->SendChatTarget(ClientId, "Login already in progress");
		return;
	}
	pExpectedPlayer->m_LoginPending = true;
	char HashedPassword[ACC_MAX_PASSW_LENGTH];
	sha256_str(HashPassword(pPassword), HashedPassword, ACC_MAX_PASSW_LENGTH);
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccLoginRequest>(pRes);
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_PasswordHash, HashedPassword, sizeof(pReq->m_PasswordHash));
	AddPending(pRes, [this, ClientId, pExpectedPlayer](CAccResult &Res) {
		if(!IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
			return;
		if(pExpectedPlayer->Acc()->m_LoggedIn)
		{
			pExpectedPlayer->m_LoginPending = false;
			GameServer()->SendChatTarget(ClientId, "You are already logged in");
			return;
		}
		if(!Res.m_Success || !Res.m_Found)
		{
			pExpectedPlayer->m_LoginPending = false;
			GameServer()->SendChatTarget(ClientId, "Login failed");
			if(GameServer()->m_apPlayers[ClientId])
				GameServer()->m_apPlayers[ClientId]->m_AccLoginAttempts++;
			return;
		}
		if(Res.m_Disabled)
		{
			pExpectedPlayer->m_LoginPending = false;
			GameServer()->SendChatTarget(ClientId, "Your account is disabled");
			return;
		}
		if(Res.m_LoggedIn)
		{
			pExpectedPlayer->m_LoginPending = false;
			GameServer()->SendChatTarget(ClientId, "Account is already logged in");
			return;
		}
		OnLogin(ClientId, Res, "Login successful");
	});
	DbPool()->Execute(CAccountsWorker::Login, std::move(pReq), "acc login");
}

static bool IsAllLowercase(const char *pStr)
{
	for(int i = 0; pStr[i]; ++i)
	{
		if(pStr[i] >= 'a' && pStr[i] <= 'z')
			continue;
		if(pStr[i] >= '0' && pStr[i] <= '9')
			continue;
		if(pStr[i] == '-' || pStr[i] == '_')
			continue;
		return false;
	}
	return true;
}

bool CAccounts::Register(int ClientId, const char *pUsername, const char *pPassword)
{
	if(!DbPool())
		return false;
	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pExpectedPlayer)
		return false;
	if(!pUsername[0] || !pPassword[0])
	{
		GameServer()->SendChatTarget(ClientId, "[Err] Username or password is empty");
		return false;
	}

	if(!IsAllLowercase(pUsername))
	{
		GameServer()->SendChatTarget(ClientId, "[Err] Username must be all lowercase and only contain numbers, hyphens, and underscores");
		return false;
	}

	if(str_length(pUsername) >= ACC_MAX_USERNAME_LENGTH)
	{
		GameServer()->SendChatTarget(ClientId, "[Err] Username is too long");
		return false;
	}
	if(str_length(pUsername) < ACC_MIN_USERNAME_LENGTH)
	{
		GameServer()->SendChatTarget(ClientId, "[Err] Username is too short");
		return false;
	}
	if(str_length(pPassword) < ACC_MIN_PASSW_LENGTH)
	{
		GameServer()->SendChatTarget(ClientId, "[Err] Password is too short");
		return false;
	}
	if(str_length(pPassword) > ACC_MAX_PASSW_LENGTH)
	{
		GameServer()->SendChatTarget(ClientId, "[Err] Password is too long");
		return false;
	}
	char HashedPassword[ACC_MAX_PASSW_LENGTH];
	sha256_str(HashPassword(pPassword), HashedPassword, ACC_MAX_PASSW_LENGTH);
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccRegisterRequest>(pRes);
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_PasswordHash, HashedPassword, sizeof(pReq->m_PasswordHash));
	str_copy(pReq->m_RegisterIP, Server()->ClientAddrString(ClientId, false), sizeof(pReq->m_RegisterIP));
	pReq->m_MaxAccountsPerIP = g_Config.m_SvAccountsMaxRegister;
	time_t Now = time(0);
	pReq->m_RegisterDate = Now;
	AddPending(pRes, [this, ClientId, pExpectedPlayer](CAccResult &Res) {
		if(!IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
			return;
		if(Res.m_RegisterStatus == CAccResult::ERegisterStatus::IP_LIMIT_REACHED)
			GameServer()->SendChatTarget(ClientId, "[Err] This IP address has already registered the maximum number of accounts");
		else if(Res.m_RegisterStatus != CAccResult::ERegisterStatus::SUCCESS)
			GameServer()->SendChatTarget(ClientId, "[Err] Username is already taken");
		else
		{
			GameServer()->SendChatTarget(ClientId, "Registered Successfully, use /login..");
			if(GameServer()->m_apPlayers[ClientId])
				GameServer()->m_apPlayers[ClientId]->m_AccRegisters++;
		}
	});
	DbPool()->ExecuteWrite(CAccountsWorker::Register, std::move(pReq), "acc register");
	return true;
}

void CAccounts::OnLogin(int ClientId, CAccResult &Res, const char *pSuccessMessage)
{
	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];

	bool NeedsOverride = Acc.m_Configs.m_SentFastInput;
	bool FastInput = Acc.m_Configs.m_FastInputs;
	int FastInputAmount = Acc.m_Configs.m_FastInputAmount;
	time_t Now = time(0);
	const char *pPlayerName = Server()->ClientIngame(ClientId) ? Server()->ClientName(ClientId) : Res.m_PlayerName;
	const std::string UsernameCopy = Res.m_aUsername;
	const std::string LastNameCopy = Res.m_LastPlayerName;
	const std::string CurrentIpCopy = Server()->ClientAddrString(ClientId, false);
	const std::string LastIpCopy = Res.m_LastIP;
	const int64_t RegisterDate = Res.m_RegisterDate;
	const int64_t Playtime = Res.m_Playtime;
	const int64_t Deaths = Res.m_Deaths;
	const int64_t Kills = Res.m_Kills;
	const int64_t Level = Res.m_Level;
	const int64_t Xp = Res.m_XP;
	const int64_t Money = Res.m_Money;
	const CInventory Inventory = Res.m_Inventory;
	const CMailBox MailBox = Res.m_MailBox;
	const CAccConfigs Configs = Res.m_Configs;
	const std::string SuccessMessage = pSuccessMessage ? pSuccessMessage : "";

	auto pUpdRes = std::make_shared<CAccResult>();
	auto pUpd = std::make_unique<CAccUpdLoginState>();
	pUpd->m_pResult = pUpdRes;
	str_copy(pUpd->m_aUsername, UsernameCopy.c_str(), sizeof(pUpd->m_aUsername));
	str_copy(pUpd->m_PlayerName, pPlayerName, sizeof(pUpd->m_PlayerName));
	str_copy(pUpd->m_CurrentIP, CurrentIpCopy.c_str(), sizeof(pUpd->m_CurrentIP));
	str_copy(pUpd->m_aInstance, g_Config.m_SvAccountsInstance, sizeof(pUpd->m_aInstance));
	pUpd->m_LastLogin = Now;
	pUpd->m_Port = Server()->Port();
	pUpd->m_ClientId = ClientId;
	DbPool()->ExecuteWrite(CAccountsWorker::UpdateLoginState, std::move(pUpd), "acc update login");
	AddPending(pUpdRes, [this, ClientId, pExpectedPlayer, NeedsOverride, FastInput, FastInputAmount, Now,
				    Username = UsernameCopy, LastName = LastNameCopy, CurrentIp = CurrentIpCopy, LastIp = LastIpCopy,
				    RegisterDate, Playtime, Deaths, Kills, Level, Xp, Money, Inventory, MailBox, Configs,
				    PlayerName = std::string(pPlayerName), SuccessMessage](CAccResult &UpdRes) {
		if(!UpdRes.m_Success || !UpdRes.m_LoginStateUpdated)
		{
			if(IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
			{
				pExpectedPlayer->m_LoginPending = false;
				GameServer()->SendChatTarget(ClientId, UpdRes.m_Success ? "Account is already logged in" : "Login failed");
			}
			return;
		}

		if(!pExpectedPlayer || !IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
		{
			struct CSqlClearLoginState : ISqlData
			{
				CSqlClearLoginState() :
					ISqlData(nullptr) {}
				char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";
				char m_aInstance[16] = "";
				int m_Port = 0;
				int m_ClientId = -1;
			};
			auto Fn = [](IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize) -> bool {
				const auto *p = dynamic_cast<const CSqlClearLoginState *>(pData);
				char aSql[512];
				str_copy(aSql,
					"UPDATE foxnet_accounts "
					"SET LoggedIn = 0, Port = 0, ClientId = -1, ServerInstance = '' "
					"WHERE Username = ? AND LoggedIn = 1 AND Port = ? AND ClientId = ? AND ServerInstance = ?",
					sizeof(aSql));
				if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
					return false;
				pSql->BindString(1, p->m_aUsername);
				pSql->BindInt(2, p->m_Port);
				pSql->BindInt(3, p->m_ClientId);
				pSql->BindString(4, p->m_aInstance);
				int NumUpdated = 0;
				return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
			};
			auto pClear = std::make_unique<CSqlClearLoginState>();
			str_copy(pClear->m_aUsername, Username.c_str(), sizeof(pClear->m_aUsername));
			str_copy(pClear->m_aInstance, g_Config.m_SvAccountsInstance, sizeof(pClear->m_aInstance));
			pClear->m_Port = Server()->Port();
			pClear->m_ClientId = ClientId;
			DbPool()->ExecuteWrite(Fn, std::move(pClear), "acc clear orphaned login");
			return;
		}

		CAccountSession &Session = GameServer()->m_aAccounts[ClientId];
		str_copy(Session.m_aUsername, Username.c_str());
		Session.m_RegisterDate = RegisterDate;
		str_copy(Session.m_aName, PlayerName.c_str());
		str_copy(Session.m_aLastName, LastName.c_str());
		str_copy(Session.m_aCurrentIp, CurrentIp.c_str());
		str_copy(Session.m_aLastIp, LastIp.c_str());
		Session.m_LoggedIn = true;
		Session.m_LastLogin = Now;
		Session.m_Port = Server()->Port();
		Session.m_ClientId = ClientId;
		Session.m_Playtime = Playtime;
		Session.m_Deaths = Deaths;
		Session.m_Kills = Kills;
		Session.m_Level = Level;
		Session.m_XP = Xp;
		Session.m_Money = Money;
		Session.m_LoginTick = Server()->Tick();
		Session.m_Inventory = Inventory;

		Session.m_Configs = Configs;
		if(NeedsOverride)
		{
			Session.m_Configs.m_FastInputs = FastInput;
			Session.m_Configs.m_FastInputAmount = FastInputAmount;
		}

		Session.m_MailBox = MailBox;
		// The login query may use a lagging read replica. Force an authoritative
		// mailbox refresh from the primary database on the next tick.
		Session.m_LastMailboxFetch = 0;
		Session.m_MailboxFetchPending = false;
		Session.m_MoneyInFlight = 0;
		Session.m_LastMoneyFetch = 0;
		Session.m_MoneyFetchPending = false;
		pExpectedPlayer->m_LoginPending = false;
		if(!SuccessMessage.empty())
			GameServer()->SendChatTarget(ClientId, SuccessMessage.c_str());
		GameServer()->OnLogin(ClientId);

		// Apply equipped items to player cosmetics
		if(auto *pPlayer = GameServer()->m_apPlayers[ClientId])
		{
			for(const auto &Item : pPlayer->Inv()->m_Map)
			{
				CInventoryEntry Entry = Item.second;
				const CItemConfig *pCfg = GameServer()->m_Shop.FindItem(Item.first.c_str());
				if(!pCfg)
					continue;
				const int Val = Entry.m_Value;
				if(Val <= 0)
					continue;

				pPlayer->UseItem(Item.first.c_str(), Val, true);
			}
		}

		ClaimDailyReward(ClientId);
	});
}

bool CAccounts::Logout(int ClientId, bool WaitForCompletion)
{
	if(GameServer()->m_aAccounts[ClientId].m_LoggedIn)
	{
		// Settle first. OnLogout() copies m_Money straight into the write request, so a refund or
		// payout made after it lands in an account that is never saved and is wiped two lines down.
		if(CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId])
			GameServer()->m_ZoneManager.OnClientReset(ClientId, pPlayer->MultiMapIdx());

		OnLogout(ClientId, GameServer()->m_aAccounts[ClientId], WaitForCompletion);
		GameServer()->OnLogout(ClientId);
		GameServer()->m_aAccounts[ClientId] = CAccountSession();
		return true;
	}
	return false;
}

void CAccounts::OnLogout(int ClientId, CAccountSession &AccInfo, bool WaitForCompletion)
{
	if(!DbPool())
		return;
	std::shared_ptr<CAccResult> pRes;
	if(WaitForCompletion)
		pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccSaveInfo>();
	pReq->m_pResult = pRes;
	str_copy(pReq->m_aUsername, AccInfo.m_aUsername, sizeof(pReq->m_aUsername));
	pReq->m_Playtime = AccInfo.m_Playtime;
	pReq->m_Deaths = AccInfo.m_Deaths;
	pReq->m_Kills = AccInfo.m_Kills;
	pReq->m_Level = AccInfo.m_Level;
	pReq->m_XP = AccInfo.m_XP;
	pReq->m_Inventory = AccInfo.m_Inventory;
	pReq->m_Configs = AccInfo.m_Configs;
	DbPool()->ExecuteWrite(CAccountsWorker::UpdateLogoutState, std::move(pReq), "acc update logout");
	if(WaitForCompletion)
		WaitForSqlCompletion(pRes, "acc update logout");
}

void CAccounts::LogoutAllAccountsPort(int Port, const char *pInstance, bool WaitForCompletion)
{
	if(!DbPool())
		return;
	struct CSqlLogoutByPort : ISqlData
	{
		CSqlLogoutByPort() :
			ISqlData(nullptr) {}
		int m_Port;
		char m_aInstance[64] = "";
	};
	auto Fn = [](IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize) -> bool {
		const auto *p = dynamic_cast<const CSqlLogoutByPort *>(pData);
		char aSql[256];
		str_copy(aSql,
			"UPDATE foxnet_accounts SET LastPlayerName = PlayerName, LastIP = CurrentIP, LoggedIn = 0, Port = 0, ClientId = -1, ServerInstance = '' WHERE Port = ? AND ServerInstance = ?",
			sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt(1, p->m_Port);
		pSql->BindString(2, p->m_aInstance);
		int NumUpdated = 0;
		return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
	};
	std::shared_ptr<CAccResult> pRes;
	if(WaitForCompletion)
		pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CSqlLogoutByPort>();
	pReq->m_pResult = pRes;
	pReq->m_Port = Port;
	if(pInstance)
		str_copy(pReq->m_aInstance, pInstance, sizeof(pReq->m_aInstance));
	DbPool()->ExecuteWrite(Fn, std::move(pReq), "acc bulk logout by port");
	if(WaitForCompletion)
		WaitForSqlCompletion(pRes, "acc bulk logout by port");
}

bool CAccounts::ChangePassword(int ClientId, const char *pOldPassword, const char *pNewPassword)
{
	if(!DbPool())
		return false;
	if(!GameServer()->m_aAccounts[ClientId].m_LoggedIn)
	{
		GameServer()->SendChatTarget(ClientId, "[Err] You are not logged in");
		return false;
	}
	if(!pOldPassword[0] || !pNewPassword[0])
	{
		GameServer()->SendChatTarget(ClientId, "[Err] Password is empty");
		return false;
	}
	if(!str_comp(pOldPassword, pNewPassword))
	{
		GameServer()->SendChatTarget(ClientId, "[Err] New password must be different from old password");
		return false;
	}
	size_t NewLength = str_length(pNewPassword);
	if(NewLength < ACC_MIN_PASSW_LENGTH)
	{
		GameServer()->SendChatTarget(ClientId, "[Err] New password is too short");
		return false;
	}
	else if(NewLength >= ACC_MAX_PASSW_LENGTH)
	{
		GameServer()->SendChatTarget(ClientId, "[Err] New password is too long");
		return false;
	}
	char HashedOld[ACC_MAX_PASSW_LENGTH];
	char HashedNew[ACC_MAX_PASSW_LENGTH];
	sha256_str(HashPassword(pOldPassword), HashedOld, ACC_MAX_PASSW_LENGTH);
	sha256_str(HashPassword(pNewPassword), HashedNew, ACC_MAX_PASSW_LENGTH);

	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccChangePassword>(pRes);
	str_copy(pReq->m_aUsername, GameServer()->m_aAccounts[ClientId].m_aUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_OldHash, HashedOld, sizeof(pReq->m_OldHash));
	str_copy(pReq->m_NewHash, HashedNew, sizeof(pReq->m_NewHash));

	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pExpectedPlayer)
		return false;

	AddPending(pRes, [this, ClientId, pExpectedPlayer](CAccResult &Res) {
		if(!IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
			return;
		if(Res.m_NumMessages.load(std::memory_order_acquire) == 0)
		{
			GameServer()->SendChatTarget(ClientId, "[Err] Password change result unknown");
			return;
		}
		int NumMessages = Res.m_NumMessages.load(std::memory_order_acquire);
		for(int i = 0; i < NumMessages; i++)
			GameServer()->SendChatTarget(ClientId, Res.m_aaMessages[i]);
	});

	DbPool()->ExecuteWrite(CAccountsWorker::ChangePassword, std::move(pReq), "acc change password");
	return true;
}

void CAccounts::SetPassword(const char *pUsername, const char *pNewPassword)
{
	if(!DbPool())
		return;
	if(!pUsername[0] || !pNewPassword[0])
		return;
	char HashedPassword[ACC_MAX_PASSW_LENGTH];
	sha256_str(HashPassword(pNewPassword), HashedPassword, ACC_MAX_PASSW_LENGTH);
	auto pReq = std::make_unique<CAccSetPassword>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_aNewPasswordHash, HashedPassword, sizeof(pReq->m_aNewPasswordHash));
	DbPool()->ExecuteWrite(CAccountsWorker::SetPassword, std::move(pReq), "acc change password (admin)");
}

void CAccounts::ShowAccProfile(int ClientId, const char *pName)
{
	if(!pName[0])
		return;
	auto SendProfile = [this, ClientId, NameCopy = std::string(pName)](const CAccResult &Data) {
		char aBuf[128];
		GameServer()->SendChatTarget(ClientId, ConvertToSmallCaps("╭──────     Profile"));
		const char *UseName = Data.m_LoggedIn ? NameCopy.c_str() : (Data.m_PlayerName[0] ? Data.m_PlayerName : Data.m_aUsername);
		str_format(aBuf, sizeof(aBuf), "│ Name: %s", UseName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		if(!Server()->ClientSlotEmpty(ClientId) && Server()->GetAuthedState(ClientId) >= AUTHED_MOD)
		{
			str_format(aBuf, sizeof(aBuf), "│ Username: %s", Data.m_aUsername);
			GameServer()->SendChatTarget(ClientId, aBuf);
		}
		if(Data.m_Disabled)
			str_copy(aBuf, "│ Status: Account disabled", sizeof(aBuf));
		else
			str_format(aBuf, sizeof(aBuf), "│ Status: %s", Data.m_LoggedIn ? "Online" : "Offline");
		GameServer()->SendChatTarget(ClientId, aBuf);
		if(!Data.m_LoggedIn)
		{
			time_t Now = time(0);
			double Seconds = difftime(Now, Data.m_LastLogin);
			int Days = (int)(Seconds / (60 * 60 * 24));
			int Hours = (int)(Seconds / (60 * 60));
			if(Data.m_LastLogin <= 0)
				str_copy(aBuf, "│ Has never been seen", sizeof(aBuf));
			else if(Days > 0)
				str_format(aBuf, sizeof(aBuf), "│ Last seen %d day%s ago", Days, Days == 1 ? "" : "s");
			else if(Hours > 0)
				str_format(aBuf, sizeof(aBuf), "│ Last seen %d hour%s ago", Hours, Hours == 1 ? "" : "s");
			else
				str_copy(aBuf, "│ Last seen less than an hour ago", sizeof(aBuf));
			GameServer()->SendChatTarget(ClientId, aBuf);
		}
		GameServer()->SendChatTarget(ClientId, ConvertToSmallCaps("├──────     Stats"));
		str_format(aBuf, sizeof(aBuf), "│ Level %" PRId64, Data.m_Level);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ %" PRId64 "%s", Data.m_Money, g_Config.m_SvCurrencyName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ %s Playtime", FormatPlaytime(Data.m_Playtime));
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ %" PRId64 " Deaths", Data.m_Deaths);
		GameServer()->SendChatTarget(ClientId, aBuf);
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
	};

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CAccountSession &Account = GameServer()->m_aAccounts[i];
		if(!Account.m_LoggedIn)
			continue;
		if(str_comp_nocase(Server()->ClientName(i), pName) != 0 &&
			str_comp_nocase(Account.m_aLastName, pName) != 0 &&
			str_comp_nocase(Account.m_aUsername, pName) != 0)
			continue;

		CAccResult CachedData;
		str_copy(CachedData.m_aUsername, Account.m_aUsername);
		str_copy(CachedData.m_PlayerName, Server()->ClientName(i));
		str_copy(CachedData.m_LastPlayerName, Account.m_aLastName);
		CachedData.m_LoggedIn = true;
		CachedData.m_LastLogin = Account.m_LastLogin;
		CachedData.m_Playtime = Account.m_Playtime;
		CachedData.m_Deaths = Account.m_Deaths;
		CachedData.m_Kills = Account.m_Kills;
		CachedData.m_Level = Account.m_Level;
		CachedData.m_XP = Account.m_XP;
		CachedData.m_Money = Account.m_Money;
		CachedData.m_Disabled = Account.m_Disabled;
		SendProfile(CachedData);
		return;
	}

	if(!DbPool())
		return;
	auto QueryByUsername = [this, ClientId, pNameStr = std::string(pName), SendProfile]() {
		auto pRes2 = std::make_shared<CAccResult>();
		auto pReq2 = std::make_unique<CAccSelectByUser>(pRes2);
		str_copy(pReq2->m_aUsername, pNameStr.c_str(), sizeof(pReq2->m_aUsername));
		AddPending(pRes2, [this, ClientId, pNameStr, SendProfile](CAccResult &Res2) {
			if(!Res2.m_Success || !Res2.m_Found)
			{
				GameServer()->SendChatTarget(ClientId, ConvertToSmallCaps("╭─────────       Profile"));
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "│ Account \"%s\" doesn't exist", pNameStr.c_str());
				GameServer()->SendChatTarget(ClientId, aBuf);
				GameServer()->SendChatTarget(ClientId, "╰──────────────────────────");
				return;
			}
			SendProfile(Res2);
		});
		DbPool()->Execute(CAccountsWorker::SelectByUsername, std::move(pReq2), "acc select by username (profile)");
	};
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccSelectByLastName>(pRes);
	str_copy(pReq->m_LastPlayerName, pName, sizeof(pReq->m_LastPlayerName));
	AddPending(pRes, [QueryByUsername, SendProfile](CAccResult &Res) {
		if(!Res.m_Success || !Res.m_Found)
			QueryByUsername();
		else
			SendProfile(Res);
	});
	DbPool()->Execute(CAccountsWorker::SelectByLastPlayerName, std::move(pReq), "acc select by last name (profile)");
}

void CAccounts::SaveAccountsInfo(int ClientId, CAccountSession &AccInfo)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccSaveInfo>();
	str_copy(pReq->m_aUsername, AccInfo.m_aUsername, sizeof(pReq->m_aUsername));
	pReq->m_Playtime = AccInfo.m_Playtime;
	pReq->m_Deaths = AccInfo.m_Deaths;
	pReq->m_Kills = AccInfo.m_Kills;
	pReq->m_Level = AccInfo.m_Level;
	pReq->m_XP = AccInfo.m_XP;
	pReq->m_Inventory = AccInfo.m_Inventory;
	pReq->m_Configs = AccInfo.m_Configs;
	DbPool()->ExecuteWrite(CAccountsWorker::SaveInfo, std::move(pReq), "acc save info");
}

void CAccounts::SaveAllAccounts()
{
	if(!DbPool())
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_aAccounts[i].m_LoggedIn)
			SaveAccountsInfo(i, GameServer()->m_aAccounts[i]);
	}
}

void CAccounts::Top5(int ClientId, const char *pType, int Offset)
{
	if(!DbPool())
		return;
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccShowTop5>(pRes);
	pReq->m_ClientId = ClientId;
	str_copy(pReq->m_Type, pType, sizeof(pReq->m_Type));
	pReq->m_Offset = Offset;

	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pExpectedPlayer)
		return;

	AddPending(pRes, [this, ClientId, pExpectedPlayer](CAccResult &Res) {
		if(!IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer))
			return;
		if(Res.m_NumMessages.load(std::memory_order_acquire) == 0)
		{
			GameServer()->SendChatTarget(ClientId, "[Err] unknown error occurred");
			return;
		}
		int NumMessages = Res.m_NumMessages.load(std::memory_order_acquire);
		for(int i = 0; i < NumMessages; i++)
			GameServer()->SendChatTarget(ClientId, Res.m_aaMessages[i]);
	});

	DbPool()->ExecuteWrite(CAccountsWorker::ShowTop5, std::move(pReq), "show acc top5");
}

void CAccounts::SetPlayerName(int ClientId, const char *pName)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccSetNameReq>();
	str_copy(pReq->m_NewPlayerName, pName, sizeof(pReq->m_NewPlayerName));
	str_copy(pReq->m_aUsername, GameServer()->m_aAccounts[ClientId].m_aUsername, sizeof(pReq->m_aUsername));
	DbPool()->ExecuteWrite(CAccountsWorker::SetPlayerName, std::move(pReq), "acc set player name");
}

void CAccounts::DisableAccount(const char *pUsername, bool Disable)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccDisable>();
	pReq->m_Disable = Disable;
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	DbPool()->ExecuteWrite(CAccountsWorker::DisableAccount, std::move(pReq), "acc (un)disable");
}

void CAccounts::DeleteAccount(int ClientId, const char *pUsername)
{
	if(!DbPool() || !pUsername || !pUsername[0])
		return;

	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccDelete>(pRes);
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));

	AddPending(pRes, [this, ClientId, Username = std::string(pUsername)](CAccResult &Res) {
		if(Res.m_Success && Res.m_Found)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				auto &Acc = GameServer()->m_aAccounts[i];
				if(Acc.m_LoggedIn && str_comp(Acc.m_aUsername, Username.c_str()) == 0)
					Logout(i);
			}
		}

		const char *pMsg = Res.m_NumMessages.load(std::memory_order_acquire) > 0 ? Res.m_aaMessages[0] : "[Err] Failed to delete account";
		if(ClientId >= 0)
		{
			if(!Server()->ClientSlotEmpty(ClientId))
				GameServer()->SendChatTarget(ClientId, pMsg);
		}
		else
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "accounts", pMsg);
	});

	DbPool()->ExecuteWrite(CAccountsWorker::DeleteAccount, std::move(pReq), "acc delete");
}

void CAccounts::RemoveItem(const char *pUsername, const char *pItemName)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccRemoveItem>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_aItemName, pItemName, sizeof(pReq->m_aItemName));
	DbPool()->ExecuteWrite(CAccountsWorker::RemoveItem, std::move(pReq), "acc remove item");
}

int CAccounts::NeededXP(int Level)
{
	const int XP = 15 + round_to_int(Level * 1.5f);
	return std::min(XP, 750);
}

void CAccounts::MarkAllMailsRead(const char *pUsername)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccMarkAllMailsRead>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	DbPool()->ExecuteWrite(CAccountsWorker::MarkAllMailsRead, std::move(pReq), "acc mark all mails read");
}

void CAccounts::DeleteAllReadMails(const char *pUsername)
{
	if(!DbPool() || !pUsername || !pUsername[0])
		return;

	auto pReq = std::make_unique<CAccDeleteAllRead>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	DbPool()->ExecuteWrite(CAccountsWorker::DeleteAllReadMails, std::move(pReq), "acc delete all read mails");
}

void CAccounts::SetMailRead(const char *pUsername, int64_t MailId, bool Read)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccSetMailRead>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	pReq->m_MailId = MailId;
	pReq->m_Read = Read;
	DbPool()->ExecuteWrite(CAccountsWorker::SetMailRead, std::move(pReq), "acc set mail read");
}

void CAccounts::SetMailUsedCmd(const char *pUsername, int64_t MailId, bool Used)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccSetMailUsedCmd>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	pReq->m_MailId = MailId;
	pReq->m_UsedCmd = Used;
	DbPool()->ExecuteWrite(CAccountsWorker::SetMailUsedCmd, std::move(pReq), "acc set mail used cmd");
}

void CAccounts::ClaimMailReward(int ClientId, int64_t MailId, std::function<void(bool DbSuccess, bool Claimed)> &&Cb)
{
	if(!DbPool() || !CheckClientId(ClientId) || Server()->ClientSlotEmpty(ClientId))
	{
		if(Cb)
			Cb(false, false);
		return;
	}

	const CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pExpectedPlayer || !Acc.m_LoggedIn || !Acc.m_aUsername[0])
	{
		if(Cb)
			Cb(false, false);
		return;
	}

	const std::string Username = Acc.m_aUsername;
	auto pRes = std::make_shared<CAccClaimMailResult>();
	auto pReq = std::make_unique<CAccClaimMailReward>(pRes);
	str_copy(pReq->m_aUsername, Username.c_str(), sizeof(pReq->m_aUsername));
	pReq->m_MailId = MailId;

	AddPending(pRes, [this, ClientId, MailId, pExpectedPlayer, Username, Callback = std::move(Cb)](CAccResult &BaseRes) mutable {
		auto *pClaimRes = dynamic_cast<CAccClaimMailResult *>(&BaseRes);
		const bool DbSuccess = BaseRes.m_Success && pClaimRes && pClaimRes->m_PrimarySucceeded;
		const bool Claimed = DbSuccess && pClaimRes->m_Claimed;
		const bool SessionStillActive = IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer) &&
						GameServer()->m_aAccounts[ClientId].m_LoggedIn &&
						str_comp(GameServer()->m_aAccounts[ClientId].m_aUsername, Username.c_str()) == 0;

		if(Claimed && !SessionStillActive)
		{
			// The reward was reserved for a session that disappeared before the
			// database acknowledged it. Restore the previous unclaimed state.
			SetMailUsedCmd(Username.c_str(), MailId, false);
			return;
		}

		if(SessionStillActive && Callback)
			Callback(DbSuccess, Claimed);
	});

	DbPool()->ExecuteWrite(CAccountsWorker::ClaimMailReward, std::move(pReq), "acc claim mail reward");
}

void CAccounts::DeleteMail(const char *pUsername, int64_t MailId)
{
	if(!DbPool())
		return;
	auto pReq = std::make_unique<CAccDeleteMail>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	pReq->m_MailId = MailId;
	DbPool()->ExecuteWrite(CAccountsWorker::DeleteMail, std::move(pReq), "acc delete mail");
}

void CAccounts::NewMail(int ClientId, const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd)
{
	if(!CheckClientId(ClientId))
		return;
	if(GameServer()->Server()->ClientSlotEmpty(ClientId))
		return;
	if(!GameServer()->m_aAccounts[ClientId].m_LoggedIn)
		return;
	const char *pUsername = GameServer()->m_aAccounts[ClientId].m_aUsername;
	if(!pUsername[0])
		return;
	NewMail(pUsername, pSubject, pMessage, pCmdName, pCmd);
}

void CAccounts::NewMail(const char *pUsername, const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd)
{
	if(!DbPool())
		return;

	auto pRes = std::make_shared<CAccMailAcknowledge>();
	auto pReq = std::make_unique<CAccNewMail>(pRes);

	if(str_length(pSubject) > MAX_VOTE_LENGTH - str_length("──────"))
	{
		log_info("mail", "subject too long");
		return;
	}

	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_aSubject, pSubject, sizeof(pReq->m_aSubject));
	str_copy(pReq->m_aMessage, pMessage, sizeof(pReq->m_aMessage));
	str_copy(pReq->m_aCmdName, pCmdName, sizeof(pReq->m_aCmdName));
	str_copy(pReq->m_aCmd, pCmd, sizeof(pReq->m_aCmd));
	pReq->m_UsedCmd = pCmdName[0] == '\0' && pCmd[0] == '\0';
	pReq->m_Unread = true;

	AddPending(pRes, [this](CAccResult &AckBase) {
		if(!AckBase.m_Success)
		{
			log_info("mail", "insert failed");
			return;
		}

		// Find online user on this server
		int Target = -1;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			auto &Acc = GameServer()->m_aAccounts[i];
			if(!Acc.m_LoggedIn)
				continue;
			if(!str_comp(Acc.m_aUsername, AckBase.m_aUsername))
			{
				Target = i;
				break;
			}
		}
		if(Target < 0)
			return;

		// Push the new mail into the session's mailbox if not already present
		if(!AckBase.m_MailBox.m_vMails.empty())
		{
			const auto &NewMail = AckBase.m_MailBox.m_vMails.front();

			auto &Box = GameServer()->m_aAccounts[Target].m_MailBox;
			bool Exists = false;
			for(const auto &M : Box.m_vMails)
			{
				if(M.m_MailId == NewMail.m_MailId)
				{
					Exists = true;
					break;
				}
			}
			if(!Exists)
				Box.m_vMails.push_back(NewMail);

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "You received a new mail: %s", NewMail.m_aSubject);
			GameServer()->SendChatTarget(Target, aBuf);
		}
	});

	DbPool()->ExecuteWrite(CAccountsWorker::NewMail, std::move(pReq), "acc new mail");
}

void CAccounts::NewGlobalMail(const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd, bool IncludeDisabled, bool OnlyLoggedIn, int MinLevel)
{
	if(!DbPool())
		return;

	if(str_length(pSubject) > MAX_VOTE_LENGTH - str_length("──────"))
	{
		log_info("mail", "subject too long");
		return;
	}

	auto pResBulk = std::make_shared<CBulkMailResult>();
	auto pReq = std::make_unique<CAccBulkNewMail>(pResBulk);
	str_copy(pReq->m_aSubject, pSubject, sizeof(pReq->m_aSubject));
	str_copy(pReq->m_aMessage, pMessage, sizeof(pReq->m_aMessage));
	str_copy(pReq->m_aCmdName, pCmdName, sizeof(pReq->m_aCmdName));
	str_copy(pReq->m_aCmd, pCmd, sizeof(pReq->m_aCmd));
	pReq->m_IncludeDisabled = IncludeDisabled;
	pReq->m_OnlyLoggedIn = OnlyLoggedIn;
	pReq->m_MinLevel = MinLevel;

	AddPending(std::static_pointer_cast<CAccResult>(pResBulk), [this](CAccResult &BaseRes) {
		auto *pBulk = static_cast<CBulkMailResult *>(&BaseRes);
		if(!pBulk->m_Success)
		{
			log_info("mail", "bulk mail failed");
			return;
		}
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			auto &Acc = GameServer()->m_aAccounts[i];
			if(!Acc.m_LoggedIn)
				continue;
			Acc.m_LastMailboxFetch = 0; // force refresh
			if(GameServer()->m_apPlayers[i])
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "You received a new global mail: %s", pBulk->m_aSubject);
				GameServer()->SendChatTarget(i, aBuf);
			}
		}
	});

	DbPool()->ExecuteWrite(CAccountsWorker::NewGlobalMail, std::move(pReq), "acc bulk new mail");
}

void CAccounts::FetchMailBox()
{
	if(!DbPool())
		return;

	time_t Now = time(0);

	struct CSqlMailboxResult : CAccResult
	{
		bool m_PrimaryLoaded = false;
	};

	struct CSqlLoadMailbox : ISqlData
	{
		CSqlLoadMailbox(std::shared_ptr<CSqlMailboxResult> pRes) :
			ISqlData(std::move(pRes)) {}
		char m_aUsername[ACC_MAX_USERNAME_LENGTH]{};
	};

	auto FnLoadMailbox = [](IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize) -> bool {
		const auto *p = dynamic_cast<const CSqlLoadMailbox *>(pData);
		auto *pRes = dynamic_cast<CSqlMailboxResult *>(pData->m_pResult.get());
		if(!p || !pRes)
			return false;
		if(w != Write::NORMAL)
			return true;

		pRes->m_MailBox.Clear();

		char aSql[256];
		str_copy(aSql, "SELECT MailId, Subject, Message, Command, CommandName, UsedCommand, Unread FROM foxnet_account_mailbox WHERE Username = ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindString(1, p->m_aUsername);

		bool End = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;

		while(!End)
		{
			CMailBox::CMail Mail{};
			Mail.m_MailId = pSql->GetInt64(1);
			pSql->GetString(2, Mail.m_aSubject, sizeof(Mail.m_aSubject));
			pSql->GetString(3, Mail.m_aMessage, sizeof(Mail.m_aMessage));
			pSql->GetString(4, Mail.m_aCmd, sizeof(Mail.m_aCmd));
			pSql->GetString(5, Mail.m_aCmdName, sizeof(Mail.m_aCmdName));
			Mail.m_UsedCmd = pSql->GetInt(6) != 0;
			Mail.m_Unread = pSql->GetInt(7) != 0;
			pRes->m_MailBox.m_vMails.push_back(Mail);

			if(!pSql->Step(&End, pError, ErrorSize))
				return false;
		}

		pRes->m_PrimaryLoaded = true;
		return true;
	};

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		auto &Acc = GameServer()->m_aAccounts[i];
		if(!Acc.m_LoggedIn)
			continue;
		if(Acc.m_MailboxFetchPending)
			continue;
		if(Acc.m_LastMailboxFetch != 0 && (Now - Acc.m_LastMailboxFetch) < 10 * 60)
			continue;

		Acc.m_MailboxFetchPending = true;
		CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[i];
		const std::string Username = Acc.m_aUsername;

		auto pRes = std::make_shared<CSqlMailboxResult>();
		auto pReq = std::make_unique<CSqlLoadMailbox>(pRes);
		str_copy(pReq->m_aUsername, Username.c_str(), sizeof(pReq->m_aUsername));

		AddPending(pRes, [this, i, Now, pExpectedPlayer, Username](CAccResult &Res) {
			if(!IsExpectedPlayerStillInSlot(GameServer(), i, pExpectedPlayer) ||
				!GameServer()->m_aAccounts[i].m_LoggedIn ||
				str_comp(GameServer()->m_aAccounts[i].m_aUsername, Username.c_str()) != 0)
				return;
			auto &AccRef = GameServer()->m_aAccounts[i];
			auto *pMailboxRes = dynamic_cast<CSqlMailboxResult *>(&Res);
			if(!Res.m_Success || !pMailboxRes || !pMailboxRes->m_PrimaryLoaded)
			{
				AccRef.m_MailboxFetchPending = false;
				return;
			}
			AccRef.m_MailBox = Res.m_MailBox;
			AccRef.m_LastMailboxFetch = Now;
			AccRef.m_MailboxFetchPending = false;
		});

		DbPool()->ExecuteWrite(FnLoadMailbox, std::move(pReq), "acc mailbox refresh");
	}
}

void CAccounts::ConReport(IConsole::IResult *pResult, void *pUserData)
{
	CAccounts *pSelf = (CAccounts *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;

	CPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	if(!pPlayer)
		return;

	if(!g_Config.m_DcReportsWebhookUrl[0] || !g_Config.m_SvAccounts)
	{
		pPlayer->SendChat("Reporting is not enabled on this server.");
		return;
	}

	const char *pFrom = pSelf->Server()->ClientName(ClientId);
	const char *pAgainst = pResult->GetString(0);
	const char *pReportText = pResult->GetString(1);

	if(!str_comp_nocase(pAgainst, pFrom))
	{
		pPlayer->SendChat("You can't report yourself.");
		return;
	}

	if(!pPlayer->Acc()->m_LoggedIn)
	{
		pPlayer->SendChat("You need to be logged in for this.");
		return;
	}

	if(!pPlayer->CanReport())
	{
		pPlayer->SendChat("You need to wait a bit until you can report.");
		return;
	}

	int AgainstId = pSelf->GameServer()->ClientIdByName(pAgainst);
	if(AgainstId == -1)
	{
		pPlayer->SendChat("Player not found.");
		return;
	}
	CPlayer *pAgainstPlayer = pSelf->GetPlayer(AgainstId);
	if(!pAgainstPlayer)
	{
		pPlayer->SendChat("Player not found.");
		return;
	}

	const std::string Message = "From: " + WebhookFormat::Name(pFrom) + " (" + std::to_string(ClientId) + ", acc: " + WebhookFormat::Name(pPlayer->Acc()->m_aUsername) + ") [||" +
				    WebhookFormat::EscapeText(pSelf->Server()->ClientAddrString(ClientId, false)) + "||]\n" +
				    "Against: " + WebhookFormat::Name(pSelf->Server()->ClientName(AgainstId)) + " (" + std::to_string(AgainstId) + ", acc: " +
				    WebhookFormat::Name(pAgainstPlayer->Acc()->m_LoggedIn ? pAgainstPlayer->Acc()->m_aUsername : "No Account") + ") [||" +
				    WebhookFormat::EscapeText(pSelf->Server()->ClientAddrString(AgainstId, false)) + "||]\n" +
				    WebhookFormat::CodeBlock(pReportText);
	pSelf->Server()->SendWebhookMessage(IServer::EWebhookCategory::REPORTS, g_Config.m_DcReportsWebhookUrl, Message.c_str(), "[REPORT] - Player");

	pPlayer->m_LastReport = pSelf->Server()->Tick();
	pPlayer->SendChat("Report sent!");
}

void CAccounts::OnConsoleInit()
{
	Console()->Register("force_login", "r[username] ?v[id]", CFGFLAG_SERVER, ConForceLogin, this, "Force Login player (id) into any account");
	Console()->Register("force_logout", "i[id]", CFGFLAG_SERVER, ConForceLogout, this, "Force logout an account thats currently active on the server");
	Console()->Register("acc_disable", "s[username] ?i[disable]", CFGFLAG_SERVER, ConDisable, this, "Disable an account");
	Console()->Register("acc_delete", "s[username]", CFGFLAG_SERVER, ConDelete, this, "Delete an account and all related data");
	Console()->Register("acc_password", "s[username] r[variable]", CFGFLAG_SERVER, ConForcePassword, this, "Disable an account");
	Console()->Register("register", "s[username] s[password]", CFGFLAG_CHAT, ConRegister, this, "Register a account");
	Console()->Register("password", "s[oldpass] s[password]", CFGFLAG_CHAT, ConPassword, this, "Change your password");
	Console()->Register("login", "s[username] r[password]", CFGFLAG_CHAT, ConLogin, this, "Login to your account");
	Console()->Register("logout", "", CFGFLAG_CHAT, ConLogout, this, "Logout of your account");
	Console()->Register("profile", "?r[name]", CFGFLAG_CHAT, ConProfile, this, "Show someones profile");

	Console()->Register("give_money", "v[id] l[amount]", CFGFLAG_SERVER, ConGiveMoney, this, "Give player (id) money");
	Console()->Register("set_money", "v[id] l[money]", CFGFLAG_SERVER, ConSetMoney, this, "Set player' (id) money");

	Console()->Register("give_xp", "v[id] l[amount]", CFGFLAG_SERVER, ConGiveXp, this, "Give player (id) xp");
	Console()->Register("set_xp", "v[id] l[amount]", CFGFLAG_SERVER, ConSetXp, this, "Set player' (id) xp");

	Console()->Register("give_playtime", "v[id] l[amount]", CFGFLAG_SERVER, ConGivePlaytime, this, "Give player (id) playtime");
	Console()->Register("set_playtime", "v[id] l[amount]", CFGFLAG_SERVER, ConSetPlaytime, this, "Set player' (id) playtime");

	Console()->Register("set_level", "v[id] l[amount]", CFGFLAG_SERVER, ConSetLevel, this, "Set player' (id) level");
	Console()->Register("set_deaths", "v[id] l[amount]", CFGFLAG_SERVER, ConSetDeaths, this, "Set player' (id) deaths");

	Console()->Register("pay", "s[player] l[amount]", CFGFLAG_CHAT, ConPayMoney, this, "Pay someone money");

	Console()->Register("top5money", "?i[offset]", CFGFLAG_CHAT, ConTop5Money, this, "Show someones profile");
	Console()->Register("top5level", "?i[offset]", CFGFLAG_CHAT, ConTop5Level, this, "Show someones profile");
	Console()->Register("top5playtime", "?i[offset]", CFGFLAG_CHAT, ConTop5Playtime, this, "Show someones profile");

	Console()->Register("new_mail", "s[username] s[subject] s[message] s[cmd_name] r[cmd]", CFGFLAG_SERVER, ConNewMail, this, "Send a new mail");
	Console()->Register("new_global_mail", "s[subject] s[message] s[cmd_name] s[cmd] ?i[min_level] i?[only-online] i?[include-disabled]", CFGFLAG_SERVER, ConNewGlobalMail, this, "Send a new mail");

	Console()->Register("report", "s[player] r[message]", CFGFLAG_CHAT, ConReport, this, "Report a player");
}

std::vector<CRewardItem> CAccounts::PickRandomRewardItems(const std::vector<EItemRarity> &vAllowedRarities, int Count)
{
	std::vector<CRewardItem> vRewardItems;
	if(Count <= 0)
		return vRewardItems;

	std::vector<const CItemConfig *> vItems;
	for(const auto &Item : GameServer()->m_Shop.Registry().Map())
	{
		const CItemConfig &Cfg = Item.second;
		if(std::find(vAllowedRarities.begin(), vAllowedRarities.end(), Cfg.m_Rarity) == vAllowedRarities.end())
			continue;
		if(Cfg.m_Price <= 0)
			continue;
		if(HasFlag(Cfg.m_Flags, EItemFlag::Upgrade))
			continue;
		vItems.push_back(&Cfg);
	}

	if(vItems.empty())
		return vRewardItems;

	std::uniform_int_distribution<int> Dis(0, (int)vItems.size() - 1);
	std::uniform_int_distribution<int> DaysDis(1, 3);
	for(int i = 0; i < Count; i++)
	{
		CRewardItem RewardItem;
		RewardItem.m_pItem = vItems[Dis(Rng())];
		RewardItem.m_Days = DaysDis(Rng()) * 7;
		vRewardItems.push_back(RewardItem);
	}
	return vRewardItems;
}

// Day index the streak is counted in, shifted so the reset lands on the configured UTC hour
static int64_t DailyRewardPeriod(int64_t Now)
{
	return (Now - (int64_t)g_Config.m_SvDailyRewardResetHour * 3600) / 86400;
}

void CAccounts::ClaimDailyReward(int ClientId)
{
	if(!g_Config.m_SvDailyReward || !DbPool() || !CheckClientId(ClientId))
		return;

	const CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	if(!Acc.m_LoggedIn || !Acc.m_aUsername[0])
		return;

	const int64_t Now = time(0);
	auto pRes = std::make_shared<CAccDailyRewardResult>();
	auto pReq = std::make_unique<CAccClaimDailyReward>(pRes);
	str_copy(pReq->m_aUsername, Acc.m_aUsername, sizeof(pReq->m_aUsername));
	pReq->m_Period = DailyRewardPeriod(Now);
	pReq->m_Now = Now;

	// The mail is addressed to the account, so it survives the player leaving
	// before the database answers.
	const std::string Username = Acc.m_aUsername;
	AddPending(pRes, [this, Username](CAccResult &BaseRes) {
		auto *pDailyRes = dynamic_cast<CAccDailyRewardResult *>(&BaseRes);
		if(!BaseRes.m_Success || !pDailyRes || !pDailyRes->m_PrimarySucceeded)
			return;
		if(!pDailyRes->m_Claimed)
			return;
		SendDailyRewardMail(Username.c_str(), pDailyRes->m_Streak);
	});

	DbPool()->ExecuteWrite(CAccountsWorker::ClaimDailyReward, std::move(pReq), "acc claim daily reward");
}

void CAccounts::SendDailyRewardMail(const char *pUsername, int64_t Streak)
{
	if(Streak <= 0)
		return;

	const int64_t MaxDays = g_Config.m_SvDailyRewardMaxDays;
	const int64_t Day = Streak < MaxDays ? Streak : MaxDays;
	const int64_t Money = saturating_add(g_Config.m_SvDailyRewardMoney, saturating_mul(Day - 1, g_Config.m_SvDailyRewardMoneyStep));
	const int64_t Xp = saturating_add(g_Config.m_SvDailyRewardXp, saturating_mul(Day - 1, g_Config.m_SvDailyRewardXpStep));

	// Longer streaks roll more items from deeper rarities, like the level up rewards do
	std::vector<CRewardItem> vItems;
	if(Streak % 365 == 0)
	{
		vItems = PickRandomRewardItems({EItemRarity::Common,
						       EItemRarity::Uncommon,
						       EItemRarity::Rare,
						       EItemRarity::Epic,
						       EItemRarity::Mythic,
						       EItemRarity::Legendary},
			6);
	}
	else if(Streak % 100 == 0)
	{
		vItems = PickRandomRewardItems({EItemRarity::Common,
						       EItemRarity::Uncommon,
						       EItemRarity::Rare,
						       EItemRarity::Epic,
						       EItemRarity::Mythic},
			4);
	}
	else if(Streak % 30 == 0)
	{
		vItems = PickRandomRewardItems({EItemRarity::Common, EItemRarity::Uncommon, EItemRarity::Rare, EItemRarity::Epic}, 3);
	}
	else if(Streak % 7 == 0)
	{
		vItems = PickRandomRewardItems({EItemRarity::Common, EItemRarity::Uncommon, EItemRarity::Rare}, 2);
	}

	// m_aCmd is the reward, m_aCmdName is what the mailbox lists as its contents
	char aCmd[1024] = "";
	char aCmdName[512] = "";
	char aPart[192];

	for(const CRewardItem &Item : vItems)
	{
		str_format(aPart, sizeof(aPart), "give_item_days %%d %d %s;", Item.m_Days, Item.m_pItem->m_pName);
		str_append(aCmd, aPart, sizeof(aCmd));
		str_format(aPart, sizeof(aPart), "%s for %d days\n", Item.m_pItem->m_pName, Item.m_Days);
		str_append(aCmdName, aPart, sizeof(aCmdName));
	}
	if(Money > 0)
	{
		str_format(aPart, sizeof(aPart), "give_money %%d %" PRId64 ";", Money);
		str_append(aCmd, aPart, sizeof(aCmd));
		str_format(aPart, sizeof(aPart), "%" PRId64 "%s\n", Money, g_Config.m_SvCurrencyName);
		str_append(aCmdName, aPart, sizeof(aCmdName));
	}
	if(Xp > 0)
	{
		str_format(aPart, sizeof(aPart), "give_xp %%d %" PRId64 ";", Xp);
		str_append(aCmd, aPart, sizeof(aCmd));
		str_format(aPart, sizeof(aPart), "%" PRId64 " XP\n", Xp);
		str_append(aCmdName, aPart, sizeof(aCmdName));
	}

	if(!aCmd[0])
		return; // nothing configured to hand out

	char aSubject[64];
	str_format(aSubject, sizeof(aSubject), "Daily Reward - Day %" PRId64, Streak);

	char aMessage[512];
	if(vItems.empty())
		str_format(aMessage, sizeof(aMessage), "Day %" PRId64 " of your login streak.", Streak);
	else
		str_format(aMessage, sizeof(aMessage), "Day %" PRId64 " of your login streak, with %d random Item%s!", Streak, (int)vItems.size(), vItems.size() > 1 ? "s" : "");
	str_append(aMessage, "\nCome back tomorrow to keep it going, miss a day and it starts over.", sizeof(aMessage));

	NewMail(pUsername, aSubject, aMessage, aCmdName, aCmd);
}

void CAccounts::ApplyMoneyDelta(int ClientId, int64_t Delta, bool RequireFunds, std::function<void(bool, bool, int64_t)> &&Cb, bool Absolute)
{
	if(!DbPool() || !CheckClientId(ClientId))
	{
		if(Cb)
			Cb(false, false, 0);
		return;
	}

	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];
	CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[ClientId];
	if(!Acc.m_LoggedIn || !Acc.m_aUsername[0])
	{
		if(Cb)
			Cb(false, false, 0);
		return;
	}

	const std::string Username = Acc.m_aUsername;
	auto pRes = std::make_shared<CAccMoneyDeltaResult>();
	auto pReq = std::make_unique<CAccMoneyDelta>(pRes);
	str_copy(pReq->m_aUsername, Username.c_str(), sizeof(pReq->m_aUsername));
	pReq->m_Delta = Delta;
	pReq->m_RequireFunds = RequireFunds;
	pReq->m_Absolute = Absolute;

	Acc.m_MoneyInFlight++;

	AddPending(pRes, [this, ClientId, pExpectedPlayer, Username, Delta, RequireFunds, Callback = std::move(Cb)](CAccResult &BaseRes) mutable {
		auto *pDeltaRes = dynamic_cast<CAccMoneyDeltaResult *>(&BaseRes);
		const bool DbSuccess = BaseRes.m_Success && pDeltaRes && pDeltaRes->m_PrimarySucceeded;
		const bool Applied = DbSuccess && pDeltaRes->m_Applied;
		const int64_t NewMoney = DbSuccess ? pDeltaRes->m_Money : 0;

		const bool SameSession = IsExpectedPlayerStillInSlot(GameServer(), ClientId, pExpectedPlayer) &&
					 GameServer()->m_aAccounts[ClientId].m_LoggedIn &&
					 str_comp(GameServer()->m_aAccounts[ClientId].m_aUsername, Username.c_str()) == 0;
		if(SameSession)
		{
			CAccountSession &Session = GameServer()->m_aAccounts[ClientId];
			if(Session.m_MoneyInFlight > 0)
				Session.m_MoneyInFlight--;
			// The database is the owner, so take its answer over the guess made
			// when the delta was sent.
			if(DbSuccess)
				Session.m_Money = NewMoney;
		}
		else if(RequireFunds && Applied)
		{
			// The spend was reserved for a session that has gone, so whatever it
			// paid for can no longer be handed over. Put the money back.
			log_info("account", "refunding %" PRId64 " to '%s', the session left before the spend completed", -Delta, Username.c_str());
			MoneyDeltaByUsername(Username.c_str(), -Delta);
		}

		if(!DbSuccess)
			log_error("account", "money delta for '%s' failed, balance left to the database", Username.c_str());

		// Only the session that asked for this can act on the result
		if(SameSession && Callback)
			Callback(DbSuccess, Applied, NewMoney);
	});

	DbPool()->ExecuteWrite(CAccountsWorker::MoneyDelta, std::move(pReq), "acc money delta");
}

void CAccounts::MoneyDeltaByUsername(const char *pUsername, int64_t Delta)
{
	if(!DbPool() || !pUsername[0] || Delta == 0)
		return;
	auto pReq = std::make_unique<CAccMoneyDelta>(std::make_shared<CAccMoneyDeltaResult>());
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	pReq->m_Delta = Delta;
	DbPool()->ExecuteWrite(CAccountsWorker::MoneyDelta, std::move(pReq), "acc money delta (no session)");
}

void CAccounts::SetMoney(int ClientId, int64_t Money)
{
	if(!CheckClientId(ClientId))
		return;
	// Shown right away, then corrected from the database like any other change
	GameServer()->m_aAccounts[ClientId].m_Money = Money;
	ApplyMoneyDelta(ClientId, Money, false, nullptr, /* Absolute */ true);
}

void CAccounts::RefreshMoney()
{
	if(!DbPool())
		return;

	const int64_t Now = time(0);

	struct CMoneyFetchResult : CAccResult
	{
		bool m_PrimaryLoaded = false;
	};

	struct CMoneyFetch : ISqlData
	{
		CMoneyFetch(std::shared_ptr<CMoneyFetchResult> pRes) :
			ISqlData(std::move(pRes)) {}
		char m_aUsername[ACC_MAX_USERNAME_LENGTH]{};
	};

	// Routed through ExecuteWrite so it reads the write database. A read
	// connection may be a replica that has not caught up with our own deltas.
	auto FnFetch = [](IDbConnection *pSql, const ISqlData *pData, Write w, char *pError, int ErrorSize) -> bool {
		const auto *p = dynamic_cast<const CMoneyFetch *>(pData);
		auto *pRes = dynamic_cast<CMoneyFetchResult *>(pData->m_pResult.get());
		if(!p || !pRes)
			return false;
		if(w != Write::NORMAL)
			return true;

		char aSql[128];
		str_copy(aSql, "SELECT Money FROM foxnet_accounts WHERE Username = ?", sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindString(1, p->m_aUsername);

		bool End = true;
		if(!pSql->Step(&End, pError, ErrorSize))
			return false;
		if(End)
			return true;
		pRes->m_Money = pSql->GetInt64(1);
		pRes->m_PrimaryLoaded = true;
		str_copy(pRes->m_aUsername, p->m_aUsername, sizeof(pRes->m_aUsername));
		while(!End)
		{
			if(!pSql->Step(&End, pError, ErrorSize))
				return false;
		}
		return true;
	};

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CAccountSession &Acc = GameServer()->m_aAccounts[i];
		if(!Acc.m_LoggedIn || !Acc.m_aUsername[0])
			continue;
		if(Acc.m_MoneyFetchPending)
			continue;
		// A delta is still unacknowledged, its result will set the balance
		if(Acc.m_MoneyInFlight > 0)
			continue;
		if(Acc.m_LastMoneyFetch != 0 && (Now - Acc.m_LastMoneyFetch) < 60)
			continue;

		CPlayer *pExpectedPlayer = GameServer()->m_apPlayers[i];
		const std::string Username = Acc.m_aUsername;
		auto pRes = std::make_shared<CMoneyFetchResult>();
		auto pReq = std::make_unique<CMoneyFetch>(pRes);
		str_copy(pReq->m_aUsername, Username.c_str(), sizeof(pReq->m_aUsername));

		// Throttle from the attempt, not the outcome, so a failing fetch does
		// not retry every tick.
		Acc.m_MoneyFetchPending = true;
		Acc.m_LastMoneyFetch = Now;
		AddPending(pRes, [this, i, pExpectedPlayer, Username](CAccResult &BaseRes) {
			const bool SameSession = IsExpectedPlayerStillInSlot(GameServer(), i, pExpectedPlayer) &&
						 GameServer()->m_aAccounts[i].m_LoggedIn &&
						 str_comp(GameServer()->m_aAccounts[i].m_aUsername, Username.c_str()) == 0;
			if(!SameSession)
				return;

			CAccountSession &Session = GameServer()->m_aAccounts[i];
			Session.m_MoneyFetchPending = false;
			auto *pFetch = dynamic_cast<CMoneyFetchResult *>(&BaseRes);
			if(!BaseRes.m_Success || !pFetch || !pFetch->m_PrimaryLoaded)
				return;
			// Deltas sent while this was in flight own the balance instead
			if(Session.m_MoneyInFlight > 0)
				return;
			Session.m_Money = pFetch->m_Money;
		});

		DbPool()->ExecuteWrite(FnFetch, std::move(pReq), "acc fetch money");
		break; // one account per tick, like the mailbox fetch
	}
}

void CAccounts::PayMoney(int PayerClientId, int ReceiverClientId, int64_t Amount)
{
	if(!DbPool() || !CheckClientId(PayerClientId) || !CheckClientId(ReceiverClientId))
		return;
	if(Amount <= 0 || PayerClientId == ReceiverClientId)
		return;

	CAccountSession &Payer = GameServer()->m_aAccounts[PayerClientId];
	CAccountSession &Receiver = GameServer()->m_aAccounts[ReceiverClientId];
	if(!Payer.m_LoggedIn || !Payer.m_aUsername[0] || !Receiver.m_LoggedIn || !Receiver.m_aUsername[0])
		return;
	if(!str_comp(Payer.m_aUsername, Receiver.m_aUsername))
		return; // same account on two slots, nothing to transfer

	CPlayer *pExpectedPayer = GameServer()->m_apPlayers[PayerClientId];
	CPlayer *pExpectedReceiver = GameServer()->m_apPlayers[ReceiverClientId];
	const std::string PayerName = Payer.m_aUsername;
	const std::string ReceiverName = Receiver.m_aUsername;

	auto pRes = std::make_shared<CAccPayResult>();
	auto pReq = std::make_unique<CAccPay>(pRes);
	str_copy(pReq->m_aPayer, PayerName.c_str(), sizeof(pReq->m_aPayer));
	str_copy(pReq->m_aReceiver, ReceiverName.c_str(), sizeof(pReq->m_aReceiver));
	pReq->m_Amount = Amount;
	pReq->m_Now = time(0);
	pReq->m_Cooldown = g_Config.m_SvPayCooldown;

	// Keep the periodic refresh off both balances until the transfer is
	// answered, otherwise it could cache a value from before the transfer.
	Payer.m_MoneyInFlight++;
	Receiver.m_MoneyInFlight++;

	AddPending(pRes, [this, PayerClientId, ReceiverClientId, pExpectedPayer, pExpectedReceiver, PayerName, ReceiverName, Amount](CAccResult &BaseRes) {
		auto *pPayRes = dynamic_cast<CAccPayResult *>(&BaseRes);
		const bool PayerHere = IsExpectedPlayerStillInSlot(GameServer(), PayerClientId, pExpectedPayer) &&
				       GameServer()->m_aAccounts[PayerClientId].m_LoggedIn &&
				       str_comp(GameServer()->m_aAccounts[PayerClientId].m_aUsername, PayerName.c_str()) == 0;
		const bool ReceiverHere = IsExpectedPlayerStillInSlot(GameServer(), ReceiverClientId, pExpectedReceiver) &&
					  GameServer()->m_aAccounts[ReceiverClientId].m_LoggedIn &&
					  str_comp(GameServer()->m_aAccounts[ReceiverClientId].m_aUsername, ReceiverName.c_str()) == 0;
		if(PayerHere && GameServer()->m_aAccounts[PayerClientId].m_MoneyInFlight > 0)
			GameServer()->m_aAccounts[PayerClientId].m_MoneyInFlight--;
		if(ReceiverHere && GameServer()->m_aAccounts[ReceiverClientId].m_MoneyInFlight > 0)
			GameServer()->m_aAccounts[ReceiverClientId].m_MoneyInFlight--;

		if(!BaseRes.m_Success || !pPayRes || !pPayRes->m_PrimarySucceeded)
		{
			if(PayerHere)
				GameServer()->SendChatTarget(PayerClientId, "The database could not complete the transaction. No money was moved.");
			return;
		}

		char aBuf[256];
		switch(pPayRes->m_Status)
		{
		case CAccPayResult::EStatus::OK:
		{
			if(PayerHere)
			{
				GameServer()->m_aAccounts[PayerClientId].m_Money = pPayRes->m_PayerMoney;
				str_format(aBuf, sizeof(aBuf), "You paid %s %" PRId64 "%s", ReceiverName.c_str(), Amount, g_Config.m_SvCurrencyName);
				GameServer()->SendChatTarget(PayerClientId, aBuf);
			}
			if(ReceiverHere)
			{
				// The money is already theirs in the database, the refresh picks
				// the balance up; tell them now so it is not a silent change.
				GameServer()->m_aAccounts[ReceiverClientId].m_LastMoneyFetch = 0;
				str_format(aBuf, sizeof(aBuf), "You received %" PRId64 "%s from %s", Amount, g_Config.m_SvCurrencyName, PayerName.c_str());
				GameServer()->SendChatTarget(ReceiverClientId, aBuf);
			}
			log_info("account", "'%s' paid '%s' %" PRId64, PayerName.c_str(), ReceiverName.c_str(), Amount);
		}
		break;
		case CAccPayResult::EStatus::ON_COOLDOWN:
			if(PayerHere)
			{
				const int64_t Left = pPayRes->m_CooldownEndsAt - time(0);
				str_format(aBuf, sizeof(aBuf), "You need to wait %" PRId64 " more seconds before another transaction.", Left > 0 ? Left : 1);
				GameServer()->SendChatTarget(PayerClientId, aBuf);
			}
			break;
		case CAccPayResult::EStatus::INSUFFICIENT_FUNDS:
			if(PayerHere)
				GameServer()->SendChatTarget(PayerClientId, "You don't have enough money to do this transaction.");
			break;
		case CAccPayResult::EStatus::NO_RECEIVER:
			if(PayerHere)
				GameServer()->SendChatTarget(PayerClientId, "That account could not receive the money. Nothing was transferred.");
			break;
		case CAccPayResult::EStatus::FAILED:
		default:
			if(PayerHere)
				GameServer()->SendChatTarget(PayerClientId, "The transaction failed. No money was moved.");
			break;
		}
	});

	DbPool()->ExecuteWrite(CAccountsWorker::Pay, std::move(pReq), "acc pay");
}
