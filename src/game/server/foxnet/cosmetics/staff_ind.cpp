#include "staff_ind.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <algorithm>
#include <iterator>

CStaffInd::CStaffInd(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_STAFF_IND, Pos)
{
	m_Pos = Pos;

	m_Dist = 0.f;
	m_BallFirst = true;

	for(int i = 0; i < NUM_IDS; i++)
		m_aIds[i] = Server()->SnapNewId();
	std::sort(std::begin(m_aIds), std::end(m_aIds));
	GameWorld()->InsertEntity(this);
}

void CStaffInd::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("staffind", "Reset");

	for(int i = 0; i < NUM_IDS; i++)
		Server()->SnapFreeId(m_aIds[i]);

	m_MarkedForDestroy = true;
}

void CStaffInd::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_StaffInd)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	m_Pos = GetCharacter()->GetPos();
	m_aPos[ARMOR] = vec2(0, -70.f);

	if(m_BallFirst)
	{
		m_Dist += 0.9f;
		if(m_Dist > 25.f)
			m_BallFirst = false;
	}
	else
	{
		m_Dist -= 0.9f;
		if(m_Dist < -25.f)
			m_BallFirst = true;
	}

	m_aPos[BALL] = vec2(m_Dist, m_aPos[ARMOR].y);
}

void CStaffInd::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(!CanSnapEntity(SnappingClient))
		return;

	SnapCosmeticLaser(SnappingClient, m_aIds[BALL], m_Owner, m_aPos[BALL], m_aPos[BALL], 1, LASERTYPE_GUN, m_BallFirst ? -1 : 0, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD);
	SnapCosmeticPickup(SnappingClient, m_aIds[ARMOR], PICKUPFLAG_NO_PREDICT, m_Owner, m_aPos[ARMOR], POWERUP_ARMOR, -1, 0, -1, COSMETIC_FLAG_ANCHORED);
	SnapCosmeticLaser(SnappingClient, m_aIds[BALL_FRONT], m_Owner, m_aPos[BALL], m_aPos[BALL], 1, LASERTYPE_GUN, m_BallFirst ? 0 : -1, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD);
}
