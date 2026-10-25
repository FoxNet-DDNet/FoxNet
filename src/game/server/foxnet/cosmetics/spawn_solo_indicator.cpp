// Made by qxdFox
#include "spawn_solo_indicator.h"

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>

CSpawnSoloIndicator::CSpawnSoloIndicator(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_SPAWN_SOLO_INDICATOR, Pos)
{
	GameWorld()->InsertEntity(this);
}

void CSpawnSoloIndicator::Reset()
{
	m_MarkedForDestroy = true;
}

void CSpawnSoloIndicator::Tick()
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter || !pCharacter->m_SpawnSolo)
	{
		Reset();
		return;
	}
	m_Pos = pCharacter->GetPos();
}

void CSpawnSoloIndicator::Snap(int SnappingClient)
{
	if(SnappingClient < 0 || SnappingClient >= MAX_CLIENTS || NetworkClipped(SnappingClient))
		return;
	if(!GetCharacter() || !GetCharacter()->m_SpawnSolo || !CanSnapEntity(SnappingClient))
		return;

	SnapCosmeticPickup(SnappingClient, GetId().value(), PICKUPFLAG_NO_PREDICT, m_Owner,
		vec2(0, -56.0f), POWERUP_ARMOR, 0, 0, -1, COSMETIC_FLAG_ANCHORED);
}
