// Made by qxdFox
#include "heart_hat.h"

#include "game/server/entities/character.h"

#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>

static constexpr float HeartOffset = -42.0f;
static constexpr float MaxHeartDist = 24.0f;

CHeartHat::CHeartHat(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_HEART_HAT, Pos)
{
	m_Pos = Pos;

	m_Owner = Owner;
	m_Ids[0] = GetId();
	for(int i = 0; i < NUM_HEARTS - 1; i++)
		m_Ids[i + 1] = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

void CHeartHat::Reset()
{
	for(int i = 0; i < NUM_HEARTS - 1; i++)
		Server()->SnapFreeId(m_Ids[i + 1]);

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CHeartHat::Tick()
{
	CPlayer *pOwnerPl = GameServer()->m_apPlayers[m_Owner];
	if(!pOwnerPl || !pOwnerPl->Cosmetics()->m_HeartHat)
	{
		Reset();
		return;
	}
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		return;

	m_Pos = pOwner->GetPos();

	for(int Heart = 0; Heart < NUM_HEARTS; Heart++)
	{

		m_Dist += 1.0f * (m_switch ? -1.0f : 1.0f);

		if(m_Dist >= MaxHeartDist)
			m_switch = true;
		if(m_Dist <= -MaxHeartDist)
			m_switch = false;
	}
}

void CHeartHat::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);
	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pOwnerChr || !pSnapPlayer)
		return;

	if(pOwnerChr->IsPaused())
		return;

	if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowHats)
		return;

	if(!pOwnerChr->TeamMask().test(SnappingClient))
		return;

	if(pSnapPlayer->GetCharacter() && pOwnerChr)
		if(!pOwnerChr->CanSnapCharacter(SnappingClient))
			return;

	if(pOwnerChr->GetPlayer()->m_Vanish && SnappingClient != pOwnerChr->GetPlayer()->GetCid() && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return;

	for(int Heart = 0; Heart < NUM_HEARTS; Heart++)
	{
		const int Id = m_Ids[Heart];

		vec2 Pos = m_Pos + pOwnerChr->GetVelocity();

		if(m_Owner == SnappingClient)
			Pos = pOwnerChr->GetPredictedPos(pOwnerChr->m_Pos, pOwnerChr->m_PrevPos);

		float Dist = m_Dist * (Heart == 0 ? -1.0f : 1.0f);

		Pos += vec2(Dist, HeartOffset);

		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);
		GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), Id, Pos, POWERUP_HEALTH, -1, -1, PICKUPFLAG_NO_PREDICT);
	}
}
