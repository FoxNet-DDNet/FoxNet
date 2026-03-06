#include "component.h"

#include "components/accounts/accounts.h"

#include <engine/console.h>
#include <engine/server.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

void CServerComponent::InitComponent(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
}
void CServerComponent::SendChatTarget(int ClientId, const char *pMessage)
{
	GameServer()->SendChatTarget(ClientId, pMessage);
}

IServer *CServerComponent::Server() const
{
	return m_pGameServer->Server();
}
IConsole *CServerComponent::Console() const
{
	return m_pGameServer->Console();
}

CPlayer *CServerComponent::GetPlayer(int ClientId)
{
	if(!CheckClientId(ClientId))
		return nullptr;
	if(!Server()->ClientIngame(ClientId))
		return nullptr;

	return GameServer()->m_apPlayers[ClientId];
}

CCharacter *CServerComponent::GetCharacter(int ClientId)
{
	if(!CheckClientId(ClientId))
		return nullptr;
	if(!Server()->ClientIngame(ClientId))
		return nullptr;

	return GameServer()->GetPlayerChar(ClientId);
}

CAccountSession *CServerComponent::GetAcc(int ClientId) const
{
	if(!CheckClientId(ClientId))
		return nullptr;
	if(!Server()->ClientIngame(ClientId))
		return nullptr;

	return &GameServer()->m_aAccounts[ClientId];
}
