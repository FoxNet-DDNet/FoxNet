// Made by qxdFox
#include "heart_hat.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

static constexpr float HeartOffset = -42.0f;
static constexpr float MaxHeartDist = 24.0f;

CHeartHat::CHeartHat(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_HEART_HAT, Pos)
{
	m_Pos = Pos;

	for(size_t i = 0; i < NUM_HEARTS; i++)
		m_aIds[i] = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

void CHeartHat::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("hearthat", "Reset");

	for(size_t i = 0; i < NUM_HEARTS; i++)
		Server()->SnapFreeId(m_aIds[i]);
	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CHeartHat::Tick()
{
	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_HeartHat)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	m_Pos = GetCharacter()->GetPos();

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

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowHats)
		return;

	for(int Heart = 0; Heart < NUM_HEARTS; Heart++)
	{
		const int Id = m_aIds[Heart];

		vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient);

		float Dist = m_Dist * (Heart == 0 ? -1.0f : 1.0f);

		Pos += vec2(Dist, HeartOffset);

		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);
		GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), Id, Pos, POWERUP_HEALTH, -1, -1, PICKUPFLAG_NO_PREDICT);
	}
}
