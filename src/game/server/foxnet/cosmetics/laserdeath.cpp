// Made by qxdFox
#include "game/server/entities/character.h"
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>

#include <generated/protocol.h>

#include <base/vmath.h>

#include "laserdeath.h"

CLaserDeath::CLaserDeath(CGameWorld *pGameWorld, int Owner, vec2 Pos, CClientMask Mask) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASERDEATH, Pos)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_Mask = Mask;

	m_Vanish = GameServer()->m_apPlayers[m_Owner] && GameServer()->m_apPlayers[m_Owner]->m_Vanish;

	std::random_device rd;
	std::uniform_int_distribution<long> dist(5.0, 50.0);
	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		m_SnapData.m_aIds[i] = Server()->SnapNewId();

		long Random = dist(rd) + i;

		m_SnapData.m_StartTick[i] = Server()->Tick() + Server()->TickSpeed() / SNAPDELAY * i;

		vec2 Pos = m_Pos + random_direction() * Random;
		m_SnapData.m_aPos[i] = Pos;
	}
	m_EndTick = Server()->Tick() + (Server()->TickSpeed() / SNAPDELAY * MAX_PARTICLES);

	GameWorld()->InsertEntity(this);
}

void CLaserDeath::Reset()
{
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
			GameServer()->CreateSound(m_Pos, SOUND_HOOK_LOOP, m_Mask);
	}
}

void CLaserDeath::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pSnapPlayer)
		return;

	if(!m_Mask.test(SnappingClient))
		return;

	if(m_Vanish && SnappingClient != m_Owner && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return;

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		if(Server()->Tick() < m_SnapData.m_StartTick[i])
			continue;

		vec2 LaserPos = m_SnapData.m_aPos[i];

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_SnapData.m_aIds[i], LaserPos, LaserPos, Server()->Tick(), m_Owner, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
