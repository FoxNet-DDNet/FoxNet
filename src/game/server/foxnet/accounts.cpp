#include "accounts.h"

#include "accountworker.h"
#include "game/server/gamecontext.h"
#include "item_registry.h"
#include "shop.h"

#include <base/hash.h>
#include <base/hash_ctxt.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/server.h>
#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/player.h>

#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>
#include "votemenu.h"

IServer *CAccounts::Server() const { return GameServer()->Server(); }

void CAccounts::AddPending(const std::shared_ptr<CAccResult> &pRes, std::function<void(CAccResult &)> &&Cb)
{
	m_vPending.push_back({pRes, std::move(Cb)});
}

SHA256_DIGEST CAccounts::HashPassword(const char *pPassword)
{
	SHA256_CTX Sha256Ctx;
	sha256_init(&Sha256Ctx);
	sha256_update(&Sha256Ctx, pPassword, str_length(pPassword));
	return sha256_finish(&Sha256Ctx);
}

void CAccounts::Init(CGameContext *pGameServer, CDbConnectionPool *pPool)
{
	m_pGameServer = pGameServer;
	m_pPool = pPool;
	LogoutAllAccountsPort(Server()->Port());
}

void CAccounts::Tick()
{
	if(m_vPending.empty())
	{
		FetchMailBox();
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
}

void CAccounts::AutoLogin(int ClientId)
{
	if(!m_pPool)
		return;
	if(!g_Config.m_SvAccounts)
		return;
	const char *pName = Server()->ClientName(ClientId);
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccSelectByLastName>(pRes);
	str_copy(pReq->m_LastPlayerName, pName, sizeof(pReq->m_LastPlayerName));
	AddPending(pRes, [this, ClientId, Name = std::string(pName)](CAccResult &Res) {
		if(!Res.m_Success || !Res.m_Found || Res.m_LastLogin == 0)
			return;
		if(GameServer()->Server()->ClientSlotEmpty(ClientId))
			return;
		const char *pAddr = Server()->ClientAddrString(ClientId, false);
		if(str_comp(Res.m_LastIP, pAddr) != 0)
			return;
		if(str_comp(Res.m_LastPlayerName, Name.c_str()) != 0)
			return;
		if(Res.m_LoggedIn || !Res.m_Configs.m_AutoLogin)
			return;
		ForceLogin(ClientId, Res.m_aUsername, true, true);
	});
	m_pPool->Execute(CAccountsWorker::SelectByLastPlayerName, std::move(pReq), "acc select by last name");
}

bool CAccounts::ForceLogin(int ClientId, const char *pUsername, bool Silent, bool Auto)
{
	if(!m_pPool || !pUsername[0])
		return false;
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccSelectByUser>(pRes);
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	AddPending(pRes, [this, ClientId, Silent, Auto](CAccResult &Res) {
		if(!Res.m_Success || !Res.m_Found)
			return;
		if(GameServer()->Server()->ClientSlotEmpty(ClientId))
			return;
		if(Res.m_LoggedIn)
		{
			if(!Silent)
				GameServer()->SendChatTarget(ClientId, "Account is already logged in");
			return;
		}
		if(Res.m_Disabled && Auto)
		{
			if(!Silent)
				GameServer()->SendChatTarget(ClientId, "Your account is disabled");
			return;
		}
		if(!Silent)
		{
			GameServer()->SendChatTarget(ClientId, "Logged in successfully");
		}
		else if(Auto)
			GameServer()->SendChatTarget(ClientId, "Automatically logged into your account");
		OnLogin(ClientId, Res);
	});
	m_pPool->Execute(CAccountsWorker::SelectByUsername, std::move(pReq), "acc select by username");
	return true;
}

void CAccounts::Login(int ClientId, const char *pUsername, const char *pPassword)
{
	if(!m_pPool)
		return;
	if(!pUsername[0] || !pPassword[0])
		return;
	char HashedPassword[ACC_MAX_PASSW_LENGTH];
	sha256_str(HashPassword(pPassword), HashedPassword, ACC_MAX_PASSW_LENGTH);
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccLoginRequest>(pRes);
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_PasswordHash, HashedPassword, sizeof(pReq->m_PasswordHash));
	AddPending(pRes, [this, ClientId](CAccResult &Res) {
		if(GameServer()->Server()->ClientSlotEmpty(ClientId))
			return;
		if(GameServer()->m_apPlayers[ClientId])
			GameServer()->m_apPlayers[ClientId]->m_AccLoginAttempts++;
		if(!Res.m_Success || !Res.m_Found)
		{
			GameServer()->SendChatTarget(ClientId, "Login failed");
			return;
		}
		if(Res.m_Disabled)
		{
			GameServer()->SendChatTarget(ClientId, "Your account is disabled");
			return;
		}
		if(Res.m_LoggedIn)
		{
			GameServer()->SendChatTarget(ClientId, "Account is already logged in");
			return;
		}
		GameServer()->SendChatTarget(ClientId, "Login successful");
		OnLogin(ClientId, Res);
	});
	m_pPool->Execute(CAccountsWorker::Login, std::move(pReq), "acc login");
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
	if(!m_pPool)
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
	time_t Now = time(0);
	pReq->m_RegisterDate = Now;
	AddPending(pRes, [this, ClientId](CAccResult &Res) {
		if(GameServer()->Server()->ClientSlotEmpty(ClientId))
			return;
		if(!Res.m_Success)
			GameServer()->SendChatTarget(ClientId, "[Err] Username is already taken");
		else
		{
			GameServer()->SendChatTarget(ClientId, "Registered Successfully, use /login..");
			if(GameServer()->m_apPlayers[ClientId])
				GameServer()->m_apPlayers[ClientId]->m_AccRegisters++;
		}
	});
	m_pPool->ExecuteWrite(CAccountsWorker::Register, std::move(pReq), "acc register");
	return true;
}

