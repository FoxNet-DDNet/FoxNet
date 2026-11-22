#include "collidable.h"

#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <algorithm>
#include <limits>
#include <vector>

// m_TuneZone is -1 until the entity has ticked once, see CPickupDrop::Tick
static bool MovingTilesFor(CEntity *pEnt)
{
	return pEnt->GetTuning(std::max(0, pEnt->TuneZone()))->m_MovingTiles;
}

// Gives up as much of a correction as it takes to stay out of the map, rather than all of it
static vec2 MoveAxisClamped(const CCollision *pCollision, vec2 &Place, const vec2 &Delta, const vec2 &Box)
{
	if(Delta.x == 0.0f && Delta.y == 0.0f)
		return vec2(0.0f, 0.0f);

	const auto CanPlace = [&](const vec2 &At) {
		return !pCollision->TestBox(At, Box);
	};

	if(CanPlace(Place + Delta))
	{
		Place += Delta;
		return Delta;
	}

	float Low = 0.0f;
	float High = 1.0f;
	for(int i = 0; i < 10; i++)
	{
		const float Mid = (Low + High) * 0.5f;
		if(CanPlace(Place + Delta * Mid))
			Low = Mid;
		else
			High = Mid;
	}

	if(Low > 0.0f)
	{
		const vec2 Applied = Delta * Low;
		Place += Applied;
		return Applied;
	}
	return vec2(0.0f, 0.0f);
}

void CCollidableZone::Init(CMapItemLayerQuads *pQuadsLayer)
{
	int NumQuads = pQuadsLayer->m_NumQuads;
	IMap *pMap = GameServer()->Map(MultiMapIndex());
	CQuad *pQuads = (CQuad *)pMap->GetDataSwapped(pQuadsLayer->m_Data);
	if(m_Type == COLLZONE_STOPA)
	{
		ReserveQuads(NumQuads);
		for(int i = 0; i < NumQuads; i++)
		{
			CQuadData QuadData;
			QuadData.Init(&pQuads[i], pMap);
			AddQuad(QuadData);
		}
	}
	else
	{
		if(NumQuads <= 0)
			return;
		const int TileType = (m_Type == COLLZONE_HOOK) ? TILE_SOLID : TILE_NOHOOK;

		for(int i = 0; i < NumQuads; i++)
		{
			CQuadData QuadData;
			QuadData.Init(&pQuads[i], pMap);
			Collision()->AddQuad(QuadData, TileType);
		}
	}
}

void CCollidableZone::OnPostTick()
{
	if(Quads().empty())
		return;

	if(!GameServer()->GlobalTuning(MultiMapIndex())->m_MovingTiles)
		return;

	HandleCharacters();
	HandlePickups();
}

void CCollidableZone::TickSharedQuads()
{
	const bool UsingQuads = GameServer()->GlobalTuning(MultiMapIndex())->m_MovingTiles;

	const double Time = static_cast<double>(GameServer()->Server()->Tick() - GameServer()->m_pController->m_QuadStartTick) / GameServer()->Server()->TickSpeed();
	Collision()->UpdateQuads(UsingQuads, Time);

	if(!UsingQuads)
		return;

	HandleSolidQuads();
}

void CCollidableZone::HandleSolidQuads()
{
	if(Collision()->Quads().empty())
		return;
	// Carrying and overrunning are both about a quad having moved, so a layer where none can
	// has nothing for this pass to do
	if(!Collision()->HasAnimatedQuads())
		return;

	const int MapIdx = (int)MultiMapIndex();
	const int MaxClients = Server()->MaxClients();

	for(int ClientId = 0; ClientId < MaxClients; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer || !pPlayer->GetCharacter())
			continue;
		if(pPlayer->MultiMapIdx() != MapIdx)
			continue;
		CCharacter *pChr = pPlayer->GetCharacter();
		if(!pChr->IsAlive())
			continue;

		bool Carried = SolidQuadPush(pChr);

		if(!Carried)
		{
			const int QuadId = Collision()->GetGroundQuadIdAt(pChr->GetPos(), pChr->GetProximityRadius());
			if(const CColQuadData *pQuad = Collision()->Quad(QuadId))
			{
				const vec2 QuadVel = pQuad->MotionAt(pChr->GetPos());

				pChr->m_QuadCarryVel = vec2(QuadVel.x, 0.0f);
				pChr->m_QuadCarryTick = Server()->Tick();
				Carried = true;
			}
		}

		if(!Carried && pChr->m_QuadCarryTick == Server()->Tick() - 1)
		{
			pChr->SetRawVelocity(pChr->GetVelocity() + pChr->m_QuadCarryVel);
			pChr->m_QuadCarryTick = -1;
		}
	}

	for(CEntity *pEnt = GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_PICKUPDROP); pEnt; pEnt = pEnt->TypeNext())
	{
		if(pEnt->MultiMapIdx() != MapIdx)
			continue;
		if(!MovingTilesFor(pEnt))
			continue;

		SolidQuadPush(pEnt);
	}
}

