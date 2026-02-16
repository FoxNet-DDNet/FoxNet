// Made by qxdFox
#include "lissajous.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>

constexpr float Speed = 100.0f;

CLissajous::CLissajous(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_LISSAJOUS, Pos)
{
	m_Pos = Pos;

	m_StartTick = Server()->Tick();

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
		m_Snap[Idx].m_Id = Server()->SnapNewId();

	std::sort(m_Snap, m_Snap + NUM_IDS, [](const CSnapData &a, const CSnapData &b) { return a.m_Id < b.m_Id; });

	GameWorld()->InsertEntity(this);
}

void CLissajous::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("lissajous", "Reset");

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
		Server()->SnapFreeId(m_Snap[Idx].m_Id);

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CLissajous::Tick()
{
	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_Lissajous)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	m_Pos = GetCharacter()->GetPos();

	for(int Idx = 0; Idx < NUM_POINTS; ++Idx)
	{
		int Point = Idx % NUM_POINTS;
		int NextPoint = (Idx + 1) % NUM_POINTS;

		m_Snap[Idx].m_To = LissajousPos(Point);
		m_Snap[Idx].m_From = LissajousPos(NextPoint);
	}

	m_Snap[NUM_POINTS].m_To = LissajousPos(0);
	m_Snap[NUM_POINTS].m_From = LissajousPos(0);
}

float CLissajous::Flow()
{
	const int Tick = Server()->Tick() - m_StartTick;
	return Tick / Speed + pi * 0.5f;
}

vec2 CLissajous::LissajousPos(int Point)
{
	// move delta to change shape
	int Tick = Server()->Tick() - m_StartTick;

	float A = 75.0f;
	float B = 75.0f;
	float a = g_Config.m_SvLissajousA;
	float b = g_Config.m_SvLissajousB;
	float delta = Tick / Speed;

	float t = 2 * pi * Point / (float)NUM_POINTS + Flow();

	float x = A * sinf(a * t + delta);
	float y = B * sinf(b * t);

	return vec2(x, y);
}

void CLissajous::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;

	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, false);

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		vec2 From = m_Snap[Idx].m_From + Pos;
		vec2 To = m_Snap[Idx].m_To + Pos;

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_Snap[Idx].m_Id, To, From, Server()->Tick(), m_Owner, 0, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
