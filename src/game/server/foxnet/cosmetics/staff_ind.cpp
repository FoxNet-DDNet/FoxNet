#include "staff_ind.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <algorithm>
#include <iterator>
#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>

CStaffInd::CStaffInd(CGameWorld *pGameWorld, CCollision *pCollision, int Owner, vec2 Pos) :
	CFoxNetEntity(pGameWorld, pCollision, CGameWorld::ENTTYPE_STAFF_IND, Pos)
{
	m_Pos = Pos;
	m_Owner = Owner;

	m_Dist = 0.f;
	m_BallFirst = true;

	for(int i = 0; i < NUM_IDS; i++)
		m_aIds[i] = Server()->SnapNewId();
	std::sort(std::begin(m_aIds), std::end(m_aIds));
	GameWorld()->InsertEntity(this);
}

void CStaffInd::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("staffind", "Reset");

	for(int i = 0; i < NUM_IDS; i++)
		Server()->SnapFreeId(m_aIds[i]);

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CStaffInd::Tick()
{
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

	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);
	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);
	const int BallId = m_BallFirst ? m_aIds[BALL_FRONT] : m_aIds[BALL];

	vec2 Pos = m_Pos + pOwnerChr->GetPredictedPos(SnappingClient) + m_aPos[ARMOR];
	vec2 LaserPos = m_Pos + pOwnerChr->GetPredictedPos(SnappingClient, false) + m_aPos[ARMOR];

	GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), m_aIds[ARMOR], Pos, POWERUP_ARMOR, -1, -1, PICKUPFLAG_NO_PREDICT);
	GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), BallId, LaserPos, LaserPos, Server()->Tick(), m_Owner, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
}
