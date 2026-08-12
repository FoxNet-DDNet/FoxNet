#include "death.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <vector>

void CDeathZone::OnTick()
{
	if(!GameServer()->GlobalTuning(MultiMapIndex())->m_MovingTiles)
		return;

	const int MapIdx = (int)MultiMapIndex();
	const int MaxClients = Server()->MaxClients();

	for(const CQuadData &QuadData : Quads())
	{
		for(int ClientId = 0; ClientId < MaxClients; ClientId++)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(!pPlayer || !pPlayer->GetCharacter())
				continue;
			if(pPlayer->MultiMapIdx() != MapIdx)
				continue;
			CCharacter *pChr = pPlayer->GetCharacter();
			if(!pChr->IsAlive())
				continue;

			vec2 Size = vec2(pChr->GetProximityRadius(), pChr->GetProximityRadius());
			if(!InsideQuad(pChr->GetPos(), QuadData, Size / 3.0f))
				continue;

			pChr->Die(pPlayer->GetCid(), WEAPON_WORLD);
		}
		// Walking the list directly avoids EntitiesOfType() heap-allocating a copy of
		// every pickup drop pointer. Reset() only marks the entity for destruction --
		// the world unlinks it later -- but cache the next pointer regardless so the
		// walk cannot be derailed by the entity we just touched.
		CEntity *pNext = nullptr;
		for(CEntity *pEnt = GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_PICKUPDROP); pEnt; pEnt = pNext)
		{
			pNext = pEnt->TypeNext();

			if(pEnt->MultiMapIdx() != MapIdx)
				continue;

			vec2 Size = vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius());
			if(!InsideQuad(pEnt->GetPos(), QuadData, Size / 3.0f))
				continue;

			pEnt->Reset();
		}
	}
}
