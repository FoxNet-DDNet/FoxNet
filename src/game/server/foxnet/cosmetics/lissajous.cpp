// Made by qxdFox
#include "lissajous.h"

#include "game/server/entities/character.h"
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <base/vmath.h>
#include <base/math.h>

#include <algorithm>
#include <cmath>

constexpr float Speed = 100.0f;

CLissajous::CLissajous(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LISSAJOUS, Pos)
{
	m_Pos = Pos;
	m_Owner = Owner;

	m_StartTick = Server()->Tick();

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
		m_Snap[Idx].m_Id = Server()->SnapNewId();

	std::sort(m_Snap, m_Snap + NUM_IDS, [](const CSnapData &a, const CSnapData &b) { return a.m_Id < b.m_Id; });

	GameWorld()->InsertEntity(this);
}

void CLissajous::Reset()
{
	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
		Server()->SnapFreeId(m_Snap[Idx].m_Id);

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CLissajous::Tick()
{
	CPlayer *pOwnerPl = GameServer()->m_apPlayers[m_Owner];
	if(!pOwnerPl || !pOwnerPl->Cosmetics()->m_Lissajous)
	{
		Reset();
		return;
	}
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		return;

	m_Pos = pOwner->GetPos();

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
	float a = 2;
	float b = 3;
	float delta = Tick / Speed;

	float t = 2 * pi * Point / NUM_POINTS + Flow();

	float x = A * sinf(a * t + delta);
	float y = B * sinf(b * t);

	return vec2(x, y);
}

void CLissajous::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);
	const CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pOwnerChr || !pSnapPlayer)
		return;

	if(pSnapPlayer->m_HideCosmetics)
		return;

	if(!pOwnerChr->TeamMask().test(SnappingClient))
		return;

	if(pSnapPlayer->GetCharacter() && pOwnerChr)
		if(!pOwnerChr->CanSnapCharacter(SnappingClient))
			return;

	if(pOwnerChr->GetPlayer()->m_Vanish && SnappingClient != pOwnerChr->GetPlayer()->GetCid() && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return;

	vec2 Pos = m_Pos + pOwnerChr->GetVelocity();
	if(g_Config.m_SvExperimentalPrediction && m_Owner == SnappingClient && !pOwnerChr->GetPlayer()->IsPaused())
	{
		double Pred = pOwnerChr->GetPlayer()->GetClientPred();
		float dist = distance(pOwnerChr->m_Pos, pOwnerChr->m_PrevPos);
		vec2 nVel = normalize(pOwnerChr->GetVelocity()) * Pred * dist / 2.0f;
		Pos = m_Pos + nVel;
	}

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		vec2 From = m_Snap[Idx].m_From + Pos;
		vec2 To = m_Snap[Idx].m_To + Pos;

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_Snap[Idx].m_Id, To, From, Server()->Tick(), m_Owner, 0, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
