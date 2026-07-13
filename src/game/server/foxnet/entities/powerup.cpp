// Made by qxdFox
#include "powerup.h"

#include <base/log.h>
#include <base/net.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/teamscore.h>

#include <algorithm>
#include <iterator>
#include <random>
#include <base/math.h>
#include <array>

CPowerUp::CPowerUp(CGameWorld *pGameWorld, int MultiMapIdx, vec2 Pos) :
	CEntity(pGameWorld, MultiMapIdx, CGameWorld::ENTTYPE_POWERUP, true, Pos, 54)
{
	m_StartTick = Server()->Tick();
	m_Pos = Pos;

	for(size_t i = 0; i < NUM_LASERS; i++)
		m_Snap[i].m_Id = Server()->SnapNewId();
	std::sort(std::begin(m_Snap), std::end(m_Snap), [](const auto &a, const auto &b) { return a.m_Id.value() < b.m_Id.value(); });
	
	GameWorld()->InsertEntity(this);

	SetData();
}

void CPowerUp::SetData()
{
	std::uniform_real_distribution<float> dis(0.0f, 100.0f);

	float RandomFloat = dis(Rng());

	if(RandomFloat < 0.8f)
		m_Data.m_Type = EPowerUp::BOOST;
	else if(RandomFloat < 50.0f)
		m_Data.m_Type = EPowerUp::XP;
	else
		m_Data.m_Type = EPowerUp::MONEY;

	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		if(!Server()->ClientIngame(i))
			continue;

		if(!GameServer()->GetPlayerChar(i))
			continue;

		if(!GameServer()->m_aAccounts[i].m_LoggedIn)
			continue;

		m_MaxCollections++;
	}
	if(m_MaxCollections < 1)
		m_MaxCollections = 1;

	constexpr int MinLifetime = 120;

	switch(m_Data.m_Type)
	{
	case EPowerUp::XP:
		m_Data.m_Value = GameServer()->RandGeometric(Rng(), 5, 35, 0.3);
		m_Lifetime = MinLifetime + m_Data.m_Value * 15;
		break;
	case EPowerUp::MONEY:
		m_Data.m_Value = GameServer()->RandGeometric(Rng(), 3, 20, 0.3) * 25;
		m_Lifetime = MinLifetime + m_Data.m_Value * 0.45f;
		break;
	case EPowerUp::BOOST:
		m_Data.m_Value = GameServer()->RandGeometric(Rng(), 10, 2, 0.3);
		m_Lifetime = MinLifetime;
		m_MaxCollections = 1;
		break;
	default:
		m_Data.m_Value = 0;
		m_Lifetime = 0;
	}
	m_Lifetime *= Server()->TickSpeed();
}

void CPowerUp::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("powerup", "Reset");

	for(size_t i = 0; i < NUM_LASERS; i++)
	{
		if(m_Snap[i].m_Id.has_value())
			Server()->SnapFreeId(m_Snap[i].m_Id.value());
	}

	for(size_t i = 0; i < GameServer()->m_vPowerups.size(); i++)
	{
		if(GameServer()->m_vPowerups[i] == this)
			GameServer()->m_vPowerups.erase(GameServer()->m_vPowerups.begin() + i);
	}

	m_MarkedForDestroy = true;
}

inline static bool PointInSquare(vec2 Point, vec2 Center, float Size)
{
	return (Point.x > Center.x - Size && Point.x < Center.x + Size && Point.y > Center.y - Size && Point.y < Center.y + Size);
}

void CPowerUp::Tick()
{
	if(m_MarkedForDestroy)
		return;

	m_Lifetime--;
	if(m_Lifetime <= 0)
	{
		Reset();
		return;
	}
	if(!g_Config.m_SvAccounts) // Powerups require accounts to store the data
	{
		Reset();
		return;
	}

	SetPowerupVisual();

	int NumCollected = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!Server()->ClientIngame(ClientId))
			continue;

		// Always run HandleClient for active clients so collection/address checks are evaluated
		// This ensures rejoined players with the same address are detected and prevented
		// from collecting repeatedly.
		HandleClient(ClientId);

		if(m_aClients[ClientId].m_Collected && m_aClients[ClientId].m_WasLoggedIn)
			NumCollected++;

		if(NumCollected >= m_MaxCollections)
		{
			Reset();
			return;
		}
	}
}

