// Made by qxdFox
#include "meteor.h"

#include "foxnet_entity.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>

#include <algorithm>
#include <optional>
#include <random>
#include <vector>
#include <game/server/player.h>

constexpr float MinMaxOffsetX = 350.0f;

constexpr float TileSize = 32.0f;
constexpr float OffsetY = TileSize * -35.0f;

constexpr float MovementSpeed = 48.0f;

constexpr float TargetSearchDepth = TileSize * 50.0f;

constexpr float MaxDistanceFromPlayer = TileSize * 25.0f;

vec2 CMeteor::FindTargetBlock(vec2 InitialPos)
{
	auto IsTopSurface = [this](vec2 Pos, const CColQuadData **ppTargetQuad) {
		const CColQuadData *pHitQuad = nullptr;
		if(!Collision()->CheckPoint(Pos, &pHitQuad) || Collision()->CheckPoint(Pos - vec2(0.0f, 1.0f)))
			return false;

		*ppTargetQuad = pHitQuad;
		return true;
	};

	m_pTargetQuad = nullptr;
	const bool StartedInCollision = Collision()->CheckPoint(InitialPos);
	for(float Distance = 1.0f; Distance <= TargetSearchDepth; Distance += 1.0f)
	{
		// Prefer the lower candidate when both directions find a surface at
		// the same distance.
		const vec2 Below = InitialPos + vec2(0.0f, Distance);
		const CColQuadData *pTargetQuad = nullptr;
		if(IsTopSurface(Below, &pTargetQuad))
		{
			m_pTargetQuad = pTargetQuad;
			return Below;
		}

		if(StartedInCollision)
		{
			const vec2 Above = InitialPos - vec2(0.0f, Distance);
			if(IsTopSurface(Above, &pTargetQuad))
			{
				m_pTargetQuad = pTargetQuad;
				return Above;
			}
		}
	}

	// When summoned in air, prefer every possible surface below before using
	// a surface above as a fallback.
	if(!StartedInCollision)
	{
		for(float Distance = 1.0f; Distance <= TargetSearchDepth; Distance += 1.0f)
		{
			const vec2 Above = InitialPos - vec2(0.0f, Distance);
			const CColQuadData *pTargetQuad = nullptr;
			if(IsTopSurface(Above, &pTargetQuad))
			{
				m_pTargetQuad = pTargetQuad;
				return Above;
			}
		}
	}

	return InitialPos;
}

vec2 CMeteor::GetRandomStartPos()
{
	std::uniform_real_distribution<float> DisX(-MinMaxOffsetX, MinMaxOffsetX);

	vec2 RandomOffset = vec2(DisX(Rng()), OffsetY);
	if(m_IsCosmetic)
		RandomOffset.y = OffsetY * 0.5f;

	return m_TargetPos + RandomOffset;
}

CMeteor::CMeteor(CGameWorld *pGameWorld, int Owner, vec2 TargetPos, bool IsCosmetic) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_LIGHTSABER, TargetPos)
{
	m_IsCosmetic = IsCosmetic;
	m_Pos = TargetPos;
	m_StartTick = Server()->Tick();
	m_State = EState::Falling;
	m_StartTeam = GetCharacter()->Team();

	const vec2 CharPos = GetCharacter()->GetPos();
	if(!m_IsCosmetic)
	{
		if(distance(CharPos, TargetPos) > MaxDistanceFromPlayer)
		{
			const vec2 Dir = normalize(TargetPos - CharPos);
			TargetPos = CharPos + Dir * MaxDistanceFromPlayer;
		}
		m_TargetPos = FindTargetBlock(TargetPos);
	}
	else
		m_TargetPos = TargetPos;
	m_CurrentPos = m_SummonPos = GetRandomStartPos();

	for(size_t i = 0; i < std::size(m_aIds); i++)
		m_aIds[i] = Server()->SnapNewId().value();
	std::sort(std::begin(m_aIds), std::end(m_aIds));

	for(size_t i = 0; i < std::size(m_aOtherIds); i++)
		m_aOtherIds[i] = Server()->SnapNewId().value();
	std::sort(std::begin(m_aOtherIds), std::end(m_aOtherIds));

	GameWorld()->InsertEntity(this);
}

void CMeteor::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("roulette", "Reset");

	if(GetId().has_value())
		Server()->SnapFreeId(GetId().value());

	for(size_t i = 0; i < std::size(m_aIds); i++)
		Server()->SnapFreeId(m_aIds[i]);

	GameWorld()->RemoveEntity(this);
}