void CAccounts::OnLogin(int ClientId, const CAccResult &Res)
{
	CAccountSession &Acc = GameServer()->m_aAccounts[ClientId];

	bool NeedsOverride = Acc.m_Configs.m_SentFastInput;
	bool FastInput = Acc.m_Configs.m_FastInputs;
	time_t Now = time(0);

	str_copy(Acc.m_aUsername, Res.m_aUsername);
	Acc.m_RegisterDate = Res.m_RegisterDate;
	str_copy(Acc.m_pName, Res.m_PlayerName);
	str_copy(Acc.m_LastName, Res.m_LastPlayerName);
	str_copy(Acc.CurrentIp, Server()->ClientAddrString(ClientId, false));
	str_copy(Acc.LastIp, Res.m_LastIP);
	Acc.m_LoggedIn = true;
	Acc.m_LastLogin = Now;
	Acc.m_Port = Server()->Port();
	Acc.ClientId = ClientId;
	Acc.m_Playtime = Res.m_Playtime;
	Acc.m_Deaths = Res.m_Deaths;
	Acc.m_Kills = Res.m_Kills;
	Acc.m_Level = Res.m_Level;
	Acc.m_XP = Res.m_XP;
	Acc.m_Money = Res.m_Money;
	Acc.m_LoginTick = Server()->Tick();
	Acc.m_Inventory = Res.m_Inventory;

	Acc.m_Configs = Res.m_Configs;
	if(NeedsOverride)
		Acc.m_Configs.m_FastInputs = FastInput;

	Acc.m_MailBox = Res.m_MailBox;
	Acc.m_LastMailboxFetch = Now;
	Acc.m_MailboxFetchPending = false;
	GameServer()->OnLogin(ClientId);

	// Apply equipped items to player cosmetics
	if(auto *pPlayer = GameServer()->m_apPlayers[ClientId])
	{
		for(const auto &kv : pPlayer->Inv()->m_Map)
		{
			CInventoryEntry Entry = kv.second;
			const CItemConfig *Cfg = GameServer()->m_Shop.FindItem(kv.first.c_str());
			if(!Cfg)
				continue;
			const int Val = Entry.m_Value;
			if(Val <= 0)
				continue;

			pPlayer->UseItem(kv.first.c_str(), Val, true);
		}
	}

	auto pUpd = std::make_unique<CAccUpdLoginState>();
	str_copy(pUpd->m_aUsername, Res.m_aUsername, sizeof(pUpd->m_aUsername));
	str_copy(pUpd->m_PlayerName, Server()->ClientName(ClientId), sizeof(pUpd->m_PlayerName));
	str_copy(pUpd->m_CurrentIP, Server()->ClientAddrString(ClientId, false), sizeof(pUpd->m_CurrentIP));
	pUpd->m_LastLogin = Now;
	pUpd->m_Port = Server()->Port();
	pUpd->m_ClientId = ClientId;
	m_pPool->ExecuteWrite(CAccountsWorker::UpdateLoginState, std::move(pUpd), "acc update login");
}

