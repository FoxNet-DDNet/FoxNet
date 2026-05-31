// Made by qxdFox
#include "lissajous.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
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
		m_aSnap[Idx].m_Id = Server()->SnapNewId();

	// Sort based on m_Id
	std::sort(std::begin(m_aSnap), std::end(m_aSnap), [](const CSnapData &a, const CSnapData &b) { return a.m_Id.value() < b.m_Id.value(); });

	GameWorld()->InsertEntity(this);
}

void CLissajous::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("lissajous", "Reset");

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
	{
		if(m_aSnap[Idx].m_Id.has_value())
			Server()->SnapFreeId(m_aSnap[Idx].m_Id.value());
	}

	m_MarkedForDestroy = true;
}

void CLissajous::Tick()
{
	if(m_MarkedForDestroy)
		return;

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

		m_aSnap[Idx].m_To = LissajousPos(Point);
		m_aSnap[Idx].m_From = LissajousPos(NextPoint);
	}

	m_aSnap[NUM_POINTS].m_To = LissajousPos(0);
	m_aSnap[NUM_POINTS].m_From = LissajousPos(0);
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

	if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
	{
		if(!m_aSnap[Idx].m_Id.has_value())
			continue;
		SnapCosmeticLaser(SnappingClient, m_aSnap[Idx].m_Id.value(), m_Owner, m_aSnap[Idx].m_To, m_aSnap[Idx].m_From, 0, LASERTYPE_GUN, -1, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD);
	}
}