void CMeteor::Tick()
{
	auto HitSolid = [this](vec2 PrevPos, vec2 Pos) -> std::optional<vec2> {
		if(PrevPos == Pos)
			return Collision()->CheckPoint(Pos) ? std::optional<vec2>(Pos) : std::nullopt;

		vec2 CollisionPos;
		if(Collision()->IntersectLine(PrevPos, Pos, &CollisionPos, nullptr))
			return CollisionPos;

		return std::nullopt;
	};

	auto HitAirborneCharacter = [this](vec2 PrevPos, vec2 Pos) -> std::optional<vec2> {
		static constexpr float HitRadius = 6.0f;

		const CCharacter *pOwner = GetCharacter();
		float ClosestDistance = distance(PrevPos, Pos) + 1.0f;
		std::optional<vec2> ClosestHitPos;
		for(CCharacter *pChr = static_cast<CCharacter *>(GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER));
			pChr;
			pChr = static_cast<CCharacter *>(pChr->TypeNext()))
		{
			if(pChr == pOwner || !pChr->CanCollide(m_Owner))
				continue;

			vec2 IntersectPos;
			if(!closest_point_on_line(PrevPos, Pos, pChr->GetPos(), IntersectPos))
				continue;
			if(distance(pChr->GetPos(), IntersectPos) >= pChr->GetProximityRadius() + HitRadius)
				continue;

			const float HitDistance = distance(PrevPos, IntersectPos);
			if(HitDistance < ClosestDistance)
			{
				ClosestDistance = HitDistance;
				ClosestHitPos = IntersectPos;
				if(pChr->IsGrounded())
					ClosestHitPos = vec2(IntersectPos.x, IntersectPos.y + CCharacterCore::PhysicalSize());
			}
		}

		return ClosestHitPos;
	};

	if(m_State == EState::Falling)
	{
		const vec2 Dir = normalize(m_TargetPos - m_SummonPos);

		const vec2 PrevPos = m_CurrentPos;
		m_CurrentPos += Dir * MovementSpeed;

		float LowestPosition = m_TargetPos.y;
		if(const CQuadData *pCurrentTargetQuad = Collision()->ResolveCurrentQuad(m_pTargetQuad))
			LowestPosition = pCurrentTargetQuad->m_AabbMin.y;

		if(GameLayerClipped(m_CurrentPos))
			m_State = EState::Explosion;

		std::optional<vec2> HitPos = HitAirborneCharacter(PrevPos, m_CurrentPos);

		if(m_CurrentPos.y >= LowestPosition) // Don't check collision until low enough
		{
			if(m_IsCosmetic)
				HitPos = m_TargetPos;
			else
			{
				const std::optional<vec2> SolidHitPos = HitSolid(PrevPos, m_CurrentPos);
				if(SolidHitPos.has_value() && (!HitPos.has_value() || distance(PrevPos, SolidHitPos.value()) < distance(PrevPos, HitPos.value())))
					HitPos = SolidHitPos;
			}
		}

		if(HitPos.has_value())
		{
			m_CurrentPos = HitPos.value();
			m_State = EState::Explosion;
		}
	}
	if(m_State == EState::Explosion)
	{
		static constexpr size_t NumExplosions = 2;
		for(size_t i = 0; i < NumExplosions; i++)
		{
			if(m_IsCosmetic)
				GameServer()->Explosion(m_CurrentPos, m_StartTeamMask);
			else
				GameServer()->CreateExplosion(m_CurrentPos, m_Owner, WEAPON_METEOR, false, m_StartTeam, MultiMapIdx(), m_StartTeamMask);
		}

		GameServer()->CreateSound(m_CurrentPos, SOUND_GRENADE_EXPLODE, m_StartTeamMask);
		Reset();
	}
}

void CMeteor::Snap(int SnappingClient)
{
	if(m_IsCosmetic)
	{
		CPlayer *pSnapPlayer;
		if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
			return;

		if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowDeaths)
			return;
	}
	else
	{
		if(!CanSnapEntityMask(SnappingClient, m_StartTeamMask))
			return;
	}

	static constexpr float GrenadeDistance = 12.0f;
	static constexpr float LaserDistance = 18.0f;

	for(size_t i = 0; i < std::size(m_aIds); i++)
	{
		const int Id = m_aIds[i];

		const int Tick = Server()->Tick() - m_StartTick;
		const float Spin = Tick * 0.35f;
;
		if(i < NumGrenades) // 3 Grenades
		{
			vec2 GrenadeOffset = CircleDirection(i, NumGrenades) * GrenadeDistance;

			Rotate(vec2(0, 0), &GrenadeOffset, Spin);
			const vec2 GrenadeSpin = normalize(vec2(GrenadeOffset.y, -GrenadeOffset.x));

			SnapCosmeticProjectilePos(SnappingClient, Id, WEAPON_GRENADE, m_CurrentPos + GrenadeOffset, GrenadeSpin);
		}
		else
		{
			vec2 LaserFrom = CircleDirection(i - 3, NumLasers) * LaserDistance;
			vec2 LaserTo = CircleDirection(i - 3 + 1, NumLasers) * LaserDistance;

			Rotate(vec2(0, 0), &LaserFrom, Spin + pi);
			Rotate(vec2(0, 0), &LaserTo, Spin + pi);

			SnapCosmeticLaserPos(SnappingClient, Id, m_Owner, m_CurrentPos + LaserFrom, m_CurrentPos + LaserTo, 4, LASERTYPE_DRAGGER, -1, COSMETIC_LASER_FLAG_FROM_HEAD);
		}
	}
	for(size_t i = 0; i < std::size(m_aOtherIds); i++)
	{
		const int Id = m_aOtherIds[i];
		const vec2 Dir = normalize(m_TargetPos - m_SummonPos);

		if(i == 0)
		{
			vec2 Pos = m_CurrentPos + vec2(Dir.y, -Dir.x) * (LaserDistance + 4.0f);
			SnapCosmeticProjectilePos(SnappingClient, Id, WEAPON_HAMMER, Pos);
		}
		else
		{
			vec2 Pos = m_CurrentPos + vec2(-Dir.y, Dir.x) * (LaserDistance + 4.0f);
			SnapCosmeticProjectilePos(SnappingClient, Id, WEAPON_HAMMER, Pos);
		}
	}
}