bool CAccounts::Logout(int ClientId)
{
	if(GameServer()->m_aAccounts[ClientId].m_LoggedIn)
	{
		OnLogout(ClientId, GameServer()->m_aAccounts[ClientId]);
		GameServer()->m_aAccounts[ClientId] = CAccountSession();
		GameServer()->OnLogout(ClientId);
		return true;
	}
	return false;
}

void CAccounts::OnLogout(int ClientId, const CAccountSession AccInfo)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccSaveInfo>();
	str_copy(pReq->m_aUsername, AccInfo.m_aUsername, sizeof(pReq->m_aUsername));
	pReq->m_Playtime = AccInfo.m_Playtime;
	pReq->m_Deaths = AccInfo.m_Deaths;
	pReq->m_Kills = AccInfo.m_Kills;
	pReq->m_Level = AccInfo.m_Level;
	pReq->m_XP = AccInfo.m_XP;
	pReq->m_Money = AccInfo.m_Money;
	pReq->m_Inventory = AccInfo.m_Inventory;
	pReq->m_Configs = AccInfo.m_Configs;
	m_pPool->ExecuteWrite(CAccountsWorker::UpdateLogoutState, std::move(pReq), "acc update logout");
}

void CAccounts::LogoutAllAccountsPort(int Port)
{
	if(!m_pPool)
		return;
	struct CSqlLogoutByPort : ISqlData
	{
		CSqlLogoutByPort() :
			ISqlData(nullptr) {}
		int m_Port;
	};
	auto Fn = [](IDbConnection *pSql, const ISqlData *pData, Write, char *pError, int ErrorSize) -> bool {
		const auto *p = dynamic_cast<const CSqlLogoutByPort *>(pData);
		char aSql[256];
		str_copy(aSql,
			"UPDATE foxnet_accounts SET LastPlayerName = PlayerName, LastIP = CurrentIP, LoggedIn = 0, Port = 0, ClientId = -1 WHERE Port = ?",
			sizeof(aSql));
		if(!pSql->PrepareStatement(aSql, pError, ErrorSize))
			return false;
		pSql->BindInt(1, p->m_Port);
		int NumUpdated = 0;
		return pSql->ExecuteUpdate(&NumUpdated, pError, ErrorSize);
	};
	auto pReq = std::make_unique<CSqlLogoutByPort>();
	pReq->m_Port = Port;
	m_pPool->ExecuteWrite(Fn, std::move(pReq), "acc bulk logout by port");
}

bool CAccounts::ChangePassword(int ClientId, const char *pOldPassword, const char *pNewPassword)
{
	if(!m_pPool)
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

	AddPending(pRes, [this, ClientId](CAccResult &Res) {
		if(GameServer()->Server()->ClientSlotEmpty(ClientId))
			return;
		if(Res.m_NumMessages == 0)
		{
			GameServer()->SendChatTarget(ClientId, "[Err] Password change result unknown");
			return;
		}
		for(int i = 0; i < Res.m_NumMessages; i++)
			GameServer()->SendChatTarget(ClientId, Res.m_aaMessages[i]);
	});

	m_pPool->ExecuteWrite(CAccountsWorker::ChangePassword, std::move(pReq), "acc change password");
	return true;
}

void CAccounts::SetPassword(const char *pUsername, const char *pNewPassword)
{
	if(!m_pPool)
		return;
	if(!pUsername[0] || !pNewPassword[0])
		return;
	char HashedPassword[ACC_MAX_PASSW_LENGTH];
	sha256_str(HashPassword(pNewPassword), HashedPassword, ACC_MAX_PASSW_LENGTH);
	auto pReq = std::make_unique<CAccSetPassword>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_aNewPasswordHash, HashedPassword, sizeof(pReq->m_aNewPasswordHash));
	m_pPool->ExecuteWrite(CAccountsWorker::SetPassword, std::move(pReq), "acc change password (admin)");
}

