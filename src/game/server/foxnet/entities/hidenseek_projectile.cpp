// Made by qxdFox
#include "hidenseek_projectile.h"

#include "foxnet_entity.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/foxnet/components/zones/hidenseek.h>
#include <game/server/foxnet/components/zones/zonemanager.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <cmath>

// Same reach as a normal gun bullet
constexpr static float HitRadius = 6.0f;

CHideAndSeekProjectile::CHideAndSeekProjectile(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir, int Span, int FreezeTicks) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_HIDENSEEK_PROJECTILE, Pos)
{
	m_Pos = Pos; // stays the start of the flight curve, like a normal projectile
	m_Direction = Dir;
	m_StartTick = Server()->Tick();
	m_LifeSpan = Span;
	m_FreezeTicks = FreezeTicks;

	CCharacter *pOwnerChar = GetCharacter();
	m_TuneZone = pOwnerChar ? pOwnerChar->GetOverriddenTuneZone() : Collision()->IsTune(Collision()->GetMapIndex(m_Pos));

	GameWorld()->InsertEntity(this);
}

void CHideAndSeekProjectile::Reset()
{
	m_MarkedForDestroy = true;
}

CHideAndSeekZone *CHideAndSeekProjectile::Zone()
{
	return static_cast<CHideAndSeekZone *>(GameServer()->m_ZoneManager.FindZoneByMapIndex(EZoneType::HideNSeek, MultiMapIdx()));
}

vec2 CHideAndSeekProjectile::FlightPos(float Time)
{
	const CTuningParams *pTuning = GetTuning(m_TuneZone);
	return CalcPos(m_Pos, m_Direction, pTuning->m_GunCurvature, pTuning->m_GunSpeed, Time);
}

CCharacter *CHideAndSeekProjectile::IntersectHider(CHideAndSeekZone *pZone, vec2 From, vec2 To, vec2 *pHitPos)
{
	float ClosestLen = distance(From, To) * 100.0f;
	CCharacter *pClosest = nullptr;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == m_Owner)
			continue;
		if(!pZone->IsAliveHider(ClientId))
			continue;

		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(!pChr || pChr->MultiMapIdx() != MultiMapIdx())
			continue;

		vec2 IntersectPos;
		if(!closest_point_on_line(From, To, pChr->m_Pos, IntersectPos))
			continue;

		if(distance(pChr->m_Pos, IntersectPos) >= pChr->GetProximityRadius() + HitRadius)
			continue;

		const float Len = distance(From, IntersectPos);
		if(Len < ClosestLen)
		{
			ClosestLen = Len;
			pClosest = pChr;
			*pHitPos = IntersectPos;
		}
	}

	return pClosest;
}

void CHideAndSeekProjectile::CreateIndLine(vec2 Pos, vec2 Direction)
{
	GameServer()->CreateIndEffect(INDTYPE_LINE, Pos, Direction, TeamMask());
}

void CHideAndSeekProjectile::Tick()
{
	if(m_MarkedForDestroy)
		return;

	CHideAndSeekZone *pZone = Zone();
	if(!pZone || !pZone->IsRoundRunning() || !GetCharacter())
	{
		// The round or the seeker that shot is gone, nothing left to hit
		Reset();
		return;
	}

	const float Pt = (Server()->Tick() - m_StartTick - 1) / (float)Server()->TickSpeed();
	const float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	const vec2 PrevPos = FlightPos(Pt);
	const vec2 CurPos = FlightPos(Ct);

	vec2 ColPos;
	vec2 BeforePos;
	const bool Collide = Collision()->IntersectLine(PrevPos, CurPos, &ColPos, &BeforePos) != 0;
	const bool Clipped = GameLayerClipped(CurPos);

	vec2 Direction = normalize(CurPos - PrevPos);
	if(Direction == vec2(0, 0))
		Direction = m_Direction;

	vec2 HitPos = CurPos;
	CCharacter *pHider = IntersectHider(pZone, PrevPos, Collide ? ColPos : CurPos, &HitPos);

	if(m_LifeSpan > -1)
		m_LifeSpan--;

	if(pHider)
	{
		if(m_FreezeTicks > 0)
			pHider->FreezeTicks(m_FreezeTicks);

		GameServer()->CreateSound(HitPos, SOUND_PLAYER_PAIN_SHORT, TeamMask());
		CreateIndLine(HitPos, Direction);
		Reset();
		return;
	}

	if(Collide || Clipped)
	{
		CreateIndLine(Collide ? ColPos : CurPos, Direction);
		Reset();
		return;
	}

	if(m_LifeSpan == -1)
	{
		CreateIndLine(CurPos, Direction);
		Reset();
		return;
	}
}

