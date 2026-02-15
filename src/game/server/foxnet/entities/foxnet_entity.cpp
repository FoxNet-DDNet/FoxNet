#include "foxnet_entity.h"

#include <base/dbg.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/item_registry.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

CFoxNetEntity::CFoxNetEntity(CGameWorld *pGameWorld, CCollision *pCollision, int Objtype, vec2 Pos, int ProximityRadius) :
	CEntity(pGameWorld, pCollision, Objtype, Pos, ProximityRadius) {}

bool CFoxNetEntity::CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer)
{
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;
	
	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pSnapPlayer || !GetCharacter())
		return false;

	if(GetCharacter()->IsPaused())
		return false;

	if(!TeamMask().test(SnappingClient))
		return false;

	if(pSnapPlayer->GetCharacter() && GetCharacter())
		if(!GetCharacter()->CanSnapCharacter(SnappingClient))
			return false;

	if(GetPlayer()->m_Vanish && SnappingClient != GetPlayer()->GetCid() && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return false;

	if(ppSnapPlayer)
		*ppSnapPlayer = pSnapPlayer;

	return true;
}

CPlayer *CFoxNetEntity::GetPlayer()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id");
	// Should be dbg_assert but idk
	if(Server()->ClientSlotEmpty(m_Owner))
		return nullptr;
	return GameServer()->m_apPlayers[m_Owner];
}
CCharacter *CFoxNetEntity::GetCharacter()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id");
	// same
	if(Server()->ClientSlotEmpty(m_Owner))
		return nullptr;
	CPlayer *pPlayer = GetPlayer();
	if(!pPlayer)
		return nullptr;
	return pPlayer->GetCharacter();
}

CClientMask CFoxNetEntity::CosmeticMask(const EItemType ItemType)
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter)
		return CClientMask();

	return pCharacter->CosmeticMask(ItemType);
}

CClientMask CFoxNetEntity::TeamMask()
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter)
		return CClientMask();

	return pCharacter->TeamMask();
}
