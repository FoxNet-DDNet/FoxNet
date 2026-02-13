#include "entities/pickupdrop.h"
#include "votemenu.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/foxnet/entities/text/text.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/score.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <base/time.h>
#include <base/types.h>

void CGameContext::ConAccRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Accounts are disabled");
		return;
	}

	if(pSelf->m_aAccounts[ClientId].m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You are already logged in");
		return;
	}
	const char *pUser = pResult->GetString(0);
	const char *pPass = pResult->GetString(1);

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	if(pPlayer->m_AccRegisters >= 2)
	{
		char aBanBuf[256];
		str_format(aBanBuf, sizeof(aBanBuf), "`%s` [%s] was banned for 1440 minutes for too many '/register's.\n"
						     "ver: %s (%d) [%s]",
			pSelf->Server()->ClientName(ClientId),
			pSelf->Server()->ClientAddrString(ClientId, false),
			pSelf->Server()->GetCustomClient(ClientId),
			pSelf->Server()->GetClientVersion(ClientId),
			pSelf->Server()->GetClientVersionStr(ClientId));
		char aTitle[32];
		str_format(aTitle, sizeof(aTitle), "[BAN] - /Register (%d)", pSelf->Server()->Port());
		pSelf->Server()->SendWebhookMessage(g_Config.m_DcBansWebhookUrl, aBanBuf, aTitle);
		pSelf->Server()->Ban(ClientId, 1440 * 60, "Too many registrations.", false);
		return;
	}

	pSelf->m_AccountManager.Register(ClientId, pUser, pPass);
}

void CGameContext::ConAccPassword(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Accounts are disabled");
		return;
	}

	if(!pSelf->m_aAccounts[ClientId].m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You aren't logged in");
		return;
	}
	const char *pOldPass = pResult->GetString(0);
	const char *pPass = pResult->GetString(1);

	pSelf->m_AccountManager.ChangePassword(ClientId, pOldPass, pPass);
}

void CGameContext::ConAccLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Accounts are disabled");
		return;
	}

	if(pSelf->m_aAccounts[ClientId].m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You are already logged in");
		return;
	}

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pPlayer->m_AccLoginAttempts >= g_Config.m_SvRconMaxTries)
	{
		char aBanBuf[256];
		str_format(aBanBuf, sizeof(aBanBuf), "`%s` [%s] was banned for %d minutes for too many '/login' attempts.\n"
						     "ver: %s (%d) [%s]",
			pSelf->Server()->ClientName(ClientId),
			pSelf->Server()->ClientAddrString(ClientId, false),
			g_Config.m_SvRconBantime,
			pSelf->Server()->GetCustomClient(ClientId),
			pSelf->Server()->GetClientVersion(ClientId),
			pSelf->Server()->GetClientVersionStr(ClientId));
		char aTitle[32];
		str_format(aTitle, sizeof(aTitle), "[BAN] - /Login (%d)", pSelf->Server()->Port());
		pSelf->Server()->SendWebhookMessage(g_Config.m_DcBansWebhookUrl, aBanBuf, aTitle);
		pSelf->Server()->Ban(ClientId, g_Config.m_SvRconBantime * 60, "Too many /login attempts.", false);
		return;
	}

	const char *pUser = pResult->GetString(0);
	const char *pPass = pResult->GetString(1);

	pSelf->m_AccountManager.Login(ClientId, pUser, pPass);
}

void CGameContext::ConAccLogout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(pSelf->m_AccountManager.Logout(ClientId))
		pSelf->SendChatTarget(ClientId, "Logged out");
	else
		pSelf->SendChatTarget(ClientId, "Not logged in");
}

void CGameContext::ConAccProfile(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->NumArguments() ? pResult->GetString(0) : pSelf->Server()->ClientName(pResult->m_ClientId);
	pSelf->m_AccountManager.ShowAccProfile(pResult->m_ClientId, pName);
}

void CGameContext::ConAccTop5Money(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_AccountManager.Top5(pResult->m_ClientId, "money", pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0);
}
void CGameContext::ConAccTop5Level(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_AccountManager.Top5(pResult->m_ClientId, "level", pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0);
}
void CGameContext::ConAccTop5Playtime(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_AccountManager.Top5(pResult->m_ClientId, "playtime", pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0);
}

void CGameContext::ConAccDisable(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pUser = pResult->GetString(0);
	const int pValue = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : 1;

	pSelf->m_AccountManager.DisableAccount(pUser, pValue);
}

void CGameContext::ConAccForcePassword(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pUser = pResult->GetString(0);
	const char *pNewPassword = pResult->GetString(1);

	pSelf->m_AccountManager.SetPassword(pUser, pNewPassword);
}

void CGameContext::ConAccForceLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	const char *pName = pResult->GetString(0);
	pSelf->m_AccountManager.ForceLogin(Victim, pName);
}

void CGameContext::ConAccForceLogout(IConsole::IResult *pResult, void *pUserData)
{ // Logs out Current Acc Session, does not work across servers
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	pSelf->m_AccountManager.Logout(ClientId);
}

void CGameContext::ConGiveMoney(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;

	if(!g_Config.m_SvAccounts)
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	if(!pPlayer->Acc()->m_LoggedIn)
		return;

	const int Amount = pResult->GetInteger(1);
	pPlayer->GiveMoney(Amount, false);
}

void CGameContext::ConGiveXp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	if(!g_Config.m_SvAccounts)
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	if(!pPlayer->Acc()->m_LoggedIn)
		return;

	const int Amount = pResult->GetInteger(1);
	if(Amount > 0)
		pPlayer->GiveXP(Amount, "", false);
}

void CGameContext::ConGiveItem(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	char aFrom[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aFrom, pSelf->Server()->ClientName(ClientId));

	const char *pItemName = pResult->GetString(1);
	pSelf->m_Shop.GiveItem(ClientId, pItemName, -1, aFrom);
}

void CGameContext::ConGiveItemDays(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const int Days = pResult->GetInteger(1);
	const char *pItemName = pResult->GetString(2);

	char aFrom[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aFrom, pSelf->Server()->ClientName(ClientId));

	pSelf->m_Shop.GiveItem(ClientId, pItemName, Days, aFrom);
}

void CGameContext::ConGiveItemForever(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	char aFrom[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aFrom, pSelf->Server()->ClientName(ClientId));

	const char *pItemName = pResult->GetString(1);
	pSelf->m_Shop.GiveItemForever(ClientId, pItemName, aFrom);
}

void CGameContext::ConRemoveItem(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();
	if(!CheckClientId(ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	char aBy[MAX_NAME_LENGTH] = "Server";
	if(CheckClientId(ClientId))
		str_copy(aBy, pSelf->Server()->ClientName(ClientId));

	const char *pItemName = pResult->GetString(1);
	pSelf->m_Shop.RemoveItem(ClientId, pItemName, aBy);
}

void CGameContext::ConNewMail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pUsername = pResult->GetString(0);
	const char *pSubject = pResult->GetString(1);
	const char *pMessage = pResult->GetString(2);
	const char *pCmdName = pResult->GetString(3);
	const char *pCmd = pResult->GetString(4);

	pSelf->m_AccountManager.NewMail(pUsername, pSubject, pMessage, pCmdName, pCmd);
}

void CGameContext::ConNewGlobalMail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Param = 0;
	const char *pSubject = pResult->GetString(Param++);
	const char *pMessage = pResult->GetString(Param++);
	const char *pCmdName = pResult->GetString(Param++);
	const char *pCmd = pResult->GetString(Param++);

	int MinLevel = pResult->NumArguments() >= Param + 1 ? pResult->GetInteger(Param++) : 0;
	bool OnlyOnline = pResult->NumArguments() >= Param + 1 ? pResult->GetInteger(Param++) != 0 : 0;
	bool IncludeDisabled = pResult->NumArguments() >= Param + 1 ? pResult->GetInteger(Param++) != 0 : 0;

	pSelf->m_AccountManager.NewGlobalMail(pSubject, pMessage, pCmdName, pCmd, IncludeDisabled, OnlyOnline, MinLevel);
}

