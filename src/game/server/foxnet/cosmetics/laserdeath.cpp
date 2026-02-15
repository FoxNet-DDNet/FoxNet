// Made by qxdFox
#include "laserdeath.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <random>
#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>

CLaserDeath::CLaserDeath(CGameWorld *pGameWorld, CCollision *pCollision, int Owner, vec2 Pos, CClientMask Mask) :
	CFoxNetEntity(pGameWorld, pCollision, CGameWorld::ENTTYPE_LASERDEATH, Pos)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_Mask = Mask;

	m_Vanish = GetPlayer() && GetPlayer()->m_Vanish;

	std::random_device rd;
	std::uniform_int_distribution<long> dist(5.0, 50.0);
	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		m_SnapData.m_aIds[i] = Server()->SnapNewId();

		long Random = dist(rd) + i;

		m_SnapData.m_StartTick[i] = Server()->Tick() + TICKDELAY * i;

		m_SnapData.m_aPos[i] = m_Pos + random_direction() * Random;
	}
	m_EndTick = Server()->Tick() + TICKDELAY * MAX_PARTICLES;

	GameWorld()->InsertEntity(this);
}

void CLaserDeath::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("laserdeath", "Reset");
	for(int i = 0; i < MAX_PARTICLES; i++)
		Server()->SnapFreeId(m_SnapData.m_aIds[i]);

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CLaserDeath::Tick()
{
	if(Server()->Tick() > m_EndTick + Server()->TickSpeed())
	{
		Reset();
		return;
	}

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		// Create sound when new particle appears
		if(Server()->Tick() == m_SnapData.m_StartTick[i])
			GameServer()->CreateSound(m_Pos, SOUND_BODY_LAND, m_Mask);
	}
}

void CLaserDeath::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(!m_Mask.test(SnappingClient))
		return;

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		if(Server()->Tick() < m_SnapData.m_StartTick[i])
			continue;

		vec2 LaserPos = m_SnapData.m_aPos[i];

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_SnapData.m_aIds[i], LaserPos, LaserPos, Server()->Tick(), -1, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
