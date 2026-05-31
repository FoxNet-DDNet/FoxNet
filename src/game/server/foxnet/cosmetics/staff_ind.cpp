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
	{
		if(m_aIds[i].has_value())
			Server()->SnapFreeId(m_aIds[i].value());
	}

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

	if(m_aIds[BALL].has_value())
		SnapCosmeticLaser(SnappingClient, m_aIds[BALL].value(), m_Owner, m_aPos[BALL], m_aPos[BALL], 1, LASERTYPE_GUN, m_BallFirst ? -1 : 0, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD);
	if(m_aIds[ARMOR].has_value())
		SnapCosmeticPickup(SnappingClient, m_aIds[ARMOR].value(), PICKUPFLAG_NO_PREDICT, m_Owner, m_aPos[ARMOR], POWERUP_ARMOR, -1, 0, -1, COSMETIC_FLAG_ANCHORED);
	if(m_aIds[BALL_FRONT].has_value())
		SnapCosmeticLaser(SnappingClient, m_aIds[BALL_FRONT].value(), m_Owner, m_aPos[BALL], m_aPos[BALL], 1, LASERTYPE_GUN, m_BallFirst ? 0 : -1, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD);
}