void CAccounts::ShowAccProfile(int ClientId, const char *pName)
{
	if(!m_pPool || !pName[0])
		return;
	auto SendProfile = [this, ClientId, NameCopy = std::string(pName)](const CAccResult &Data) {
		char aBuf[128];
		GameServer()->SendChatTarget(ClientId, "╭──────     Pʀᴏғɪʟᴇ");
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
		GameServer()->SendChatTarget(ClientId, "├──────      Sᴛᴀᴛs");
		str_format(aBuf, sizeof(aBuf), "│ Level %ld", Data.m_Level);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ %ld%s", Data.m_Money, g_Config.m_SvCurrencyName);
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ %s Playtime", FormatPlaytime(Data.m_Playtime));
		GameServer()->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "│ %ld Deaths", Data.m_Deaths);
		GameServer()->SendChatTarget(ClientId, aBuf);
		GameServer()->SendChatTarget(ClientId, "╰───────────────────────");
	};
	auto QueryByUsername = [this, ClientId, pNameStr = std::string(pName), SendProfile]() {
		auto pRes2 = std::make_shared<CAccResult>();
		auto pReq2 = std::make_unique<CAccSelectByUser>(pRes2);
		str_copy(pReq2->m_aUsername, pNameStr.c_str(), sizeof(pReq2->m_aUsername));
		AddPending(pRes2, [this, ClientId, pNameStr, SendProfile](CAccResult &Res2) {
			if(!Res2.m_Success || !Res2.m_Found)
			{
				GameServer()->SendChatTarget(ClientId, "╭─────────       Pʀᴏғɪʟᴇ");
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "│ Account \"%s\" doesn't exist", pNameStr.c_str());
				GameServer()->SendChatTarget(ClientId, aBuf);
				GameServer()->SendChatTarget(ClientId, "╰──────────────────────────");
				return;
			}
			SendProfile(Res2);
		});
		m_pPool->Execute(CAccountsWorker::SelectByUsername, std::move(pReq2), "acc select by username (profile)");
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
	m_pPool->Execute(CAccountsWorker::SelectByLastPlayerName, std::move(pReq), "acc select by last name (profile)");
}

void CAccounts::SaveAccountsInfo(int ClientId, const CAccountSession AccInfo)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccSaveInfo>();
	str_copy(pReq->m_aUsername, AccInfo.m_aUsername, sizeof(pReq->m_aUsername));
	pReq->m_Playtime = AccInfo.m_Playtime;
	pReq->m_Deaths = AccInfo.m_Deaths;
	pReq->m_Kills = AccInfo.m_Kills;
	pReq->m_Level = AccInfo.m_Level;
	pReq->m_XP = AccInfo.m_XP;
	pReq->m_Money = AccInfo.m_Money;
	pReq->m_Inventory = AccInfo.m_Inventory;
	pReq->m_Configs = AccInfo.m_Configs;
	m_pPool->ExecuteWrite(CAccountsWorker::SaveInfo, std::move(pReq), "acc save info");
}

void CAccounts::SaveAllAccounts()
{
	if(!m_pPool)
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_aAccounts[i].m_LoggedIn)
			SaveAccountsInfo(i, GameServer()->m_aAccounts[i]);
	}
}

void CAccounts::Top5(int ClientId, const char *pType, int Offset)
{
	if(!m_pPool)
		return;
	auto pRes = std::make_shared<CAccResult>();
	auto pReq = std::make_unique<CAccShowTop5>(pRes);
	pReq->m_ClientId = ClientId;
	str_copy(pReq->m_Type, pType, sizeof(pReq->m_Type));
	pReq->m_Offset = Offset;

	AddPending(pRes, [this, ClientId](CAccResult &Res) {
		if(GameServer()->Server()->ClientSlotEmpty(ClientId))
			return;
		if(Res.m_NumMessages == 0)
		{
			GameServer()->SendChatTarget(ClientId, "[Err] unknown error occurred");
			return;
		}
		for(int i = 0; i < Res.m_NumMessages; i++)
			GameServer()->SendChatTarget(ClientId, Res.m_aaMessages[i]);
	});

	m_pPool->ExecuteWrite(CAccountsWorker::ShowTop5, std::move(pReq), "show acc top5");
}

void CAccounts::SetPlayerName(int ClientId, const char *pName)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccSetNameReq>();
	str_copy(pReq->m_NewPlayerName, pName, sizeof(pReq->m_NewPlayerName));
	str_copy(pReq->m_aUsername, GameServer()->m_aAccounts[ClientId].m_aUsername, sizeof(pReq->m_aUsername));
	m_pPool->ExecuteWrite(CAccountsWorker::SetPlayerName, std::move(pReq), "acc set player name");
}

void CAccounts::DisableAccount(const char *pUsername, bool Disable)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccDisable>();
	pReq->m_Disable = Disable;
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	m_pPool->ExecuteWrite(CAccountsWorker::DisableAccount, std::move(pReq), "acc (un)disable");
}

void CAccounts::RemoveItem(const char *pUsername, const char *pItemName)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccRemoveItem>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	str_copy(pReq->m_aItemName, pItemName, sizeof(pReq->m_aItemName));
	m_pPool->ExecuteWrite(CAccountsWorker::RemoveItem, std::move(pReq), "acc remove item");
}