bool CCollidableZone::SolidQuadPush(CEntity *pEnt)
{
	const vec2 BoxSize = vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius());
	const vec2 HalfBox = BoxSize * 0.5f;
	const vec2 Pos = pEnt->GetPos();

	for(const CColQuadData &QuadData : Collision()->Quads())
	{
		const vec2 QuadMotion = QuadData.MotionAt(Pos);
		const float MotionLength = length(QuadMotion);
		if(MotionLength <= 0.001f)
			continue; // a quad that did not move cannot have moved into anything

		if(!InsideQuad(Pos, QuadData, HalfBox))
			continue;

		const vec2 Axis = QuadMotion / MotionLength;

		const vec2 QuadExtent = QuadData.m_AabbMax - QuadData.m_AabbMin;
		const float MaxReach = length(QuadExtent) + std::max(BoxSize.x, BoxSize.y);

		vec2 BestWay = vec2(0.0f, 0.0f);
		float BestDistance = std::numeric_limits<float>::infinity();

		for(int Direction = 0; Direction < 2; Direction++)
		{
			const vec2 Way = Direction == 0 ? Axis : -Axis;

			float Free = -1.0f;
			float Reached = 0.0f;
			for(float Distance = 1.0f; Distance <= MaxReach; Reached = Distance, Distance *= 2.0f)
			{
				if(Collision()->TestBox(Pos + Way * Distance, BoxSize))
					continue;

				float Low = Reached;
				float High = Distance;
				for(int i = 0; i < 8; i++)
				{
					const float Mid = (Low + High) * 0.5f;
					if(Collision()->TestBox(Pos + Way * Mid, BoxSize))
						Low = Mid;
					else
						High = Mid;
				}
				Free = High;
				break;
			}

			if(Free >= 0.0f && Free < BestDistance)
			{
				BestDistance = Free;
				BestWay = Way;
			}
		}

		if(BestDistance == std::numeric_limits<float>::infinity())
			continue;

		pEnt->ForceSetPos(Pos + BestWay * BestDistance);

		if(pEnt->ObjectType() == CGameWorld::ENTTYPE_CHARACTER)
		{
			CCharacter *pChr = static_cast<CCharacter *>(pEnt);
			pChr->ResetCore();

			pChr->m_QuadCarryVel = BestWay * BestDistance;
			pChr->m_QuadCarryTick = Server()->Tick();
		}

		return true;
	}

	return false;
}

