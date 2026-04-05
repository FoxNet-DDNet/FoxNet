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

	std::vector<CEntity *> apEnts = GameServer()->m_World.EntitiesOfType(CGameWorld::ENTTYPE_PICKUPDROP);

	for(const CQuadData &QuadData : Quads())
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(!pPlayer || !pPlayer->GetCharacter())
				continue;
			if(pPlayer->MultiMapIdx() != (int)MultiMapIndex())
				continue;
			CCharacter *pChr = pPlayer->GetCharacter();
			if(!pChr->IsAlive())
				continue;

			vec2 Size = vec2(pChr->GetProximityRadius(), pChr->GetProximityRadius());
			if(!InsideQuad(pChr->GetPos(), QuadData, Size / 3.0f))
				continue;

			pChr->Die(pPlayer->GetCid(), WEAPON_WORLD);
		}
		for(CEntity *pEnt : apEnts)
		{
			if(pEnt->MultiMapIdx() != (int)MultiMapIndex())
				continue;

			if(!InsideQuad(pEnt->GetPos(), QuadData, vec2(0, 0)))
				continue;

			pEnt->Reset();
		}
	}
}
