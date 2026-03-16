// Made by qxdFox
#include "firework.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <random>

constexpr int LaunchSpeed = -25;
constexpr float LaunchTime = 1.5f;
constexpr float FireworkTime = 3.5f;
constexpr float MaxSpeed = 20.0f;

CFirework::CFirework(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_FIREWORK, Pos)
{
	m_Owner = Owner;
	m_Pos = Pos;
	m_StartPos = m_Pos;
	m_StartTick = Server()->Tick();

	std::random_device rd;
	for(int i = 0; i < MAX_FIREWORKS; i++)
	{
		m_aIds[i] = Server()->SnapNewId();

		std::uniform_int_distribution<long> X(-MaxSpeed, MaxSpeed);
		std::uniform_int_distribution<long> Y(-MaxSpeed, MaxSpeed);
		std::uniform_int_distribution<long> Lt(Server()->TickSpeed() * 1.5f, FireworkTime * Server()->TickSpeed() - MAX_FIREWORKS);

		m_aVel[i] = vec2(X(rd), Y(rd));
		m_aLifetime[i] = Lt(rd) + i;
	}
	m_State = State::START;
	GameWorld()->InsertEntity(this);
}

void CFirework::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("firework", "Reset");
	for(int i = 0; i < MAX_FIREWORKS; i++)
		Server()->SnapFreeId(m_aIds[i]);

	m_MarkedForDestroy = true;
}

void CFirework::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(m_State == State::START)
	{
		m_Pos.y += (LaunchSpeed * LaunchTime) / Server()->TickSpeed() * (LaunchTime * 2 + 0.25f /*padding*/);

		if(m_StartTick + Server()->TickSpeed() * LaunchTime < Server()->Tick())
		{
			for(int i = 0; i < MAX_FIREWORKS; i++)
			{
				m_aPos[i].y = m_StartPos.y + LaunchSpeed * LaunchTime * 5;
				m_aPos[i].x = m_StartPos.x;
			}

			m_State = State::EXPLOSION;
			GameServer()->Explosion(m_Pos, m_StartTeamMask);
			m_StartTick = Server()->Tick() - 2;
		}
	}
	else if(m_State == State::EXPLOSION)
	{
		for(int i = 0; i < MAX_FIREWORKS; i++)
			m_aLifetime[i]--;

		if(m_StartTick + Server()->TickSpeed() * FireworkTime < Server()->Tick())
			m_State = State::NONE;
	}
	else
	{
		Reset();
		return;
	}

	for(int i = 0; i < MAX_FIREWORKS; i++)
	{
		m_aPos[i] += m_aVel[i] / 10;
	}
}

void CFirework::Snap(int SnappingClient)
{
	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;

	if(m_State == State::START)
	{
		if(NetworkClipped(SnappingClient, m_Pos))
			return;
		CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(GetId());
		if(!pProj)
			return;

		pProj->m_X = round_to_int(m_Pos.x * 100.0f);
		pProj->m_Y = round_to_int(m_Pos.y * 100.0f);
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		pProj->m_Type = WEAPON_GRENADE;
		pProj->m_StartTick = Server()->Tick();
		pProj->m_Owner = m_Owner;
	}
	else if(m_State == State::EXPLOSION)
	{
		for(int i = 0; i < MAX_FIREWORKS; i++)
		{
			if(NetworkClipped(SnappingClient, m_aPos[i]))
				continue;

			CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(m_aIds[i]);
			if(!pProj || m_aLifetime[i] <= 0)
				continue;

			pProj->m_StartTick = Server()->Tick();

			pProj->m_X = round_to_int(m_aPos[i].x * 100.0f);
			pProj->m_Y = round_to_int(m_aPos[i].y * 100.0f);
			pProj->m_VelX = m_aVel[i].x * 32;
			pProj->m_VelY = m_aVel[i].x * 32;
			pProj->m_Type = WEAPON_LASER;
			pProj->m_Owner = m_Owner;
		}
	}
}