void CCollidableZone::CollidableImpl(CEntity *pEnt, int QuadId, vec2 QuadMotion)
{
	const CQuadData &Quad = m_vQuads[QuadId];
	const float Radius = pEnt->GetProximityRadius() * 0.55f;
	const vec2 P = pEnt->GetPos();

	const vec2 *pPoints = Quad.m_aPoints;
	const vec2 aA[4] = {pPoints[0], pPoints[1], pPoints[2], pPoints[3]};
	const vec2 aB[4] = {pPoints[1], pPoints[2], pPoints[3], pPoints[0]};

	bool Buried = true;
	for(int i = 0; i < 4; ++i)
	{
		const vec2 E = aB[i] - aA[i];
		if(dot(E, E) <= 1e-6f)
			continue;

		if(dot(P - aA[i], normalize(vec2(-E.y, E.x))) < 0.0f)
		{
			Buried = false;
			break;
		}
	}

	// Only a quad overlapping this one can hide one of its edges, and those are settled at load
	m_vpNeighbours.clear();
	QueryNeighbours(QuadId, m_vNeighbourIds);
	for(int OtherId : m_vNeighbourIds)
		m_vpNeighbours.push_back(&m_vQuads[OtherId]);

	const auto BuriedAt = [&](const vec2 &Point) {
		for(const CQuadData *pOther : m_vpNeighbours)
		{
			if(InsideQuad(Point, *pOther))
				return true;
		}
		return false;
	};

	const auto SeamEdge = [&](const vec2 &A, const vec2 &B, const vec2 &Outward) {
		if(m_vpNeighbours.empty())
			return false;
		for(int Sample = 1; Sample <= 3; Sample++)
		{
			if(!BuriedAt(mix(A, B, Sample * 0.25f) + Outward))
				return false;
		}
		return true;
	};

	vec2 PushDir = vec2(0.0f, 0.0f);
	float PushDepth = 0.0f;
	int BestEdgeIdx = -1;
	vec2 BestEdgeVec = vec2(0.0f, 0.0f);

	if(Buried)
	{
		// Inside, the shallowest edge is the way out, and the seams are not ways out at all
		float MinPenetration = std::numeric_limits<float>::infinity();
		vec2 BestInwardNormal = vec2(0.0f, 0.0f);

		for(int i = 0; i < 4; ++i)
		{
			const vec2 E = aB[i] - aA[i];
			if(dot(E, E) <= 1e-6f)
				continue;

			const vec2 N_in = normalize(vec2(-E.y, E.x));

			if(SeamEdge(aA[i], aB[i], -N_in))
				continue;

			const float Penetration = dot(P - aA[i], N_in) + Radius;
			if(Penetration < MinPenetration)
			{
				MinPenetration = Penetration;
				BestInwardNormal = N_in;
				BestEdgeIdx = i;
				BestEdgeVec = E;
			}
		}

		if(BestEdgeIdx < 0 || MinPenetration <= 0.0f)
			return;

		PushDepth = MinPenetration;
		PushDir = -BestInwardNormal;
	}
	else
	{
		float Nearest = std::numeric_limits<float>::max();
		vec2 NearestPoint = vec2(0.0f, 0.0f);

		for(int i = 0; i < 4; ++i)
		{
			const vec2 E = aB[i] - aA[i];
			const float Len2 = dot(E, E);
			if(Len2 <= 1e-6f)
				continue;

			const vec2 Point = aA[i] + E * std::clamp(dot(P - aA[i], E) / Len2, 0.0f, 1.0f);
			const float Dist = distance(P, Point);
			if(Dist < Nearest)
			{
				Nearest = Dist;
				NearestPoint = Point;
				BestEdgeIdx = i;
				BestEdgeVec = E;
			}
		}

		if(BestEdgeIdx < 0 || Nearest >= Radius)
			return; // only the box around the entity ever reached this quad

		const vec2 N_in = normalize(vec2(-BestEdgeVec.y, BestEdgeVec.x));
		if(BuriedAt(NearestPoint - N_in))
			return; // touching this quad through the neighbour that covers it

		PushDepth = Radius - Nearest;
		PushDir = Nearest > 0.0001f ? (P - NearestPoint) / Nearest : -N_in;
	}

	{
		const vec2 MTV = PushDir * PushDepth;
		const vec2 Box = vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius());

		vec2 NewPos = pEnt->m_Pos;

		vec2 AppliedX = MoveAxisClamped(Collision(), NewPos, vec2(MTV.x, 0.0f), Box);
		vec2 AppliedY = MoveAxisClamped(Collision(), NewPos, vec2(0.0f, MTV.y), Box);

		const vec2 Vel = pEnt->GetVelocity();
		pEnt->ForceSetPos(NewPos);

		vec2 NewVel = Vel;

		const vec2 SurfaceNormal = PushDir;
		const float SurfaceSpeed = dot(QuadMotion, SurfaceNormal);
		const float Target = std::max(SurfaceSpeed, 0.0f);
		const float Along = dot(NewVel, SurfaceNormal);
		if(Along < Target)
			NewVel += SurfaceNormal * (Target - Along);

		if(AppliedX.x == 0.0f && MTV.x != 0.0f)
			NewVel.x = 0.0f;
		if(AppliedY.y == 0.0f && MTV.y != 0.0f)
			NewVel.y = 0.0f;

		pEnt->SetRawVelocity(NewVel);

		if(pEnt->ObjectType() == CGameWorld::ENTTYPE_CHARACTER)
		{
			CCharacter *pChr = static_cast<CCharacter *>(pEnt);

			if(!pChr)
				return;

			pChr->ResetCore();

			if(g_Config.m_SvQStopaGivesDj && BestEdgeIdx >= 0)
			{
				const float NormalThresh = 0.35f;
				const float SlopeThresh = 0.60f;

				float edgeLen = length(BestEdgeVec);
				float edgeSlope = edgeLen > 1e-6f ? absolute(BestEdgeVec.y) / edgeLen : 1.0f;
				bool IsFloorNormal = (PushDir.y <= -NormalThresh);
				bool IsFlatEnough = (edgeSlope <= SlopeThresh);
				bool PushedUp = (AppliedY.y < 0.0f);
				bool WasFallingOrRest = (Vel.y >= 0.0f);

				if(IsFloorNormal && IsFlatEnough && PushedUp && WasFallingOrRest)
					pChr->ResetJumps();
			}

			pChr->ResettleCore();
		}
	}
}

/*
 * How far apart the path below may be sampled. The push sends an entity out by the nearest edge,
 * so to be sent back the way it came, a sample has to land while the entity is still outside the
 * face it is about to cross and within Radius of it. That band is Radius deep outside the face
 * and half the quad's waist inside it, so a step no larger than that always puts a sample in it.
 * Caught on the far side instead, the nearest edge is the one it was leaving by and the push
 * would shove it the rest of the way through.
 */
