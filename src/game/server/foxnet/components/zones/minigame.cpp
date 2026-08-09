#include "minigame.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <cstddef>
#include <optional>

IMinigame::~IMinigame()
{
	for(int Id : m_vSnapIds)
		Server()->SnapFreeId(Id);
}

void IMinigame::SendMotd(int ClientId)
{
	if(!CheckClientId(ClientId))
		return;
	if(m_aSeenMotd[ClientId])
		return;

	const char *pMotd = Motd();
	if(pMotd[0] == '\0')
		return;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	// A dead character here means OnPlayerEnter turned them away, they never really got in
	if(!pChr || !pChr->IsAlive())
		return;
	if(pChr->Team() != TEAM_FLOCK)
		return;

	// One motd per player every 30 seconds, no matter how many zones they walk through
	if(pPlayer->m_LastMotd + Server()->TickSpeed() * 30 > Server()->Tick() && pPlayer->m_LastMotd > 0)
		return;

	m_aSeenMotd[ClientId] = true;
	pPlayer->m_LastMotd = Server()->Tick();

	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = pMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
	pPlayer->SendChat("How to view Area Info: Open Menu -> Server Info -> MOTD");
}

int IMinigame::AllocSnapId()
{
	std::optional<int> Id = Server()->SnapNewId();
	if(!Id.has_value())
		return -1;

	m_vSnapIds.push_back(Id.value());
	return Id.value();
}

void IMinigame::AddAreaQuad(const CQuadData &QuadData)
{
	AddQuad(QuadData);
	// The index, not a copy: UpdateCache() animates the quads in place and the area has to move with them
	m_vAreaQuadIndices.push_back(m_vQuads.size() - 1);
}

bool IMinigame::ContainsPlayer(const CPlayer *pPlayer) const
{
	const CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return IsInArea(pPlayer->GetCid()); // nothing to test against, leave ownership as it is

	for(size_t Index : m_vAreaQuadIndices)
	{
		if(InsideQuad(pChr->GetPos(), m_vQuads[Index]))
			return true;
	}

	return false;
}
