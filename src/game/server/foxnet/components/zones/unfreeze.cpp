#include "unfreeze.h"

#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <algorithm>

void CUnfreezeZone::OnPostTick()
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

		/*
		 * The whole path of this tick, not only where it ended: a quad thinner than one tick of
		 * travel was otherwise passed straight through at speed. sv_quad_freeze_path 0 puts that
		 * back for a map built around phasing through one, and takes the original test with it
		 * rather than a path of no length, so the edges answer exactly as they used to.
		 */
		const bool Path = g_Config.m_SvQuadFreezePath != 0;
		const vec2 To = pChr->GetPos();
		const vec2 From = Path && pChr->QuadSweepTick() == Server()->Tick() ? pChr->QuadSweepFrom() : To;

		QueryQuads(
			vec2(std::min(From.x, To.x), std::min(From.y, To.y)),
			vec2(std::max(From.x, To.x), std::max(From.y, To.y)),
			m_vCandidates);

		for(int QuadId : m_vCandidates)
		{
			const CQuadData &QuadData = m_vQuads[QuadId];
			if(!(Path ? SegmentInsideQuad(From, To, QuadData) : InsideQuad(To, QuadData, vec2(0, 0))))
				continue;

			pChr->Unfreeze();
			pChr->m_InsideQuadFreeze = false;
		}
	}
}
