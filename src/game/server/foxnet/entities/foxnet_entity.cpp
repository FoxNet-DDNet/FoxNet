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

CEntityOwned::CEntityOwned(CGameWorld *pGameWorld, int Owner, int Objtype, vec2 Pos, int ProximityRadius) :
	CEntity(pGameWorld, DefaultMapIndex, Objtype, Pos, ProximityRadius)
{
	m_Owner = Owner;

	if(GetCharacter())
		m_StartTeamMask = TeamMask();
	if(GetPlayer())
		SetMapIndex(GetPlayer()->MultiMapIdx());
}

bool CEntityOwned::CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer)
{
	if(m_MarkedForDestroy)
		return false;
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

CPlayer *CEntityOwned::GetPlayer()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id %d", m_Owner);
	// Should be dbg_assert but idk
	if(Server()->ClientSlotEmpty(m_Owner))
		return nullptr;
	return GameServer()->m_apPlayers[m_Owner];
}
CCharacter *CEntityOwned::GetCharacter()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id %d", m_Owner);
	// same
	if(Server()->ClientSlotEmpty(m_Owner))
		return nullptr;
	CPlayer *pPlayer = GetPlayer();
	if(!pPlayer)
		return nullptr;

	if(MultiMapIdx() != pPlayer->MultiMapIdx())
		SetMapIndex(pPlayer->MultiMapIdx());

	return pPlayer->GetCharacter();
}

CCollision *CEntityOwned::GetCollision()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id %d", m_Owner);
	// same
	if(Server()->ClientSlotEmpty(m_Owner))
		return Collision();
	CPlayer *pPlayer = GetPlayer();
	if(!pPlayer)
		return Collision();
	return GameServer()->Collision(pPlayer->MultiMapIdx());
}

CClientMask CEntityOwned::CosmeticMask(const EItemType ItemType)
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter)
		return CClientMask().set();

	return pCharacter->CosmeticMask(ItemType);
}

CClientMask CEntityOwned::TeamMask()
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter)
		return CClientMask().set();

	return pCharacter->TeamMask();
}
