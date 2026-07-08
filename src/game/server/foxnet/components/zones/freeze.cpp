#include "freeze.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

void CFreezeZone::OnTick()
{
	if(!GameServer()->GlobalTuning(MultiMapIndex())->m_MovingTiles)
		return;

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
			pChr->m_InsideQuadFreeze = false;

			if(!pChr->IsAlive())
				continue;
			if(pChr->Core()->m_IsInFreeze)
				continue;
			if(pChr->Core()->m_DeepFrozen)
				continue;
			if(pChr->Core()->m_LiveFrozen)
				continue;

			if(pChr->m_TileIndex == TILE_UNFREEZE || pChr->m_TileFIndex == TILE_UNFREEZE)
				continue;

			if(!InsideQuad(pChr->GetPos(), QuadData, vec2(0, 0)))
				continue;

			pChr->Freeze();
			pChr->m_InsideQuadFreeze = true;
		}
	}
}
