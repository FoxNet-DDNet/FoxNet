#include "freeze.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

void CFreezeZone::OnTick()
{
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

			pChr->m_InsideQuadFreeze = false;
			if(pChr->Core()->m_IsInFreeze)
				continue;

			vec2 Points[4] = {QuadData.m_Pos[0], QuadData.m_Pos[1], QuadData.m_Pos[3], QuadData.m_Pos[2]};
			if(!InsideQuad(pChr->GetPos(), Points, vec2(0, 0)))
				continue;

			pChr->Freeze();
			pChr->m_InsideQuadFreeze = true;
		}
	}
}
