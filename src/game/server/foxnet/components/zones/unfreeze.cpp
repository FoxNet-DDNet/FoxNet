#include "unfreeze.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

void CUnfreezeZone::OnTick()
{
	if(!GameServer()->GlobalTuning(MultiMapIndex())->m_MovingTiles)
		return;

	const int MapIdx = (int)MultiMapIndex();
	const int MaxClients = Server()->MaxClients();

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

		if(pChr->Core()->m_IsInFreeze)
			continue;

		if(pChr->Core()->m_DeepFrozen)
			continue;
		if(pChr->Core()->m_LiveFrozen)
			continue;

		if(pChr->m_FreezeTime == 0 && !pChr->m_InsideQuadFreeze)
			continue;

		for(const CQuadData &QuadData : Quads())
		{
			if(!InsideQuad(pChr->GetPos(), QuadData, vec2(0, 0)))
				continue;

			pChr->Unfreeze();
			pChr->m_InsideQuadFreeze = false;
		}
	}
}