void CGameContext::ConAddChatDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	const char *Reason = pResult->GetString(1);
	bool Ban = pResult->GetInteger(2);
	int BanTime = pResult->GetInteger(3);
	float Addition = pResult->GetFloat(4);
	if(BanTime < 0)
	{
		log_info("chat-detection", "Ban time must be greater than 0");
		return;
	}
	if(Addition <= 0.0f)
		Addition = 1.0f;
	for(const auto &Words : pSelf->m_vChatDetection)
	{
		if(Words.String()[0] == '\0')
			continue;
		if(!str_comp_nocase(Words.String(), String))
		{
			log_info("chat-detection", "String \"%s\" already exists in the list", String);
			return;
		}
	}

	if(str_comp_nocase(String, "") != 0)
	{
		pSelf->m_vChatDetection.push_back(CStringDetection(String, Reason, Addition, Ban, BanTime));
		if(pResult->m_ClientId >= 0)
			log_info("chat-detection", "Added \"%s\" to the Chat Detection List", String);
	}
}

void CGameContext::ConClearChatDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_vChatDetection.clear();
}

void CGameContext::ConRemoveChatDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	pSelf->RemoveChatDetectionString(String);
}

void CGameContext::RemoveChatDetectionString(const char *pString)
{
	if(pString[0] == '\0')
		return;

	if(m_vChatDetection.empty())
	{
		log_info("chat-detection", "List is Empty");
		return;
	}

	for(auto it = m_vChatDetection.begin(); it != m_vChatDetection.end(); ++it)
	{
		if(!str_comp_nocase(it->String(), pString))
		{
			m_vChatDetection.erase(it);
			log_info("chat-detection", "Removed \"%s\" from the Chat Detection List", pString);
			return;
		}
	}
}

void CGameContext::ConListChatDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_vChatDetection.empty())
	{
		log_info("chat-detection", "List is Empty");
		return;
	}

	for(const auto &Words : pSelf->m_vChatDetection)
	{
		if(Words.String()[0] == '\0')
			continue;

		log_info("chat-detection", "Str: %s | Reas: %s | Time: %d | Bans: %s | CountAdd: %.1f", Words.String(), Words.Reason(), Words.Time(), Words.IsBan() ? "Yes" : "No", Words.Addition());
	}
}

void CGameContext::ConAddNameDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	const char *Reason = pResult->GetString(1);
	int BanTime = pResult->GetInteger(2);
	int ExactName = pResult->NumArguments() > 3 ? pResult->GetInteger(3) : 0;
	if(BanTime < 0)
	{
		log_info("name-detection", "Ban time must be greater than 0");
		return;
	}
	if(ExactName < 0 || ExactName > 2)
	{
		log_info("name-detection", "Exact Name must be between 0 and 2");
		log_info("name-detection", "0=search for string | 1=full name match case sensitive | 2=1 but case insensitive");
		return;
	}

	for(const auto &Words : pSelf->m_vNameDetection)
	{
		if(Words.String()[0] == '\0')
			continue;
		if(!str_comp_nocase(Words.String(), String))
		{
			log_info("name-detection", "Name \"%s\" already exists in the list", String);
			return;
		}
	}

	if(str_comp_nocase(String, "") != 0)
	{
		pSelf->m_vNameDetection.push_back(CStringDetection(String, Reason, 1, BanTime, ExactName));
		if(pResult->m_ClientId >= 0)
			log_info("name-detection", "Added \"%s\" to the Name Detection List", String);
	}
}

void CGameContext::ConClearNameDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_vNameDetection.clear();
}

void CGameContext::ConRemoveNameDetectionString(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *String = pResult->GetString(0);
	pSelf->RemoveNameDetectionString(String);
}

void CGameContext::RemoveNameDetectionString(const char *pString)
{
	if(pString[0] == '\0')
		return;
	if(m_vNameDetection.empty())
	{
		log_info("name-detection", "List is Empty");
		return;
	}

	for(auto it = m_vNameDetection.begin(); it != m_vNameDetection.end(); ++it)
	{
		if(!str_comp(it->String(), pString))
		{
			m_vNameDetection.erase(it);
			log_info("name-detection", "Removed \"%s\" from the Name Detection List", pString);
			return;
		}
	}
}

void CGameContext::ConListNameDetectionStrings(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_vNameDetection.empty())
	{
		log_info("name-detection", "List is Empty");
		return;
	}

	for(const auto &Words : pSelf->m_vNameDetection)
	{
		if(Words.String()[0] == '\0')
			continue;

		log_info("name-detection", "Str: %s | Reas: %s | Time: %d | Exact: %d", Words.String(), Words.Reason(), Words.Time(), Words.ExactMatch());
	}
}

void CGameContext::ConShopListItems(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_Shop.ListItems();
}

void CGameContext::ConShopEditItem(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pItem = pResult->GetString(0);
	int Price = pResult->GetInteger(1);
	int MinLevel = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : -1;

	pSelf->m_Shop.EditItem(pItem, Price, MinLevel);
}

void CGameContext::ConShopReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_Shop.ResetItems();
}

void CGameContext::ConShopBuyItem(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;
	const char *pItem = pResult->GetString(0);
	pSelf->m_Shop.BuyItem(ClientId, pItem);
}

void CGameContext::ConToggleItem(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];

	if(!pPlayer)
		return;

	const char *pItem = pResult->GetString(0);
	const int Value = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : -1;

	pPlayer->UseItem(pItem, Value);
}