void CHideAndSeekProjectile::TickPaused()
{
	m_StartTick++;
}

void CHideAndSeekProjectile::Snap(int SnappingClient)
{
	if(m_MarkedForDestroy || !GetId().has_value())
		return;

	const float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	const vec2 SnapPos = FlightPos(Ct);

	if(NetworkClipped(SnappingClient, SnapPos))
		return;

	if(SnappingClient != SERVER_DEMO_CLIENT && !TeamMask().test(SnappingClient))
		return;

	const int Id = GetId().value();
	const int Ver = GameServer()->GetClientVersion(SnappingClient);

	const int MaxPos = 0x7fffffff / 100;
	const bool LegacyCompatible = !(absolute((int)m_Pos.y) + 1 >= MaxPos || absolute((int)m_Pos.x) + 1 >= MaxPos);

	// Snapped without an owner: the bullet belongs to the minigame, not to the seeker,
	// so no client predicts it away and it cannot be filtered out as "own" weapon fire
	if(Ver >= VERSION_DDNET_ENTITY_NETOBJS)
	{
		CNetObj_DDNetProjectile Proj = {};
		Proj.m_X = round_to_int(m_Pos.x * 100.0f);
		Proj.m_Y = round_to_int(m_Pos.y * 100.0f);
		Proj.m_VelX = round_to_int(m_Direction.x * 1e6f);
		Proj.m_VelY = round_to_int(m_Direction.y * 1e6f);
		Proj.m_Type = WEAPON_GUN;
		Proj.m_StartTick = m_StartTick;
		Proj.m_Owner = -1;
		Proj.m_SwitchNumber = 0;
		Proj.m_TuneZone = m_TuneZone;
		Proj.m_Flags = 0;
		Server()->SnapNewItem(Id, Proj);
	}
	else if(Ver >= VERSION_DDNET_ANTIPING_PROJECTILE && LegacyCompatible)
	{
		const float Angle = -std::atan2(m_Direction.x, m_Direction.y);
		const int Data = LEGACYPROJECTILEFLAG_IS_DDNET | LEGACYPROJECTILEFLAG_NO_OWNER;

		CNetObj_DDRaceProjectile Proj = {};
		Proj.m_X = (int)(m_Pos.x * 100.0f);
		Proj.m_Y = (int)(m_Pos.y * 100.0f);
		Proj.m_Angle = (int)(Angle * 1000000.0f);
		Proj.m_Data = Data;
		Proj.m_StartTick = m_StartTick;
		Proj.m_Type = WEAPON_GUN;

		if(Ver >= VERSION_DDNET_MSG_LEGACY)
		{
			Server()->SnapNewItem(Id, Proj);
		}
		else
		{
			CNetObj_Projectile Projectile = {};
			static_assert(sizeof(Proj) == sizeof(Projectile));
			mem_copy(&Projectile, &Proj, sizeof(Projectile));
			Server()->SnapNewItem(Id, Projectile);
		}
	}
	else
	{
		CNetObj_Projectile Proj = {};
		Proj.m_X = (int)m_Pos.x;
		Proj.m_Y = (int)m_Pos.y;
		Proj.m_VelX = (int)(m_Direction.x * 100.0f);
		Proj.m_VelY = (int)(m_Direction.y * 100.0f);
		Proj.m_StartTick = m_StartTick;
		Proj.m_Type = WEAPON_GUN;
		Server()->SnapNewItem(Id, Proj);
	}
}

void CHideAndSeekProjectile::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}
