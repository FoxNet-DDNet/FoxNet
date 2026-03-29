#include "fake_snap.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <game/server/foxnet/components/accounts/accounts.h>
#include <game/server/foxnet/item_registry.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>
#include <vector>

void CFakeSnap::ConSendFakeMessage(IConsole::IResult *pResult, void *pUserData)
{
	CFakeSnap *pSelf = (CFakeSnap *)pUserData;
	const char *pName = pResult->GetString(0);
	const char *pMsg = pResult->GetString(1);
	pSelf->AddFakeMessage(pName, pMsg, "Robot");
}

bool CFakeSnap::AddFakeMessage(const char *pName, const char *pMessage, const char *pSkinName, bool CustomColor, int ColorBody, int ColorFeet)
{
	if(!pName[0] || !pMessage[0])
		return false;

	static int LastUsedId = -1;

	int FreeId = -1;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!GetPlayer(ClientId) && Server()->ClientSlotEmpty(ClientId) && LastUsedId != ClientId)
		{
			FreeId = ClientId;
			break;
		}
	}
	if(FreeId == -1)
		return false; // no free visual slot
	LastUsedId = FreeId;
	CFakeSnapPlayer FakeSnap;
	FakeSnap.m_ClientId = FreeId;
	str_copy(FakeSnap.m_aName, pName);
	FakeSnap.m_aClan[0] = '\0';
	FakeSnap.m_Country = -1;
	str_copy(FakeSnap.m_aSkinName, pSkinName ? pSkinName : "default");
	FakeSnap.m_CustomColors = CustomColor;
	FakeSnap.m_ColorBody = ColorBody;
	FakeSnap.m_ColorFeet = ColorFeet;
	str_copy(FakeSnap.m_aMessage, pMessage);

	m_vFakeSnapPlayers.push_back(FakeSnap);
	return true;
}

void CFakeSnap::OnSnap(int ClientId, bool GlobalSnap, bool RecordingDemo)
{
	for(auto pFakePlayer = m_vFakeSnapPlayers.begin(); pFakePlayer < m_vFakeSnapPlayers.end();)
	{
		if(pFakePlayer->m_ClientId == -1)
		{
			pFakePlayer = m_vFakeSnapPlayers.erase(pFakePlayer);
			continue;
		}

		if(auto *pClientInfo = Server()->SnapNewItem<CNetObj_ClientInfo>(pFakePlayer->m_ClientId))
		{
			StrToInts(pClientInfo->m_aName, std::size(pClientInfo->m_aName), pFakePlayer->m_aName);
			StrToInts(pClientInfo->m_aClan, std::size(pClientInfo->m_aClan), pFakePlayer->m_aClan);
			pClientInfo->m_Country = pFakePlayer->m_Country;
			StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), pFakePlayer->m_aSkinName);
			pClientInfo->m_UseCustomColor = pFakePlayer->m_CustomColors;
			pClientInfo->m_ColorBody = pFakePlayer->m_ColorBody;
			pClientInfo->m_ColorFeet = pFakePlayer->m_ColorFeet;
		}

		if(auto *pPlayerInfo = Server()->SnapNewItem<CNetObj_PlayerInfo>(pFakePlayer->m_ClientId))
		{
			pPlayerInfo->m_Latency = 0;
			pPlayerInfo->m_Score = 0;
			pPlayerInfo->m_Team = TEAM_SPECTATORS;
			pPlayerInfo->m_Local = 0;
			pPlayerInfo->m_ClientId = pFakePlayer->m_ClientId;
		}
		pFakePlayer++;
	}
}

void CFakeSnap::OnPostGlobalSnap() // Send Message, player gets snapped in OnSnap()
{
	for(auto pFakePlayer = m_vFakeSnapPlayers.begin(); pFakePlayer < m_vFakeSnapPlayers.end(); pFakePlayer++)
	{
		const int ClientId = pFakePlayer->m_ClientId;
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;

		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientId = ClientId;
		Msg.m_pMessage = pFakePlayer->m_aMessage;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

		log_info(pFakePlayer->m_aContext, "%d:%d:%s: %s", ClientId, Msg.m_Team, pFakePlayer->m_aName, pFakePlayer->m_aMessage);

		pFakePlayer->m_ClientId = -1;
	}
}

void CFakeSnap::OnConsoleInit()
{
	Console()->Register("fake_message", "s[name] r[msg]", CFGFLAG_SERVER, ConSendFakeMessage, this, "Sends a message as a fake player with that name");
}
