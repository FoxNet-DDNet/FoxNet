#include "minigame.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <cstddef>

IMinigame::~IMinigame()
{
	// Players outlive the zones they stand in, a map reload must never leave one pointing at us
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;

		if(pPlayer->m_pMinigame == this)
			pPlayer->m_pMinigame = nullptr;
		if(pPlayer->m_pLastMinigame == this)
			pPlayer->m_pLastMinigame = nullptr;
	}
}

void IMinigame::AddAreaQuad(const CQuadData &QuadData)
{
	AddQuad(QuadData);
	// The index, not a copy: UpdateCache() animates the quads in place and the area has to move with them
	m_vAreaQuadIndices.push_back(m_vQuads.size() - 1);
}

bool IMinigame::IsInArea(int ClientId) const
{
	if(!CheckClientId(ClientId))
		return false;

	const CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	return pPlayer != nullptr && pPlayer->m_pMinigame == this;
}

bool IMinigame::ContainsPlayer(const CPlayer *pPlayer) const
{
	const CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return pPlayer->m_pMinigame == this; // nothing to test against, leave ownership as it is

	for(size_t Index : m_vAreaQuadIndices)
	{
		if(InsideQuad(pChr->GetPos(), m_vQuads[Index]))
			return true;
	}

	return false;
}
