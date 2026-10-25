// Made by qxdFox
#include "worm.h"

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
#include <game/server/foxnet/components/powerup.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <vector>
#include <game/server/player.h>

CWorm::CWorm(CGameWorld *pGameWorld, int MultiMapIdx, vec2 Pos, CWormConfig WormConfig) :
	CEntity(pGameWorld, MultiMapIdx, CGameWorld::ENTTYPE_WORM, true, Pos)
{
	m_WormConfig = WormConfig;
	m_TargetStartTick = Server()->Tick();
	m_LastEffectTick = m_TargetStartTick - 5;
	m_TargetClientId = -1;
	m_HasTarget = false;
	m_Velocity = vec2(0.0f, 0.0f);
	m_Pos = Pos;
	m_StartPos = Pos;
	m_TargetPos = FindTargetPos();

	m_vSegments.resize(m_WormConfig.m_Segments);
	for(auto &Segment : m_vSegments)
	{
		Segment.m_From = Pos;
		Segment.m_To = Pos;
		Segment.m_Id = Server()->SnapNewId().value();
	}

	std::sort(m_vSegments.begin(), m_vSegments.end(),
		[](const WormSegment &a, const WormSegment &b) {
			return a.m_Id < b.m_Id;
		});

	GameWorld()->InsertEntity(this);
}

CWorm::~CWorm()
{
	for(const auto &Segment : m_vSegments)
		Server()->SnapFreeId(Segment.m_Id);
}

vec2 CWorm::FindTargetPos()
{
	m_HasTarget = false;
	m_TargetClientId = -1;
	std::vector<int> vTargets;
	if(m_WormConfig.m_TargetType != EWormTargetType::RandomPosition)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			const CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
			if(pChr && pChr->IsAlive() && pChr->MultiMapIdx() == MultiMapIdx())
				vTargets.push_back(ClientId);
		}
	}
	if(vTargets.empty())
	{
		constexpr float TileSize = 32.0f;
		const float SearchRadius = m_WormConfig.m_RandomSearchRadiusTiles * TileSize;
		const float SolidRange = m_WormConfig.m_NearbySolidRangeTiles * TileSize;
		constexpr int Attempts = 128;
		const float MaxX = Collision()->GetWidth() * TileSize;
		const float MaxY = Collision()->GetHeight() * TileSize;
		const auto IsValidTarget = [&](vec2 Candidate) {
			if(Candidate.x < TileSize || Candidate.y < TileSize || Candidate.x >= MaxX - TileSize || Candidate.y >= MaxY - TileSize ||
				distance(Candidate, m_Pos) < TileSize * 2.0f || Collision()->CheckPoint(Candidate))
				return false;

			// Check eight directions so the air point is near terrain, including diagonal walls.
			for(int Direction = 0; Direction < 8; Direction++)
			{
				const float Angle = Direction * pi / 4.0f;
				const vec2 End = Candidate + vec2(cosf(Angle), sinf(Angle)) * SolidRange;
				if(Collision()->IntersectLine(Candidate, End, nullptr, nullptr))
					return true;
			}
			return false;
		};
		const auto TryNearby = [&]() -> std::optional<vec2> {
			for(int Attempt = 0; Attempt < Attempts; Attempt++)
			{
				const vec2 Candidate = m_Pos + vec2(random_float(-SearchRadius, SearchRadius), random_float(-SearchRadius, SearchRadius));
				if(IsValidTarget(Candidate))
					return Candidate;
			}
			return std::nullopt;
		};
		const auto TryCached = [&]() -> std::optional<vec2> {
			for(int Attempt = 0; Attempt < 16; Attempt++)
			{
				vec2 Candidate;
				if(!GameServer()->m_PowerUps.TryPickCachedCandidate(MultiMapIdx(), Candidate))
					break;
				if(IsValidTarget(Candidate))
					return Candidate;
			}
			return std::nullopt;
		};

		const bool PreferCached = m_UseCachedRandomTargetNext;
		m_UseCachedRandomTargetNext = !m_UseCachedRandomTargetNext;
		std::optional<vec2> Target = PreferCached ? TryCached() : TryNearby();
		if(!Target.has_value())
			Target = PreferCached ? TryNearby() : TryCached();
		if(Target.has_value())
		{
			m_HasTarget = true;
			return Target.value();
		}
		return m_Pos;
	}

	if(m_WormConfig.m_TargetType == EWormTargetType::RandomTee)
	{
		m_TargetClientId = vTargets[static_cast<size_t>(random_float() * vTargets.size()) % vTargets.size()];
		m_HasTarget = true;
		return GameServer()->GetPlayerChar(m_TargetClientId)->GetPos();
	}

	const vec2 Forward = length(m_Velocity) > 0.001f ? normalize(m_Velocity) : vec2(1.0f, 0.0f);
	float BestDistance = 1e30f;
	int BestClientId = -1;
	for(int ClientId : vTargets)
	{
		const vec2 Target = GameServer()->GetPlayerChar(ClientId)->GetPos();
		const vec2 Offset = Target - m_Pos;
		if(dot(Offset, Forward) <= 0.0f)
			continue;
		const float Distance = length_squared(Offset);
		if(Distance < BestDistance)
		{
			BestDistance = Distance;
			BestClientId = ClientId;
		}
	}
	if(BestClientId == -1)
	{
		for(int ClientId : vTargets)
		{
			const vec2 Target = GameServer()->GetPlayerChar(ClientId)->GetPos();
			const float Distance = length_squared(Target - m_Pos);
			if(Distance < BestDistance)
			{
				BestDistance = Distance;
				BestClientId = ClientId;
			}
		}
	}
	m_TargetClientId = BestClientId;
	m_HasTarget = BestClientId != -1;
	return m_HasTarget ? GameServer()->GetPlayerChar(m_TargetClientId)->GetPos() : m_Pos;
}