float CCollidableZone::SampleStep(CEntity *pEnt, const vec2 &From, const vec2 &To)
{
	const float Radius = pEnt->GetProximityRadius() * 0.55f;

	QueryQuads(
		vec2(std::min(From.x, To.x) - Radius, std::min(From.y, To.y) - Radius),
		vec2(std::max(From.x, To.x) + Radius, std::max(From.y, To.y) + Radius),
		m_vSweepCandidates);

	float Step = std::numeric_limits<float>::max();
	for(int QuadId : m_vSweepCandidates)
		Step = std::min(Step, Radius + m_vQuads[QuadId].m_Waist * 0.5f);

	if(Step == std::numeric_limits<float>::max())
		return 0.0f; // nothing on the path, so the whole travel is one step

	return std::max(Step, ms_MinSampleStep);
}

void CCollidableZone::SweepResolve(CEntity *pEnt)
{
	const vec2 To = pEnt->GetPos();

	if(pEnt->QuadSweepTick() != Server()->Tick())
	{
		ResolveOverlaps(pEnt); // nothing recorded this tick, so there is no path to walk
		return;
	}

	const vec2 From = pEnt->QuadSweepFrom();
	const vec2 Travel = To - From;
	const float Length = length(Travel);

	int Steps = 1;
	if(Length > pEnt->GetProximityRadius() * 0.55f)
	{
		const float Step = SampleStep(pEnt, From, To);
		if(Step > 0.0f)
			Steps = std::clamp((int)std::ceil(Length / Step), 1, ms_MaxSampleSteps);
	}

	// One step is the end of the travel, which is where this has always acted -- so slow enough
	// to be in no danger of stepping over a quad, nothing about this changes a thing
	if(Steps <= 1)
	{
		ResolveOverlaps(pEnt);
		return;
	}

	vec2 Offset = vec2(0.0f, 0.0f);
	for(int Step = 1; Step <= Steps; Step++)
	{
		const vec2 Target = From + Travel * ((float)Step / (float)Steps) + Offset;
		pEnt->ForceSetPos(Target);
		ResolveOverlaps(pEnt);
		// Carry what the push changed into the rest of the travel, so the entity keeps sliding
		// along a surface instead of being dragged back onto the original line
		Offset += pEnt->GetPos() - Target;
	}

	/*
	 * A push quantizes where it lands, but a sample that pushed nothing leaves the carried offset
	 * on the position as it stands. Ending a tick on a fractional position is something no other
	 * path here does, and the snapshot the prediction gets carries whole units, so settle it.
	 */
	if(Offset != vec2(0.0f, 0.0f) && pEnt->ObjectType() == CGameWorld::ENTTYPE_CHARACTER)
		static_cast<CCharacter *>(pEnt)->ResettleCore();
}

void CCollidableZone::ResolveOverlaps(CEntity *pEnt)
{
	const vec2 Size = vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius()) * 0.55f;
	const vec2 Pos = pEnt->GetPos();

	// Twice the box, so a quad that a push of this pass moves the entity into is already listed
	const vec2 Margin = Size * 2.0f;
	QueryQuads(
		vec2(Pos.x - Margin.x, Pos.y - Margin.y),
		vec2(Pos.x + Margin.x, Pos.y + Margin.y),
		m_vOverlapCandidates);

	// Map order, as the walk of the whole layer had: the pushes below are cumulative, so which
	// quad goes first is part of the answer and the grid hands them back cell by cell
	std::sort(m_vOverlapCandidates.begin(), m_vOverlapCandidates.end());

	for(int QuadId : m_vOverlapCandidates)
	{
		const CQuadData &QuadData = m_vQuads[QuadId];
		// Re-read, since every push of this loop moves the entity
		const vec2 At = pEnt->GetPos();
		if(!InsideQuad(At, QuadData, Size))
			continue;

		CollidableImpl(pEnt, QuadId, QuadData.MotionAt(At));
	}
}

void CCollidableZone::HandleCharacters()
{
	if(Quads().empty())
		return;

	const int MapIdx = (int)MultiMapIndex();
	const int MaxClients = Server()->MaxClients();

	for(int ClientId = 0; ClientId < MaxClients; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer || !pPlayer->GetCharacter())
			continue;
		if(pPlayer->MultiMapIdx() != MapIdx)
			continue;
		CCharacter *pChr = pPlayer->GetCharacter();
		if(!pChr || !pChr->IsAlive())
			continue;

		SweepResolve(pChr);
	}
}

void CCollidableZone::HandlePickups()
{
	if(Quads().empty())
		return;

	const int MapIdx = (int)MultiMapIndex();

	for(CEntity *pEnt = GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_PICKUPDROP); pEnt; pEnt = pEnt->TypeNext())
	{
		if(pEnt->MultiMapIdx() != MapIdx)
			continue;
		if(!MovingTilesFor(pEnt))
			continue;

		SweepResolve(pEnt);
	}
}
