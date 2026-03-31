#include "component.h"

#include "components/accounts/accounts.h"

#include <engine/console.h>
#include <engine/server.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/collision.h>

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
	return GameServer()->Server();
}
IConsole *CServerComponent::Console() const
{
	return GameServer()->Console();
}
CCollision *CServerComponent::Collision(size_t MultiMapIdx) const
{
	return GameServer()->Collision(MultiMapIdx);
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
	return &GameServer()->m_aAccounts[ClientId];
}

CMultiMaps *CServerComponent::MultiMaps(size_t Idx) const
{
	if(Idx >= GameServer()->m_vMultiMaps.size())
		return nullptr;

	return GameServer()->m_vMultiMaps[Idx].get();
}