void CWorm::Reset()
{
	if(m_MarkedForDestroy)
		return;
	if(g_Config.m_SvLogExtra >= 2)
		log_info("roulette", "Reset");

	m_MarkedForDestroy = true;
}

void CWorm::Tick()
{
	const int Now = Server()->Tick();
	bool TeeBecameAvailable = false;
	if(m_WormConfig.m_TargetType != EWormTargetType::RandomPosition && m_TargetClientId == -1)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			const CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
			if(pChr && pChr->IsAlive() && pChr->MultiMapIdx() == MultiMapIdx())
			{
				TeeBecameAvailable = true;
				break;
			}
		}
	}
	const bool TargetsTee = m_TargetClientId != -1;
	bool LostTarget = false;
	if(TargetsTee && m_HasTarget)
	{
		const CCharacter *pTarget = GameServer()->GetPlayerChar(m_TargetClientId);
		if(!pTarget || !pTarget->IsAlive() || pTarget->MultiMapIdx() != MultiMapIdx())
		{
			m_HasTarget = false;
			m_TargetClientId = -1;
			LostTarget = true;
		}
		else
			m_TargetPos = pTarget->GetPos();
	}
	const bool ReachedTarget = !TargetsTee && m_HasTarget && distance(m_Pos, m_TargetPos) <= 16.0f;
	const int TargetTimeout = (int)((TargetsTee ? m_WormConfig.m_TeeRetargetSeconds : m_WormConfig.m_RandomPositionTimeoutSeconds) * Server()->TickSpeed());
	if(LostTarget || TeeBecameAvailable || ReachedTarget || Now - m_TargetStartTick >= (m_HasTarget ? TargetTimeout : Server()->TickSpeed()))
	{
		m_TargetPos = FindTargetPos();
		m_TargetStartTick = Now;
	}

	const float Speed = std::max(0.0f, m_WormConfig.m_Speed);
	if(Speed == 0.0f)
		m_Velocity = vec2(0.0f, 0.0f);
	else if(Collision()->CheckPoint(m_Pos) && m_HasTarget)
	{
		const vec2 ToTarget = m_TargetPos - m_Pos;
		if(length_squared(ToTarget) > 0.001f)
		{
			const float CurrentSpeed = length(m_Velocity);
			vec2 Direction = normalize(ToTarget);
			float TurnSeverity = 0.0f;
			if(CurrentSpeed > 0.001f)
			{
				const vec2 CurrentDirection = m_Velocity / CurrentSpeed;
				const float TurnRadius = std::max(1.0f, m_WormConfig.m_GroundTurnRadiusTiles * 32.0f);
				const float MaxTurn = std::min(pi, CurrentSpeed / TurnRadius);
				const float Turn = atan2f(CurrentDirection.x * Direction.y - CurrentDirection.y * Direction.x,
					dot(CurrentDirection, Direction));
				TurnSeverity = std::abs(Turn) / pi;
				Direction = RotateVec(CurrentDirection, std::clamp(Turn, -MaxTurn, MaxTurn));
			}
			// A sharp turn slows the worm underground; it regains speed on a straight path.
			const float TargetSpeed = Speed * (1.0f - 0.65f * TurnSeverity);
			const float NewSpeed = CurrentSpeed < TargetSpeed ?
				std::min(TargetSpeed, CurrentSpeed + m_WormConfig.m_GroundAcceleration) :
				std::max(TargetSpeed, CurrentSpeed - std::max(0.6f, m_WormConfig.m_GroundAcceleration * 4.0f));
			m_Velocity = Direction * NewSpeed;
		}
	}
	else
	{
		// Airborne motion is ballistic: keep horizontal momentum and add gravity.
		m_Velocity.y = std::min(m_Velocity.y + m_WormConfig.m_AirGravity, std::max(Speed * 2.0f, 10.0f));
	}

	const vec2 PreviousPos = m_Pos;
	m_Pos += m_Velocity;
	// Sample the head's path so a fast step cannot pass through a death tile.
	const int Steps = std::max(1, (int)ceilf(length(m_Velocity) / 8.0f));
	for(int Step = 0; Step <= Steps; Step++)
	{
		const vec2 CheckPos = PreviousPos + m_Velocity * (float(Step) / Steps);
		if(GameLayerClipped(CheckPos) ||
			Collision()->GetCollisionAt(CheckPos.x, CheckPos.y) == TILE_DEATH ||
			Collision()->GetFrontCollisionAt(CheckPos.x, CheckPos.y) == TILE_DEATH)
		{
			Reset();
			return;
		}
	}
	vec2 PreviousSegmentPos = m_Pos;
	const float SegmentLength = std::max(1.0f, m_WormConfig.m_Length);
	for(auto &Segment : m_vSegments)
	{
		const vec2 Offset = Segment.m_From - PreviousSegmentPos;
		const float Distance = length(Offset);
		if(Distance > SegmentLength)
			Segment.m_From = PreviousSegmentPos + Offset / Distance * SegmentLength;
		Segment.m_To = PreviousSegmentPos;
		PreviousSegmentPos = Segment.m_From;
	}

	// The map hides laser segments in solid tiles, so leave a visible pulse at the buried head.
	if(Collision()->CheckPoint(m_Pos) && Now - m_LastEffectTick >= 5)
	{
		m_LastEffectTick = Now;
		CClientMask Mask;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			if(GameServer()->m_apPlayers[ClientId] && CheckMultiMapIdx(ClientId, MultiMapIdx()))
				Mask.set(ClientId);
		}
		GameServer()->CreateDeath(m_Pos, -1, Mask);
	}
}