void CGameContext::ConRainbowBody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_RainbowBody;
	pPlayer->SetRainbowBody(Set);
	log_info("cosmetics", "Set rainbow body to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConRainbowFeet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_RainbowFeet;
	pPlayer->SetRainbowFeet(Set);
	log_info("cosmetics", "Set rainbow feet to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConRainbowSpeed(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	if(pResult->NumArguments() < 2)
	{
		log_info("cosmetics", "Speed: %d", pPlayer->Cosmetics()->m_RainbowSpeed);
	}
	else
	{
		int Speed = std::clamp(pResult->GetInteger(1), 1, 200);
		log_info("cosmetics", "Rainbow speed for '%s' changed to %d", pSelf->Server()->ClientName(Victim), Speed);
		pPlayer->Cosmetics()->m_RainbowSpeed = Speed;
	}
}

void CGameContext::ConSparkle(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Sparkle;
	pPlayer->SetSparkle(Set);
	log_info("cosmetics", "Set sparkle to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConDotTrail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	int Trail = pPlayer->Cosmetics()->m_Trail == TRAILTYPE_DOT ? TRAILTYPE_NONE : TRAILTYPE_DOT;

	pPlayer->SetTrail(Trail);
	log_info("cosmetics", "Set trail to %d for player %s", Trail, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConStarTrail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Trail = pPlayer->Cosmetics()->m_Trail == TRAILTYPE_STAR ? TRAILTYPE_NONE : TRAILTYPE_STAR;

	pPlayer->SetTrail(Trail);
	log_info("cosmetics", "Set star trail to %d for player %s", Trail, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConInverseAim(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_InverseAim;
	pPlayer->SetInverseAim(Set);
	log_info("cosmetics", "Set inverse aim to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConLovely(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Lovely;
	pPlayer->SetLovely(Set);
	log_info("cosmetics", "Set lovely to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConRotatingBall(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_RotatingBall;
	pPlayer->SetRotatingBall(Set);
	log_info("cosmetics", "Set rotating ball to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConEpicCircle(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_EpicCircle;
	pPlayer->SetEpicCircle(Set);
	log_info("cosmetics", "Set epic circle to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConBloody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Bloody;
	pPlayer->SetBloody(Set);
	log_info("cosmetics", "Set bloody to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHeartHat(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_HeartHat;
	pPlayer->SetHeartHat(Set);
	log_info("cosmetics", "Set heart hat to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConDeathEffect(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() < 1 ? 0 : pResult->GetInteger(0);
	pPlayer->SetDeathEffect(Type);
	log_info("cosmetics", "Set death effect to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConDamageIndType(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() < 1 ? 0 : pResult->GetInteger(0);
	pPlayer->SetDamageIndType(Type);
	log_info("cosmetics", "Set damage ind to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConGunType(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() < 1 ? 0 : pResult->GetInteger(0);
	pPlayer->SetGunType(Type);
	log_info("cosmetics", "Set gun type to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHatType(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Type = pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 0;
	pPlayer->SetHatType((EHatType)Type);
	log_info("cosmetics", "Set hat type to %d for player %s", Type, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConStaffInd(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_StaffInd;
	pPlayer->SetStaffInd(Set);
	log_info("cosmetics", "Set staff indicator to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetPickupPet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;
	bool Set = !pPlayer->Cosmetics()->m_PickupPet;
	pPlayer->SetPickupPet(Set);
	log_info("cosmetics", "Set pickup pet to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConLissajous(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_Lissajous;
	pPlayer->SetLissajous(Set);
	log_info("cosmetics", "Set lissajous to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHookPower(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Power = -1;
	if(pResult->NumArguments() && pSelf->IsValidHookPower(pResult->GetInteger(0)))
		Power = pResult->GetInteger(0);
	if(Power == -1)
	{
		log_info("console", "~~~ Hook Powers ~~~");
		for(int i = 0; i < NUM_HOOKTYPES; i++)
		{
			log_info("hook-power", "%d = %s", i, pSelf->HookTypeName(i));
		}
	}
	else
	{
		if(pPlayer->Cosmetics()->m_HookPower == Power)
			Power = HOOKTYPE_NORMAL;
		pPlayer->HookPower(Power);
		log_info("hook-power", "%s", pSelf->HookTypeName(Power));
	}
}

void CGameContext::ConInvisible(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->m_Invisible;
	pPlayer->SetInvisible(Set);
	log_info("cosmetics", "Set invisible to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConStrongBloody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_StrongBloody;
	pPlayer->SetStrongBloody(Set);
	log_info("cosmetics", "Set strong bloody to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSnake(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;
	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	bool Set = !pChr->m_Snake.Active();
	pChr->SetSnake(!pChr->m_Snake.Active());
	log_info("cosmetics", "Set snake to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConHidePowerUps(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Acc()->m_Configs.m_HidePowerUps;
	pPlayer->SetHidePowerUps(Set);
	log_info("cosmetics", "Set hide powerups to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetEmoticonGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	int Set = pResult->GetInteger(0);
	pPlayer->SetEmoticonGun(Set);
	log_info("cosmetics", "Set emote gun to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConPhaseGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Set = !pPlayer->Cosmetics()->m_PhaseGun;
	pPlayer->SetPhaseGun(Set);
	log_info("cosmetics", "Set phase gun to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetConfettiGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;
	bool Set = !pPlayer->Cosmetics()->m_ConfettiGun;
	pPlayer->SetConfettiGun(Set);
	log_info("cosmetics", "Set confetti gun to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetUfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr || g_Config.m_SvAutoUfo)
		return;

	bool Set = !pChr->m_Ufo.Active();
	pChr->SetUfo(Set);
	log_info("cosmetics", "Set ufo to %d for player %s", Set, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetPlayerName(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->Server()->OverrideClientName(Victim, pResult->GetString(1));
	log_info("server", "changed player '%s's changed name to '%s'", pSelf->Server()->ClientName(Victim), pResult->GetString(1));
}

void CGameContext::ConSetPlayerClan(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pSelf->Server()->SetClientClan(Victim, pResult->GetString(1));
	log_info("server", "changed player '%s's changed clan to '%s'", pSelf->Server()->ClientName(Victim), pResult->GetString(1));
}

void CGameContext::ConSetPlayerSkin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	str_copy(pPlayer->m_TeeInfos.m_aSkinName, pResult->GetString(1));
	log_info("server", "changed player '%s's changed skin to '%s'", pSelf->Server()->ClientName(Victim), pResult->GetString(1));
}

void CGameContext::ConSetPlayerCustomColor(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_TeeInfos.m_UseCustomColor = pResult->GetInteger(1);
	log_info("server", "changed player '%s's changed custom color to '%d'", pSelf->Server()->ClientName(Victim), pResult->GetInteger(1));
}

void CGameContext::ConSetPlayerColorBody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_TeeInfos.m_ColorBody = pResult->GetInteger(1);
	log_info("server", "changed player '%s's changed body color to '%d'", pSelf->Server()->ClientName(Victim), pResult->GetInteger(1));
}

void CGameContext::ConSetPlayerColorFeet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_TeeInfos.m_ColorFeet = pResult->GetInteger(1);
	log_info("server", "changed player '%s's changed feet color to '%d'", pSelf->Server()->ClientName(Victim), pResult->GetInteger(1));
}

void CGameContext::ConSetPlayerAfk(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	bool Afk = !pPlayer->IsAfk();
	if(pResult->NumArguments() > 1)
		Afk = pResult->GetInteger(1);

	pPlayer->SetInitialAfk(Afk);
	log_info("server", "changed player '%s's afk status to '%d'", pSelf->Server()->ClientName(Victim), Afk);
}

void CGameContext::ConSetAbility(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	const int Ability = pResult->GetInteger(0);
	pPlayer->SetAbility(Ability);
	log_info("server", "Set ability to %d for player '%s'", Ability, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConIgnoreGameLayer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->SetIgnoreGameLayer(!pPlayer->m_IgnoreGamelayer);
	log_info("server", "Set ignore game layer to %d for player %s", pPlayer->m_IgnoreGamelayer, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConSetVanish(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_Vanish = !pPlayer->m_Vanish;

	char PlayerInfo[512] = " (No Client Info)";
	char aBuf[128];

	if(!pPlayer->m_Vanish)
	{
		IServer::CClientInfo Info;
		if(pSelf->Server()->GetClientInfo(Victim, &Info) && Info.m_GotDDNetVersion)
			str_format(PlayerInfo, sizeof(PlayerInfo), "(%s %d)", pSelf->Server()->GetCustomClient(Victim), Info.m_DDNetVersion);
		else if(Info.m_GotDDNetVersion)
			str_format(PlayerInfo, sizeof(PlayerInfo), "(%d)", Info.m_DDNetVersion);

		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s %s", pSelf->Server()->ClientName(Victim), pSelf->m_pController->GetTeamName(pPlayer->GetTeam()), PlayerInfo);
	}
	else
		str_format(aBuf, sizeof(aBuf), "'%s' has left the game", pSelf->Server()->ClientName(Victim));

	pSelf->SendChat(-1, TEAM_ALL, aBuf, -1, CGameContext::FLAG_SIX);
}

void CGameContext::ConSetVanishQuiet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->m_Vanish = !pPlayer->m_Vanish;
}

void CGameContext::ConIncludeInServerInfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	int Include = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : -2;

	if(Include == -2)
	{
		pPlayer->m_IncludeServerInfo = pPlayer->m_IncludeServerInfo == 0 ? 1 : 0;
	}
	else
	{
		Include = std::clamp(Include, 0, 1);
		pPlayer->m_IncludeServerInfo = Include;
	}
}

void CGameContext::ConRedirectClient(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	pSelf->Server()->RedirectClient(Victim, pResult->GetInteger(1));
}

void CGameContext::ConSetPassive(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetPassive(!pChr->Core()->m_Passive);
}

void CGameContext::ConSetHittable(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetHittable(!pChr->Core()->m_Hittable);
}

void CGameContext::ConSetHookable(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetHookable(!pChr->Core()->m_Hookable);
}

void CGameContext::ConSetCollidable(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetCollidable(!pChr->Core()->m_Collidable);
}

void CGameContext::ConSetTuneOverride(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->GetVictim() : pResult->m_ClientId;

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	pChr->SetTuneOverride(pResult->GetInteger(0));
}

void CGameContext::ConTelekinesisImmunity(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];

	if(!pPlayer)
		return;

	pPlayer->SetTelekinesisImmunity(!pPlayer->m_TelekinesisImmunity);
	log_info("server", "Set telekinesis immunity to %d for player %s", pPlayer->m_TelekinesisImmunity, pSelf->Server()->ClientName(Victim));
}

void CGameContext::ConTelekinesis(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	const int Weapon = WEAPON_TELEKINESIS;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConHeartGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	const int Weapon = WEAPON_HEARTGUN;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConLightsaber(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;
	const int Weapon = WEAPON_LIGHTSABER;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConPortalGun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->NumArguments() > 1 ? pResult->m_ClientId : pResult->GetVictim();

	CCharacter *pChr = pSelf->GetPlayerChar(Victim);

	if(!pChr)
		return;

	const int Weapon = WEAPON_PORTALGUN;
	const bool GotWeapon = pChr->GetWeaponGot(Weapon);

	if(GotWeapon)
		pChr->GiveWeapon(Weapon, true);
	else
	{
		pChr->GiveWeapon(Weapon);
		pChr->SetActiveWeapon(Weapon);
	}
}

void CGameContext::ConSetObfuscated(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->SetObfuscated(!pPlayer->m_Obfuscated);
	log_info("server", "Set obfuscated to %d for '%s' (%d)", pPlayer->m_Obfuscated, pSelf->Server()->ClientName(ClientId), ClientId);
}

void CGameContext::ConSetSpiderHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->m_SpiderHook = !pPlayer->m_SpiderHook;
	log_info("server", "Set spider hook to %d for '%s' (%d)", pPlayer->m_SpiderHook, pSelf->Server()->ClientName(ClientId), ClientId);
}

void CGameContext::ConSetSpazzing(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->NumArguments() ? pResult->GetVictim() : pResult->m_ClientId;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->m_Spazzing = !pPlayer->m_Spazzing;
	log_info("server", "Set spazzing to %d for '%s' (%d)", pPlayer->m_Spazzing, pSelf->Server()->ClientName(ClientId), ClientId);
}

void CGameContext::ConSendFakeMessage(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	const char *pMsg = pResult->GetString(1);
	pSelf->AddFakeMessage(pName, pMsg, "Robot");
}

void CGameContext::ConToggleMapVoteLock(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_MapVoteLock = !pSelf->m_MapVoteLock;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(pSelf->m_apPlayers[ClientId])
		{
			const int VoteMenuPage = pSelf->m_VoteMenu.GetPage(ClientId);
			if(VoteMenuPage == PAGE_VOTES || !g_Config.m_SvCustomVoteMenu)
				pSelf->ClearVotes(ClientId);
		}
	}
}

void CGameContext::ConInsertMapEntry(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pMap = pResult->GetString(0);
	const char *pServer = pResult->GetString(1);
	const char *pMapper = pResult->GetString(2);
	int Points = pResult->GetInteger(3);
	int Stars = pResult->GetInteger(4);
	char aTimestamp[32];
	if(pResult->NumArguments() > 5)
		str_copy(aTimestamp, pResult->GetString(5), sizeof(aTimestamp));
	else
	{
		time_t Now = time(0);
		str_timestamp_ex(Now, aTimestamp, sizeof(aTimestamp), "%Y-%m-%d");
	}

	if(!pMap)
	{
		log_info("score", "ConInsertMapEntry: no map specified");
		return;
	}
	if(!pServer)
	{
		log_info("score", "ConInsertMapEntry: no server specified");
		return;
	}
	if(!pMapper)
	{
		log_info("score", "ConInsertMapEntry: no mapper(s) specified");
		return;
	}

	pSelf->Score()->InsertMapEntry(pMap, pServer, pMapper, Points, Stars, aTimestamp);
}

void CGameContext::ConInsertRecord(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	const char *pMap = pResult->GetString(1);
	float Time = pResult->GetFloat(2);
	pSelf->Score()->InsertPlayerRecord(pResult->m_ClientId, pName, pMap, Time);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}

void CGameContext::ConRemoveRecord(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	const char *pMap = pResult->GetString(1);
	pSelf->Score()->RemovePlayerRecords(pName, pMap);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}
void CGameContext::ConRemoveRecordWithTime(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	const char *pMap = pResult->GetString(1);
	float Time = pResult->GetFloat(2);
	pSelf->Score()->RemovePlayerRecordWithTime(pName, pMap, Time);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}

void CGameContext::ConRemoveAllRecords(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pName = pResult->GetString(0);
	pSelf->Score()->RemoveAllPlayerRecords(pName);

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(pSelf->Server()->ClientIngame(ClientId) && !str_comp(pName, pSelf->Server()->ClientName(ClientId)))
		{
			pSelf->Score()->PlayerData(ClientId)->Reset();
			pSelf->Score()->LoadPlayerData(ClientId);
			return;
		}
	}
}

void CGameContext::ConDropWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
		return;

	vec2 Dir = normalize(vec2(pChr->Input()->m_TargetX, pChr->Input()->m_TargetY));
	int Type = pChr->Core()->m_ActiveWeapon;

	pChr->DropWeapon(Type, pChr->Core()->m_Vel * 0.7f + Dir * vec2(5.0f, 6.0f));
}

void CGameContext::ConCleanDroppedPickups(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
		if(pPlayer)
			pPlayer->m_vPickupDrops.clear();
	}
	// clean all existing pickupdrops
	pSelf->m_World.RemoveEntities(CGameWorld::ENTTYPE_PICKUPDROP);
}

void CGameContext::ConNewPickupDrop(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
		return;

	vec2 Pos = pChr->m_Pos;
	vec2 Dir = vec2(0, 0);
	int TeleCheck = pChr->m_TeleCheckpoint;
	int Team = pChr->Team();
	int Type = pResult->GetInteger(0);

	int Lifetime = pSelf->Server()->TickSpeed() * 300; // 5 minutes

	new CPickupDrop(&pSelf->m_World, -1, Pos, Team, TeleCheck, Dir, Lifetime, Type);
}

void CGameContext::ConRepredict(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!pResult->NumArguments())
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "You need to specify your client prediction margin");
		pSelf->SendChatTarget(pResult->m_ClientId, "10 is the default.");
		return;
	}

	int PredMargin = pResult->GetInteger(0);

	pPlayer->Repredict(PredMargin);
}

void CGameContext::ConSetBet(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;

	if(!g_Config.m_SvAccounts)
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!pPlayer->Acc()->m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You need to be logged in for this");
		return;
	}

	if(pPlayer->GetArea() != AREA_ROULETTE)
	{
		pSelf->SendChatTarget(ClientId, "You need to be in an area where you can place a bet");
		return;
	}

	const int Amount = pResult->GetInteger(0);
	const int Money = pPlayer->Acc()->m_Money;
	if(Amount > Money)
	{
		pSelf->SendChatTarget(ClientId, "You don't have enough money to place that bet");
		return;
	}

	if(Amount <= 0)
		return;
	if(pPlayer->m_BetAmount == Amount)
		return;

	char aBuf[64];
	if(pPlayer->m_BetAmount <= 0)
		str_format(aBuf, sizeof(aBuf), "You wagered %d%s", Amount, g_Config.m_SvCurrencyName);
	else
		str_format(aBuf, sizeof(aBuf), "You changed your wager to %d%s", Amount, g_Config.m_SvCurrencyName);

	pSelf->SendChatTarget(ClientId, aBuf);

	pPlayer->m_BetAmount = Amount;
	pPlayer->m_LastBet = pSelf->Server()->Tick();
}

void CGameContext::ConReport(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(!CheckClientId(ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(!g_Config.m_DcReportsWebhookUrl[0] || !g_Config.m_SvAccounts)
	{
		pSelf->SendChatTarget(ClientId, "Reporting is not enabled on this server.");
		return;
	}

	const char *pFrom = pSelf->Server()->ClientName(ClientId);
	const char *pAgainst = pResult->GetString(0);
	const char *pReportText = pResult->GetString(1);

	if(!str_comp_nocase(pAgainst, pFrom))
	{
		pSelf->SendChatTarget(ClientId, "You can't report yourself.");
		return;
	}

	if(!pPlayer->Acc()->m_LoggedIn)
	{
		pSelf->SendChatTarget(ClientId, "You need to be logged in for this.");
		return;
	}

	if(pPlayer->m_LastReport > 0 && pPlayer->m_LastReport + pSelf->Server()->TickSpeed() * 60 > pSelf->Server()->Tick())
	{
		pSelf->SendChatTarget(ClientId, "You need to wait a bit until you can report.");
		return;
	}

	bool Found = false;
	CPlayer *pAgainstPl = nullptr;
	for(int ClientId2 = 0; ClientId2 < MAX_CLIENTS; ClientId2++)
	{
		if(!pSelf->Server()->ClientIngame(ClientId2))
			continue;

		if(!str_comp(pSelf->Server()->ClientName(ClientId2), pAgainst))
		{
			Found = true;
			pAgainstPl = pSelf->m_apPlayers[ClientId2];
			break;
		}
	}
	if(!Found)
	{
		pSelf->SendChatTarget(ClientId, "Player not found.");
		return;
	}

	char aBuf[256];

	str_format(aBuf, sizeof(aBuf), "From: `%s` (Acc: `%s`)\n"
				       "against: `%s` (Acc: `%s`)\n"
				       "\n"
				       "Reason: %s\n"
				       "\n"
				       "-----------------------------------",
		pFrom,
		pPlayer->Acc()->m_aUsername,
		pAgainst,
		pAgainstPl ? pAgainstPl->Acc()->m_aUsername : "No Account",
		pReportText);

	char aNameBuf[32] = "";
	str_format(aNameBuf, sizeof(aNameBuf), "Player Report (Port: %d)", pSelf->Server()->Port());

	pSelf->Server()->SendWebhookMessage(g_Config.m_DcReportsWebhookUrl, aBuf, aNameBuf);

	pPlayer->m_LastReport = pSelf->Server()->Tick();
	pSelf->SendChatTarget(ClientId, "Report sent!");
}

void CGameContext::ConLaserText(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;

	if(!CheckClientId(ClientId))
		return;

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pChr)
		return;

	const vec2 Pos = pChr->m_Pos + vec2(0, -100);

	const char *pText = pResult->NumArguments() ? pResult->GetString(0) : "noob";

	new CLaserText(&pSelf->m_World, Pos, ClientId, 250, pText);
}

void CGameContext::ConProjectileText(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;

	if(!CheckClientId(ClientId))
		return;

	CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
	if(!pChr)
		return;

	const vec2 Pos = pChr->m_Pos + vec2(0, -60);
	const char *pText = pResult->GetString(0);
	new CProjectileText(&pSelf->m_World, Pos, ClientId, 250, pText, WEAPON_HAMMER);
}

void CGameContext::ConSendAsPlayer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->GetVictim();

	if(!CheckClientId(ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const char *pText = pResult->GetString(1);

	if(pText[0] == '/')
	{
		const char *pWhisper;
		if((pWhisper = str_startswith_nocase(pText + 1, "w ")))
		{
			pSelf->Whisper(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else if((pWhisper = str_startswith_nocase(pText + 1, "whisper ")))
		{
			pSelf->Whisper(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else if((pWhisper = str_startswith_nocase(pText + 1, "c ")))
		{
			pSelf->Converse(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else if((pWhisper = str_startswith_nocase(pText + 1, "converse ")))
		{
			pSelf->Converse(pPlayer->GetCid(), const_cast<char *>(pWhisper));
		}
		else
		{
			if(g_Config.m_SvSpamprotection && !str_startswith(pText + 1, "timeout ") && pPlayer->m_aLastCommands[0] && pPlayer->m_aLastCommands[0] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick() && pPlayer->m_aLastCommands[1] && pPlayer->m_aLastCommands[1] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick() && pPlayer->m_aLastCommands[2] && pPlayer->m_aLastCommands[2] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick() && pPlayer->m_aLastCommands[3] && pPlayer->m_aLastCommands[3] + pSelf->Server()->TickSpeed() > pSelf->Server()->Tick())
				return;

			int64_t Now = pSelf->Server()->Tick();
			pPlayer->m_aLastCommands[pPlayer->m_LastCommandPos] = Now;
			pPlayer->m_LastCommandPos = (pPlayer->m_LastCommandPos + 1) % 4;

			pSelf->Console()->SetFlagMask(CFGFLAG_CHAT);
			pSelf->Console()->ExecuteLine(pText + 1, ClientId, false);

			// m_apPlayers[ClientId] can be NULL, if the player used a
			// timeout code and replaced another client.
			// log_info("chat-command", "%d used %s", ClientId, pText);

			pSelf->Console()->SetFlagMask(CFGFLAG_SERVER);
		}
	}
	else
	{
		// pPlayer->UpdatePlaytime();
		char aCensoredMessage[256];
		pSelf->CensorMessage(aCensoredMessage, pText, sizeof(aCensoredMessage));
		pSelf->SendChat(ClientId, TEAM_ALL, aCensoredMessage, ClientId);
	}
}

void CGameContext::ConBotClientDetectionAdd(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const char *pClientName = pResult->GetString(0);
	const char *pDDNetVersionStr = pResult->GetString(1);
	int ClientVersion = pResult->GetInteger(2);
	int Ban = pResult->NumArguments() > 3 ? pResult->GetInteger(3) : 0;

	CBotClientDetection NewEntry;
	str_copy(NewEntry.m_pClientName, pClientName);
	str_copy(NewEntry.m_pDDNetVersionStr, pDDNetVersionStr);
	NewEntry.m_DDNetVersion = ClientVersion;
	NewEntry.m_Ban = Ban;

	for(const auto &Entry : pSelf->m_vBotClientDetections)
	{
		if(str_comp(Entry.m_pClientName, pClientName) == 0 &&
			str_comp(Entry.m_pDDNetVersionStr, pDDNetVersionStr) == 0 &&
			Entry.m_DDNetVersion == ClientVersion)
		{
			log_info("bot-detection", "Entry already exists");
			return;
		}
	}

	pSelf->m_vBotClientDetections.emplace_back(NewEntry);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pSelf->Server()->ClientIngame(i))
			continue;
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if(pPlayer)
			pPlayer->m_BotChecked = false; // force recheck
	}
}

void CGameContext::ConBotClientDetectionClear(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_vBotClientDetections.clear();
}

void CGameContext::ConBotClientDetectionList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	for(const auto &Entry : pSelf->m_vBotClientDetections)
	{
		const char *pClientName = Entry.m_pClientName[0] ? Entry.m_pClientName : "<any>";
		const char *pDDNetVersionStr = Entry.m_pDDNetVersionStr[0] ? Entry.m_pDDNetVersionStr : "<any>";
		int DDNetVersion = Entry.m_DDNetVersion;

		log_info("bot-detection", "Client: %s (%d) [%s]", pClientName, DDNetVersion, pDDNetVersionStr);
	}
}

void CGameContext::ConPlaySoundGlobal(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Sound = pResult->NumArguments() ? pResult->GetInteger(0) : -1;

	if(Sound < 0 || Sound >= NUM_SOUNDS)
	{
		log_info("foxnet", "0  = Gun Fire");
		log_info("foxnet", "1  = Shot Gun Fire");
		log_info("foxnet", "2  = Grenade Fire");
		log_info("foxnet", "3  = Hammer Fire");
		log_info("foxnet", "4  = Hammer Hit");
		log_info("foxnet", "5  = Ninja Fire");
		log_info("foxnet", "6  = Grenade Explosion");
		log_info("foxnet", "7  = Ninja Hit");
		log_info("foxnet", "8  = Laser Fire");
		log_info("foxnet", "9  = Laser Bounce");
		log_info("foxnet", "10 = Weapon Switch");
		log_info("foxnet", "11 = Player Pain Short");
		log_info("foxnet", "12 = Player Pain Long");
		log_info("foxnet", "13 = Body Land");
		log_info("foxnet", "14 = Player Airjump");
		log_info("foxnet", "15 = Player Jump");
		log_info("foxnet", "16 = Player Die");
		log_info("foxnet", "17 = Player Spawn");
		log_info("foxnet", "18 = Player Skid");
		log_info("foxnet", "19 = Tee Cry");
		log_info("foxnet", "20 = Hook Loop");
		log_info("foxnet", "21 = Hook Attach Ground");
		log_info("foxnet", "22 = Hook Attach Player");
		log_info("foxnet", "23 = Hook Sound No-Attach");
		log_info("foxnet", "24 = Pickup Health");
		log_info("foxnet", "25 = Pickup Armor");
		log_info("foxnet", "26 = Pickup Grenade");
		log_info("foxnet", "27 = Pickup Shotgun");
		log_info("foxnet", "28 = Pickup Ninja");
		log_info("foxnet", "29 = Weapon Spawn");
		log_info("foxnet", "30 = Weapon No-Ammo");
		log_info("foxnet", "31 = Hit");
		log_info("foxnet", "32 = Chat Server");
		log_info("foxnet", "33 = Chat Client");
		log_info("foxnet", "34 = Chat Highlight");
		log_info("foxnet", "35 = CTF Drop");
		log_info("foxnet", "36 = CTF Return");
		log_info("foxnet", "37 = CTF grab PL");
		log_info("foxnet", "37 = CTF grab PL");
		log_info("foxnet", "38 = CTF Grab EN");
		log_info("foxnet", "39 = CTF Flag Capture");
		log_info("foxnet", "40 = Menu");
		return;
	}

	pSelf->CreateSoundGlobal(Sound);
}

void CGameContext::RegisterFoxNetCommands()
{
	Console()->Register("playsound", "?i[sound_id]", CFGFLAG_SERVER, ConPlaySoundGlobal, this, "Play a sound globally for everyone");

	Console()->Register("bot_client_string_add", "s[client-name] s[version-str] i[client-ver] ?i[ban]", CFGFLAG_SERVER, ConBotClientDetectionAdd, this, "Add a string to bot client detection");
	Console()->Register("bot_client_strings_clear", "", CFGFLAG_SERVER, ConBotClientDetectionClear, this, "clear bot client detection strings");
	Console()->Register("bot_client_strings_list", "", CFGFLAG_SERVER, ConBotClientDetectionList, this, "List bot client detection strings");

	Console()->Register("send_as", "v[id] r[message]", CFGFLAG_SERVER, ConSendAsPlayer, this, "Send a chat message as player (id)");

	Console()->Register("lasertext", "r[string]", CFGFLAG_SERVER, ConLaserText, this, "laser text");
	Console()->Register("projectiletext", "r[string]", CFGFLAG_SERVER, ConProjectileText, this, "projectile text");

	Console()->Register("chat_string_add", "s[string] s[reason] i[should Ban] i[bantime] ?f[addition]", CFGFLAG_SERVER, ConAddChatDetectionString, this, "Add a string to the chat detection list");
	Console()->Register("chat_string_remove", "s[name]", CFGFLAG_SERVER, ConRemoveChatDetectionString, this, "Remove a string from the chat detection list");
	Console()->Register("chat_strings_list", "", CFGFLAG_SERVER, ConListChatDetectionStrings, this, "List all strings on the list");
	Console()->Register("chat_string_clear", "", CFGFLAG_SERVER, ConClearChatDetectionStrings, this, "Clear all strings on the list");

	Console()->Register("name_string_add", "s[name] s[reason] i[bantime] ?i[exact name]", CFGFLAG_SERVER, ConAddNameDetectionString, this, "Add a string to the name detection list");
	Console()->Register("name_string_remove", "s[name]", CFGFLAG_SERVER, ConRemoveNameDetectionString, this, "Remove a string from the name detection list");
	Console()->Register("name_strings_list", "", CFGFLAG_SERVER, ConListNameDetectionStrings, this, "List all strings on the list");
	Console()->Register("name_string_clear", "", CFGFLAG_SERVER, ConClearNameDetectionStrings, this, "Clear all strings on the list");

	Console()->Register("snake", "?v[id]", CFGFLAG_SERVER, ConSnake, this, "Makes a player (id) a Snake");
	Console()->Register("ufo", "?v[id]", CFGFLAG_SERVER, ConSetUfo, this, "Puts player (id) into an UFO");

	Console()->Register("set_name", "v[id] s[name]", CFGFLAG_SERVER, ConSetPlayerName, this, "Set a players (id) Name");
	Console()->Register("set_clan", "v[id] s[clan]", CFGFLAG_SERVER, ConSetPlayerClan, this, "Set a players (id) Clan");
	Console()->Register("set_skin", "v[id] s[skin]", CFGFLAG_SERVER, ConSetPlayerSkin, this, "Set a players (id) Skin");
	Console()->Register("set_custom_color", "v[id] i[int]", CFGFLAG_SERVER, ConSetPlayerCustomColor, this, "Whether a player (id) uses custom color (1 = true | 0 = false)");
	Console()->Register("set_color_body", "v[id] i[color]", CFGFLAG_SERVER, ConSetPlayerColorBody, this, "Set a players (id) Body Color");
	Console()->Register("set_color_feet", "v[id] i[color]", CFGFLAG_SERVER, ConSetPlayerColorFeet, this, "Set a players (id) Feet Color");
	Console()->Register("set_afk", "v[id] ?i[afk]", CFGFLAG_SERVER, ConSetPlayerAfk, this, "Set a players (id) afk status");

	Console()->Register("set_ability", "i[ability] ?v[id]", CFGFLAG_SERVER, ConSetAbility, this, "Set a players (id) Ability");

	Console()->Register("ignore_gamelayer", "?v[id]", CFGFLAG_SERVER, ConIgnoreGameLayer, this, "Turns off the kill-border for (id)");
	Console()->Register("invisible", "?v[id]", CFGFLAG_SERVER, ConInvisible, this, "Makes a players (id) Invisible");
	Console()->Register("vanish", "?v[id]", CFGFLAG_SERVER, ConSetVanish, this, "Completely hide player (id) from everyone on the server");
	Console()->Register("vanish_quiet", "?v[id]", CFGFLAG_SERVER, ConSetVanishQuiet, this, "Completely hide player (id) from everyone on the server without the chat join/leave message");
	Console()->Register("include_serverinfo", "v[id] ?i[include]", CFGFLAG_SERVER, ConIncludeInServerInfo, this, "whether a player should be in the serverinfo (true by default for everyone)");
	Console()->Register("redirect", "v[id] i[port]", CFGFLAG_SERVER, ConRedirectClient, this, "Redirect player (id) to a different Server (port)");

	Console()->Register("passive", "?v[id]", CFGFLAG_SERVER, ConSetPassive, this, "Put player (id) into passive");
	Console()->Register("hittable", "?v[id]", CFGFLAG_SERVER, ConSetHittable, this, "whether player (id) can be hit by other players");
	Console()->Register("hookable", "?v[id]", CFGFLAG_SERVER, ConSetHookable, this, "whether player (id) can be hooked by other players");
	Console()->Register("collidable", "?v[id]", CFGFLAG_SERVER, ConSetCollidable, this, "whether player (id) can collide with others");

	Console()->Register("set_tune_override", "i[zone] ?v[id]", CFGFLAG_SERVER, ConSetTuneOverride, this, "Sets the tune override for the player (id)");

	Console()->Register("telekinesis_immunity", "?v[id]", CFGFLAG_SERVER, ConTelekinesisImmunity, this, "Makes player (id) immunte to telekinesis");

	Console()->Register("telekinesis", "?v[id]", CFGFLAG_SERVER, ConTelekinesis, this, "Gives/Takes telekinses to/from player (id)");
	Console()->Register("heartgun", "?v[id]", CFGFLAG_SERVER, ConHeartGun, this, "Gives/Takes a heartgun to/from player (id)");
	Console()->Register("lightsaber", "?v[id]", CFGFLAG_SERVER, ConLightsaber, this, "Gives/Takes a lightsaber to/from player (id)");
	Console()->Register("portalgun", "?v[id]", CFGFLAG_SERVER, ConPortalGun, this, "Gives/Takes the portal gun to/from player (id)");

	Console()->Register("obfuscate", "?v[id]", CFGFLAG_SERVER, ConSetObfuscated, this, "Makes players (id) name obfuscated");
	Console()->Register("spider_hook", "?v[id]", CFGFLAG_SERVER, ConSetSpiderHook, this, "whether player (id) has spider hook");
	Console()->Register("spazzing", "?v[id]", CFGFLAG_SERVER, ConSetSpazzing, this, "Makes players (id) spazzing");

	Console()->Register("fake_message", "s[name] r[msg]", CFGFLAG_SERVER, ConSendFakeMessage, this, "Sends a message as a fake player with that name");

	Console()->Register("map_vote_lock", "", CFGFLAG_SERVER, ConToggleMapVoteLock, this, "Toggle Map Vote Locking");

	// Cosmetics
	Console()->Register("c_lovely", "?v[id]", CFGFLAG_SERVER, ConLovely, this, "Makes a player (id) Lovely");
	Console()->Register("c_epic_circle", "?v[id]", CFGFLAG_SERVER, ConEpicCircle, this, "Gives a player (id) an Epic Circle");
	Console()->Register("c_rotating_ball", "?v[id]", CFGFLAG_SERVER, ConRotatingBall, this, "Gives a player (id) a Rotating Ball");
	Console()->Register("c_bloody", "?v[id]", CFGFLAG_SERVER, ConBloody, this, "Gives a player (id) the Bloody Effect");
	Console()->Register("c_strongbloody", "?v[id]", CFGFLAG_SERVER, ConStrongBloody, this, "Gives a player (id) the Strong Bloody Effect");
	Console()->Register("c_sparkle", "?v[id]", CFGFLAG_SERVER, ConSparkle, this, "Gives a player (id) the Sparkle");
	Console()->Register("c_inverse_aim", "?v[id]", CFGFLAG_SERVER, ConInverseAim, this, "Makes a players (id) aim be inversed");
	Console()->Register("c_heart_hat", "?v[id]", CFGFLAG_SERVER, ConHeartHat, this, "Gives a player (id) a heart hat");
	Console()->Register("c_hookpower", "?i[power] ?v[id]", CFGFLAG_SERVER, ConHookPower, this, "Sets hook power for player (id)");

	Console()->Register("c_staff_ind", "?v[id]", CFGFLAG_SERVER, ConStaffInd, this, "Gives a player (id) a Staff Indicator");
	Console()->Register("c_pickuppet", "?v[id]", CFGFLAG_SERVER, ConSetPickupPet, this, "Gives player (id) a pet");
	Console()->Register("c_lissajous", "?v[id]", CFGFLAG_SERVER, ConLissajous, this, "Gives player (id) a lissajous figure");

	Console()->Register("c_star_trail", "?v[id]", CFGFLAG_SERVER, ConStarTrail, this, "Gives a player (id) a Star Trail");
	Console()->Register("c_dot_trail", "?v[id]", CFGFLAG_SERVER, ConDotTrail, this, "Gives a player (id) a Dot Trail");

	Console()->Register("c_rainbow_body", "?v[id]", CFGFLAG_SERVER, ConRainbowBody, this, "Makes a players (id) Body Rainbow");
	Console()->Register("c_rainbow_feet", "?v[id]", CFGFLAG_SERVER, ConRainbowFeet, this, "Makes a players (id) Feet Rainbow");
	Console()->Register("c_rainbow_speed", "?v[id] ?i[speed]", CFGFLAG_SERVER, ConRainbowSpeed, this, "Makes a players (id) Rainbow");

	Console()->Register("c_phase_gun", "?v[id]", CFGFLAG_SERVER, ConPhaseGun, this, "Gives player (id) a gun that shoots trough walls");
	Console()->Register("c_emote_gun", "i[type] ?v[id]", CFGFLAG_SERVER, ConSetEmoticonGun, this, "Set a players (id) Emoticon Gun to i[type] (1-12)");
	Console()->Register("c_confetti_gun", "?v[id]", CFGFLAG_SERVER, ConSetConfettiGun, this, "Set a players (id) Gun to shoot confetti");

	Console()->Register("c_death_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConDeathEffect, this, "Set players (id) Death Type");
	Console()->Register("c_damageind_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConDamageIndType, this, "Set players (id) Damage Ind Type");
	Console()->Register("c_gun_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConGunType, this, "Set players (id) Gun Type");
	Console()->Register("c_hat_type", "i[type] ?v[id]", CFGFLAG_SERVER, ConHatType, this, "Set players (id) Hat Type");

	// Player configs
	// Console()->Register("hide_cosmetics", "?v[id]", CFGFLAG_SERVER, ConHideCosmetics, this, "Hides Cosmetics for Player (id)");
	Console()->Register("hide_powerups", "?v[id]", CFGFLAG_SERVER, ConHidePowerUps, this, "Hides Powerups for Player (id)");

	// Records
	Console()->Register("insert_map_entry", "s[map] s[server] s[mapper] i[points] i[stars] ?r[timestamp]", CFGFLAG_SERVER, ConInsertMapEntry, this, "Insert a new map entry into the ddnet_maps sql table");

	Console()->Register("record_insert", "s[name] s[map] f[time]", CFGFLAG_SERVER, ConInsertRecord, this, "Insert a new record for that name on the given map with given time");
	Console()->Register("record_remove", "s[name] r[map]", CFGFLAG_SERVER, ConRemoveRecord, this, "Remove all records a name has on the given map");
	Console()->Register("record_remove_time", "s[name] s[map] f[time]", CFGFLAG_SERVER, ConRemoveRecordWithTime, this, "Remove records a name has on given map with given time");
	Console()->Register("record_remove_all", "r[name]", CFGFLAG_SERVER, ConRemoveAllRecords, this, "Remove all records a name has");

	// Account
	Console()->Register("force_login", "r[username] ?v[id]", CFGFLAG_SERVER, ConAccForceLogin, this, "Force Login player (id) into any account");
	Console()->Register("force_logout", "i[id]", CFGFLAG_SERVER, ConAccForceLogout, this, "Force logout an account thats currently active on the server");
	Console()->Register("acc_disable", "s[username] ?i[disable]", CFGFLAG_SERVER, ConAccDisable, this, "Disable an account");
	Console()->Register("acc_password", "s[username] r[variable]", CFGFLAG_SERVER, ConAccForcePassword, this, "Disable an account");
	Console()->Register("give_money", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveMoney, this, "Give player (id) money");
	Console()->Register("give_xp", "v[id] i[amount]", CFGFLAG_SERVER, ConGiveXp, this, "Give player (id) xp");
	Console()->Register("remove_item", "v[id] r[item]", CFGFLAG_SERVER, ConRemoveItem, this, "remove an item from player (id)");
	Console()->Register("give_item", "v[id] r[item]", CFGFLAG_SERVER, ConGiveItem, this, "Give player (id) an item");
	Console()->Register("give_item_days", "v[id] i[days] r[item]", CFGFLAG_SERVER, ConGiveItemDays, this, "Give player (id) an item for x days");
	Console()->Register("give_item_forever", "v[id] r[item]", CFGFLAG_SERVER, ConGiveItemForever, this, "Give player (id) an item forever");

	Console()->Register("new_mail", "s[username] s[subject] s[message] s[cmd_name] r[cmd]", CFGFLAG_SERVER, ConNewMail, this, "Send a new mail");
	Console()->Register("new_global_mail", "s[subject] s[message] s[cmd_name] s[cmd] ?i[min_level] i?[only-online] i?[include-disabled]", CFGFLAG_SERVER, ConNewGlobalMail, this, "Send a new mail");

	Console()->Register("register", "s[username] s[password]", CFGFLAG_CHAT, ConAccRegister, this, "Register a account");
	Console()->Register("password", "s[oldpass] s[password]", CFGFLAG_CHAT, ConAccPassword, this, "Change your password");
	Console()->Register("login", "s[username] r[password]", CFGFLAG_CHAT, ConAccLogin, this, "Login to your account");
	Console()->Register("logout", "", CFGFLAG_CHAT, ConAccLogout, this, "Logout of your account");
	Console()->Register("profile", "?r[name]", CFGFLAG_CHAT, ConAccProfile, this, "Show someones profile");

	Console()->Register("top5money", "?i[offset]", CFGFLAG_CHAT, ConAccTop5Money, this, "Show someones profile");
	Console()->Register("top5level", "?i[offset]", CFGFLAG_CHAT, ConAccTop5Level, this, "Show someones profile");
	Console()->Register("top5playtime", "?i[offset]", CFGFLAG_CHAT, ConAccTop5Playtime, this, "Show someones profile");

	Console()->Register("bet", "i[amount]", CFGFLAG_SERVER | CFGFLAG_CHAT, ConSetBet, this, "place a bet on the roulette");

	Console()->Register("report", "s[player] r[message]", CFGFLAG_SERVER | CFGFLAG_CHAT, ConReport, this, "Report a player");
	// Shop
	Console()->Register("shop_edit_item", "s[Name] i[Price] ?i[Minimum Level]", CFGFLAG_SERVER, ConShopEditItem, this, "Edit a shop item");
	Console()->Register("shop_list_items", "", CFGFLAG_SERVER, ConShopListItems, this, "Lists all shop items");
	Console()->Register("shop_reset", "", CFGFLAG_SERVER, ConShopReset, this, "Resets all prices in the shop");

	Console()->Register("buyitem", "r[item]", CFGFLAG_CHAT, ConShopBuyItem, this, "Buy an item from the shop");
	Console()->Register("toggleitem", "s[item] ?i[value]", CFGFLAG_CHAT, ConToggleItem, this, "Toggle an Item, value is only needed for 2 items");
	Console()->Register("dropweapon", "", CFGFLAG_CHAT, ConDropWeapon, this, "Drops the weapon you're currently holding");

	Console()->Register("cleanup_pickupdrops", "", CFGFLAG_SERVER, ConCleanDroppedPickups, this, "Removes all dropped pickups");
	Console()->Register("new_pickupdrop", "i[type]", CFGFLAG_SERVER, ConNewPickupDrop, this, "Spawns a new pickup drop on your position");

	Console()->Register("repredict", "?i[predmargin]", CFGFLAG_CHAT | CFGFLAG_SERVER, ConRepredict, this, "Recalculates the Server-Side prediction (based on Ping + pred margin)");
	
	Console()->Chain("sv_debug_quad_pos", ConchainQuadDebugPos, this);
	Console()->Chain("sv_solo_on_spawn", ConchainSoloOnSpawn, this);
	Console()->Chain("sv_cosmetics", ConchainCosmetics, this);
	Console()->Chain("sv_accounts", ConchainAccounts, this);
	Console()->Chain("sv_custom_vote_menu", ConchainResendVoteMenu, this);
	Console()->Chain("sv_accounts_forced", ConchainAccountsForced, this);

	Console()->Chain("unban", ConchainScriptingBan, this);
	Console()->Chain("ban", ConchainScriptingBan, this);
	Console()->Chain("ban_timestamp", ConchainScriptingBan, this);
}

void CGameContext::ConchainScriptingBan(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CGameContext *pSelf = (CGameContext *)pUserData;
	int NumArgs = pResult->NumArguments();
	if(!pResult->NumArguments())
		return;
	int UserId = pResult->m_ClientId;
	if(!CheckClientId(UserId) && UserId != IConsole::CLIENT_ID_FOXNET)
		return;

	char aCmdBuf[1028];
	char aArgBuf[256] = "";
	for(int i = 0; i < NumArgs; i++)
	{
		if(i > 0)
			str_append(aArgBuf, " ", sizeof(aArgBuf));
		str_append(aArgBuf, pResult->GetString(i), sizeof(aArgBuf));
	}
	str_format(aCmdBuf, sizeof(aCmdBuf), "%s %s", pResult->GetCommand(), aArgBuf);
	pSelf->FormatAndRunScriptingBan(aCmdBuf, UserId);
}

void CGameContext::FormatAndRunScriptingBan(const char *pStr, int UserId)
{
	if(!CheckClientId(UserId) && UserId != IConsole::CLIENT_ID_FOXNET)
		return;

	if(g_Config.m_SvScriptPlayerBans[0])
	{
		// (un)ban ip minutes reason
		char aScriptingArgs[256] = "";
		NETADDR Addr;
		char aAddrStr[NETADDR_MAXSTRSIZE] = "";
		const char *pArg = GetParsedArgument(pStr, 1, false);
		if(!pArg)
			return;

		if(str_startswith(pStr, "unban "))
		{
			if(str_isallnum(pArg))
			{
				int Index = str_toint(pArg);
				const NETADDR *pTempAddr = Server()->GetAddrFromBanIndex(Index);
				if(pTempAddr)
					net_addr_str(pTempAddr, aAddrStr, sizeof(aAddrStr), false);
			}
			else if(!net_addr_from_str(&Addr, pArg))
				str_copy(aAddrStr, pArg, sizeof(aAddrStr));

			if(aAddrStr[0])
			{
				str_format(aScriptingArgs, sizeof(aScriptingArgs), "unban %s \"\" \"\"", aAddrStr);
			}
		}
		else if(str_startswith(pStr, "ban ") || str_startswith(pStr, "ban_timestamp "))
		{
			if(str_isallnum(pArg))
			{
				int ClientId = str_toint(pArg);
				if(ClientId == UserId)
					return; // prevent self ban

				const NETADDR *pTempAddr = Server()->GetAddrFromBanIndex(-1);
				if(pTempAddr)
					net_addr_str(pTempAddr, aAddrStr, sizeof(aAddrStr), false);
			}
			else if(!net_addr_from_str(&Addr, pArg))
				str_copy(aAddrStr, pArg, sizeof(aAddrStr));

			if(aAddrStr[0])
			{
				const char *pMinutesStr = GetParsedArgument(pStr, 2, false);
				long Minutes = (pMinutesStr && str_isallnum(pMinutesStr)) ? (long)str_toint(pMinutesStr) : 5;
				const char *pReason = GetParsedArgument(pStr, 3, true);
				if(!pReason)
					pReason = "No Reason Provided.";
				str_format(aScriptingArgs, sizeof(aScriptingArgs), "ban %s %ld \"%s\"", aAddrStr, Minutes, pReason);
			}
		}

		if(!aScriptingArgs[0])
			return;

		char aScriptingBuf[256];
		str_format(aScriptingBuf, sizeof(aScriptingBuf), "chai %s %s", g_Config.m_SvScriptPlayerBans, aScriptingArgs);
		Console()->ExecuteLine(aScriptingBuf, IConsole::CLIENT_ID_UNSPECIFIED);
	}
}

void CGameContext::ConchainQuadDebugPos(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		pSelf->QuadDebugIds(g_Config.m_SvDebugQuadPos);
	}
}

void CGameContext::ConchainSoloOnSpawn(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments() || !pResult->GetInteger(0))
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CCharacter *pChr = pSelf->GetPlayerChar(ClientId);
		if(pChr && pChr->m_SpawnSolo)
			pChr->UnSpawnSolo();
	}
}

void CGameContext::ConchainCosmetics(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool Value = pResult->GetInteger(0) != 0;
	if(!Value)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
			if(pPlayer)
			{
				pPlayer->DisableAllCosmetics();
			}
		}
	}
}

void CGameContext::ConchainAccounts(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool Value = pResult->GetInteger(0) != 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
		if(pPlayer)
		{
			if(!Value)
			{
				pPlayer->SetPage(PAGE_MAIN);
				if(pPlayer->Acc()->m_LoggedIn && pSelf->m_AccountManager.Logout(ClientId))
					pSelf->SendChatTarget(ClientId, "You have been logged out because accounts have been disabled on this server.");
			}
			else
				pSelf->m_AccountManager.AutoLogin(ClientId); // try to login all clients
		}
	}
}

void CGameContext::ConchainResendVoteMenu(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;

	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ClearVotes(-1);
}

void CGameContext::ConchainAccountsForced(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(!pResult->NumArguments())
		return;
	if(!g_Config.m_SvAccounts)
	{
		log_info("accounts", "Cannot force accounts when accounts are disabled. Disabling forced accounts.");
		g_Config.m_SvAccountsForced = 0;
		return;
	}

	CGameContext *pSelf = (CGameContext *)pUserData;

	int Value = pResult->GetInteger(0);

	if(Value)
	{
		for(CPlayer *pPl : pSelf->m_apPlayers)
		{
			if(!pPl)
				continue;
			if(pPl->GetTeam() == TEAM_SPECTATORS)
				continue;
			if(pPl->Acc()->m_LoggedIn)
				continue;
			pSelf->SendChatTarget(pPl->GetCid(), "You have been moved to spectators because accounts are now forced on this server.");
			pPl->SetTeam(TEAM_SPECTATORS, false);
		}
	}
}