int CAccounts::NeededXP(int Level)
{
	if(Level < 1)
		return 30;
	else if(Level < 5)
		return 65;
	else if(Level < 10)
		return 100;
	else if(Level < 20)
		return 150;
	else
		return 150 + Level * 2;
}

void CAccounts::MarkAllMailsRead(const char *pUsername)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccMarkAllMailsRead>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	m_pPool->ExecuteWrite(CAccountsWorker::MarkAllMailsRead, std::move(pReq), "acc mark all mails read");
}

void CAccounts::ClaimAllMailRewards(const char *pUsername)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccClaimAllMailRewards>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	m_pPool->ExecuteWrite(CAccountsWorker::ClaimAllMailRewards, std::move(pReq), "acc mark all mails claimed");
}

void CAccounts::DeleteAllReadMails(const char *pUsername)
{
	if(!m_pPool || !pUsername || !pUsername[0])
		return;

	auto pReq = std::make_unique<CAccDeleteAllRead>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	m_pPool->ExecuteWrite(CAccountsWorker::DeleteAllReadMails, std::move(pReq), "acc delete all read mails");
}

void CAccounts::SetMailRead(const char *pUsername, int64_t MailId, bool Read)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccSetMailRead>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	pReq->m_MailId = MailId;
	pReq->m_Read = Read;
	m_pPool->ExecuteWrite(CAccountsWorker::SetMailRead, std::move(pReq), "acc set mail read");
}

void CAccounts::SetMailUsedCmd(const char *pUsername, int64_t MailId, bool Used)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccSetMailUsedCmd>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	pReq->m_MailId = MailId;
	pReq->m_UsedCmd = Used;
	m_pPool->ExecuteWrite(CAccountsWorker::SetMailUsedCmd, std::move(pReq), "acc set mail used cmd");
}

void CAccounts::DeleteMail(const char *pUsername, int64_t MailId)
{
	if(!m_pPool)
		return;
	auto pReq = std::make_unique<CAccDeleteMail>();
	str_copy(pReq->m_aUsername, pUsername, sizeof(pReq->m_aUsername));
	pReq->m_MailId = MailId;
	m_pPool->ExecuteWrite(CAccountsWorker::DeleteMail, std::move(pReq), "acc delete mail");
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
	if(!m_pPool)
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

	m_pPool->ExecuteWrite(CAccountsWorker::NewMail, std::move(pReq), "acc new mail");
}

void CAccounts::NewGlobalMail(const char *pSubject, const char *pMessage, const char *pCmdName, const char *pCmd, bool IncludeDisabled, bool OnlyLoggedIn, int MinLevel)
{
	if(!m_pPool)
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

	m_pPool->ExecuteWrite(CAccountsWorker::NewGlobalMail, std::move(pReq), "acc bulk new mail");
}

void CAccounts::FetchMailBox()
{
	if(!m_pPool)
		return;

	time_t Now = time(0);

	struct CSqlLoadMailbox : ISqlData
	{
		CSqlLoadMailbox(std::shared_ptr<CAccResult> pRes) :
			ISqlData(std::move(pRes)) {}
		char m_aUsername[ACC_MAX_USERNAME_LENGTH]{};
	};

	auto FnLoadMailbox = [](IDbConnection *pSql, const ISqlData *pData, char *pError, int ErrorSize) -> bool {
		const auto *p = dynamic_cast<const CSqlLoadMailbox *>(pData);
		auto *pRes = dynamic_cast<CAccResult *>(pData->m_pResult.get());
		if(!p || !pRes)
			return false;

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

		pRes->m_Success = true;
		pRes->m_Completed.store(true);
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

		auto pRes = std::make_shared<CAccResult>();
		auto pReq = std::make_unique<CSqlLoadMailbox>(pRes);
		str_copy(pReq->m_aUsername, Acc.m_aUsername, sizeof(pReq->m_aUsername));

		AddPending(pRes, [this, i, Now](CAccResult &Res) {
			if(Server()->ClientSlotEmpty(i))
				return;
			auto &AccRef = GameServer()->m_aAccounts[i];
			AccRef.m_MailBox = Res.m_MailBox;
			AccRef.m_LastMailboxFetch = Now;
			AccRef.m_MailboxFetchPending = false;
		});

		m_pPool->Execute(FnLoadMailbox, std::move(pReq), "acc mailbox refresh");
	}
}