bool CWorm::CanSnapTo(int SnappingClient)
{
	if(m_MarkedForDestroy)
		return false;
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;
	if(!GameServer()->m_apPlayers[SnappingClient])
		return false;
	return CheckMultiMapIdx(SnappingClient, MultiMapIdx());
}

void CWorm::Snap(int SnappingClient)
{
	if(!CanSnapTo(SnappingClient))
		return;

	const bool SupportsCosmeticSnaps = SnappingClient != SERVER_DEMO_CLIENT &&
		GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps;

	for(const auto &Segment : m_vSegments)
	{
		if(NetworkClippedLine(SnappingClient, Segment.m_From, Segment.m_To))
			continue;

		if(SupportsCosmeticSnaps)
		{
			CNetObj_CosmeticLaser Laser = {};
			Laser.m_FromX = (int)Segment.m_From.x;
			Laser.m_FromY = (int)Segment.m_From.y;
			Laser.m_ToX = (int)Segment.m_To.x;
			Laser.m_ToY = (int)Segment.m_To.y;
			Laser.m_TickOffset = 0;
			Laser.m_Type = LASERTYPE_GUN;
			Laser.m_Owner = -1;
			Laser.m_Alpha = 100;
			Laser.m_Flags = COSMETIC_LASER_FLAG_TO_HEAD;
			Server()->SnapNewItem(Segment.m_Id, Laser);
		}
		else
		{
			const CSnapContext Context(Server()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient);
			GameServer()->SnapLaserObject(Context, Segment.m_Id, Segment.m_To, Segment.m_From,
				Server()->Tick(), -1, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
		}
	}
}