void CPowerUp::HandleClient(int ClientId)
{
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr || !pChr->IsAlive() || (pChr->Team() != TEAM_FLOCK && !g_Config.m_SvSoloServer))
		return;
	if(pChr->MultiMapIdx() != MultiMapIdx())
		return; // Prevent collection across maps

	CClientMask TeamMask = pChr->TeamMask();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// Only fetch player pointer for active slots.
		CPlayer *pPlayer = nullptr;
		if(Server()->ClientIngame(i))
			pPlayer = GameServer()->m_apPlayers[i];

		if(pPlayer && pPlayer->Acc()->m_Configs.m_HidePowerUps)
			TeamMask.set(i, false);

		// Prevent multi-collect from the same address (covers rejoin to different slot)
		// If either current slot or inspected slot has a collected flag, compare addresses.
		if(i != ClientId && (m_aClients[ClientId].m_Collected || m_aClients[i].m_Collected))
		{
			// Compare stored address (may have been set when someone collected).
			if(net_addr_comp_noport(Server()->ClientAddr(ClientId), &m_aClients[i].m_Addr) == 0)
			{
				m_aClients[ClientId].m_Collected = true;
				m_aClients[i].m_Collected = true;
			}
		}
	}

	if(!m_aClients[ClientId].m_Collected)
	{
		if(PointInSquare(m_Pos, pChr->GetPos(), 54.0f))
		{
			GameServer()->OnCollectPowerup(ClientId, &m_Data);
			GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, TeamMask);

			m_aClients[ClientId].m_Collected = true;
			m_aClients[ClientId].m_WasLoggedIn = pChr->GetPlayer()->Acc()->m_LoggedIn;
			m_aClients[ClientId].m_Addr = *Server()->ClientAddr(ClientId);

			if(m_Lifetime > Server()->TickSpeed() * 30)
				m_Lifetime -= 10 * Server()->TickSpeed(); // Speed up disappearance after collection
		}
	}
}

void CPowerUp::SetPowerupVisual()
{
	for(int i = 0; i < NUM_LASERS; i++)
		m_Snap[i].m_To = m_Snap[i].m_From = vec2(0, 0);

	float Len = 28.0f;

	m_Snap[0].m_To = vec2(-Len, -Len);
	m_Snap[0].m_From = vec2(Len, -Len);

	m_Snap[1].m_To = vec2(Len, -Len);
	m_Snap[1].m_From = vec2(Len, Len);

	m_Snap[2].m_To = vec2(Len, Len);
	m_Snap[2].m_From = vec2(-Len, Len);

	m_Snap[3].m_To = vec2(-Len, Len);
	m_Snap[3].m_From = vec2(-Len, -Len);

	m_Snap[4].m_To = vec2(-Len, -Len);
	m_Snap[4].m_From = vec2(-Len, -Len);

	int LaserType;

	if(m_Data.m_Type == EPowerUp::XP)
		LaserType = LASERTYPE_GUN;
	else if(m_Data.m_Type == EPowerUp::MONEY)
		LaserType = LASERTYPE_SHOTGUN;
	else if(m_Data.m_Type == EPowerUp::BOOST)
		LaserType = LASERTYPE_FREEZE;
	else
		LaserType = LASERTYPE_DOOR;

	for(size_t i = 0; i < NUM_LASERS; i++)
	{
		m_Snap[i].m_LaserType = LaserType;
	}

	//if(m_Data.m_Type == EPowerUp::BOOST)
	//{
	//	for(size_t i = 0; i < NUM_LASERS; i++)
	//	{
	//		Rotate(vec2(0, 0), &m_Snap[i].m_To, pi * 0.25f);
	//		Rotate(vec2(0, 0), &m_Snap[i].m_From, pi * 0.25f);
	//	}
	//}
}

void CPowerUp::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(!GetId().has_value())
		return;

	if(SnappingClient != SERVER_DEMO_CLIENT)
	{
		CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
		if(!pSnapPlayer)
			return;

		if(pSnapPlayer->Acc()->m_Configs.m_HidePowerUps)
			return;

		if(m_aClients[SnappingClient].m_Collected && (!Server()->IsRconAuthed(SnappingClient) || !pSnapPlayer->IsPaused()))
			return; // Hide already collected PowerUps
	}

	// Make the powerup blink when about to disappear
	if(m_Lifetime < Server()->TickSpeed() * 10 && (Server()->Tick() / (Server()->TickSpeed() / 4)) % 2 == 0)
		return;

	CGameTeams Teams = GameServer()->m_pController->Teams();
	if(!Teams.SetMaskWithFlags(SnappingClient, MultiMapIdx(), TEAM_FLOCK, CGameTeams::IGNORE_SOLO))
		return;

	const int SnappingClientVersion = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	if((Server()->Tick() - m_StartTick) % Server()->TickSpeed() == 0)
		m_Switch = !m_Switch;

	GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, SixUp, SnappingClient), GetId().value(), m_Pos, m_Switch, 0, -1, PICKUPFLAG_NO_PREDICT);

	for(int i = 0; i < NUM_LASERS; i++)
	{
		if(!m_Snap[i].m_Id.has_value())
			continue;

		vec2 To = m_Pos + m_Snap[i].m_To;
		vec2 From = m_Pos + m_Snap[i].m_From;
		GameServer()->SnapLaserObject(CSnapContext(SnappingClientVersion, SixUp, SnappingClient), m_Snap[i].m_Id.value(), To, From, Server()->Tick(), -1, m_Snap[i].m_LaserType);
	}
}
