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

#include <limits>
#include <vector>

void CCollidableZone::Init(CMapItemLayerQuads *pQuadsLayer)
{
	int NumQuads = pQuadsLayer->m_NumQuads;
	CQuad *pQuads = (CQuad *)GameServer()->Map(MultiMapIndex())->GetDataSwapped(pQuadsLayer->m_Data);
	if(m_Type == COLLZONE_STOPA)
	{
		ReserveQuads(NumQuads);
		for(int i = 0; i < NumQuads; i++)
		{
			CQuadData QuadData;
			QuadData.Init(&pQuads[i]);
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
			QuadData.Init(&pQuads[i]);
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

				/*
				 * Sideways only, because sideways is all that standing on a quad ever gave the
				 * character: MoveBox carries it along the surface and no further, see
				 * QuadStepDeltaAt, which drops the carry outright once the quad is moving
				 * vertically at all. Handing the fall over as well makes a descending quad
				 * sticky through the back door -- every time contact breaks, the character is
				 * kicked down at the platform's own speed, which is the platform pulling it
				 * along rather than gravity, and in lumps rather than smoothly. Nothing is lost
				 * on the way up: a rising quad moves into whatever stands on it, so the push
				 * handles that one and stores what it actually pushed.
				 */
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
		if(!pEnt->GetTuning(pEnt->TuneZone())->m_MovingTiles)
			continue;

		SolidQuadPush(pEnt);
	}
}

bool CCollidableZone::SolidQuadPush(CEntity *pEnt)
{
	/*
	 * The box MoveBox collides with, so this asks about the entity's size in exactly the terms
	 * the rest of the collision does. InsideQuad takes a half extent where TestBox takes a full
	 * one, which is the only reason the two differ here.
	 */
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
			pChr->SetResendCore(true);

			pChr->m_QuadCarryVel = BestWay * BestDistance;
			pChr->m_QuadCarryTick = Server()->Tick();
		}

		return true;
	}

	return false;
}

void CCollidableZone::CollidableImpl(CEntity *pEnt, const vec2 aPoints[4], vec2 QuadMotion)
{
	const float Radius = pEnt->GetProximityRadius() * 0.55f;
	const vec2 P = pEnt->GetPos();

	const vec2 aA[4] = {aPoints[0], aPoints[1], aPoints[2], aPoints[3]};
	const vec2 aB[4] = {aPoints[1], aPoints[2], aPoints[3], aPoints[0]};

	float MinPenetration = std::numeric_limits<float>::infinity();
	vec2 BestInwardNormal = vec2(0.0f, 0.0f);
	int BestEdgeIdx = -1;
	vec2 BestEdgeVec = vec2(0.0f, 0.0f);

	for(int i = 0; i < 4; ++i)
	{
		vec2 E = aB[i] - aA[i];
		const float Elen2 = dot(E, E);
		if(Elen2 <= 1e-6f)
			continue;

		const vec2 N_in = normalize(vec2(-E.y, E.x));

		// Against the quad where it stands now, and the whole way out of it, see the note on the
		// push below
		const float Penetration = dot(P - aA[i], N_in) + Radius;

		if(Penetration < MinPenetration)
		{
			MinPenetration = Penetration;
			BestInwardNormal = N_in;
			BestEdgeIdx = i;
			BestEdgeVec = E;
		}
	}

	if(MinPenetration == std::numeric_limits<float>::infinity() || MinPenetration <= 0.0f)
		return;

	/*
	 * The whole overlap, because this runs after everything has already moved for the tick, see
	 * CCollidableZone::OnPostTick. Resolving only part of it and leaving the velocity below to
	 * cover the rest was right while this ran first, when that velocity still had the tick's
	 * movement ahead of it to act over. Now it has nothing left to act on until the next tick,
	 * so whatever the push leaves unresolved is simply how far the entity is sunk into the quad
	 * when the tick ends, which is the state that gets drawn and sent.
	 */
	const float PushDepth = MinPenetration;

	if(PushDepth > 0.0f)
	{
		const vec2 MTV = -BestInwardNormal * PushDepth;

		auto CanPlace = [&](const vec2 &Pos) {
			return !Collision()->TestBox(Pos, vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius()));
		};

		auto MoveAxis = [&](vec2 &Pos, const vec2 &Delta) {
			if(Delta.x == 0.0f && Delta.y == 0.0f)
				return vec2(0.f, 0.f);

			vec2 Target = Pos + Delta;
			if(CanPlace(Target))
			{
				Pos = Target;
				return Delta;
			}

			float lo = 0.0f;
			float hi = 1.0f;
			for(int i = 0; i < 10; ++i)
			{
				float Mid = (lo + hi) * 0.5f;
				vec2 MidPos = Pos + Delta * Mid;
				if(CanPlace(MidPos))
					lo = Mid;
				else
					hi = Mid;
			}
			if(lo > 0.0f)
			{
				vec2 Applied = Delta * lo;
				Pos += Applied;
				return Applied;
			}
			return vec2(0.0f, 0.0f);
		};

		vec2 NewPos = pEnt->m_Pos;

		vec2 AppliedX = MoveAxis(NewPos, vec2(MTV.x, 0.0f));
		vec2 AppliedY = MoveAxis(NewPos, vec2(0.0f, MTV.y));

		const vec2 Vel = pEnt->GetVelocity();
		pEnt->ForceSetPos(NewPos);

		vec2 NewVel = Vel;

		const vec2 SurfaceNormal = -BestInwardNormal;
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

			pChr->SetResendCore(true);

			if(g_Config.m_SvQStopaGivesDj && BestEdgeIdx >= 0)
			{
				const float NormalThresh = 0.35f;
				const float SlopeThresh = 0.60f;

				float edgeLen = length(BestEdgeVec);
				float edgeSlope = edgeLen > 1e-6f ? absolute(BestEdgeVec.y) / edgeLen : 1.0f;
				bool IsFloorNormal = (BestInwardNormal.y >= NormalThresh);
				bool IsFlatEnough = (edgeSlope <= SlopeThresh);
				bool PushedUp = (AppliedY.y < 0.0f);
				bool WasFallingOrRest = (Vel.y >= 0.0f);

				if(IsFloorNormal && IsFlatEnough && PushedUp && WasFallingOrRest)
					pChr->ResetJumps();
			}
		}
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

		const vec2 Pos = pChr->GetPos();
		const vec2 Size = vec2(pChr->GetProximityRadius(), pChr->GetProximityRadius()) * 0.55f;

		const auto TestQuad = [&](const CQuadData &QuadData) {
			const vec2 QuadMotion = QuadData.MotionAt(pChr->GetPos());

			// The same reference CollidableImpl resolves against, or the filter and the push
			// disagree about who is touching what
			if(!InsideQuad(Pos, QuadData, Size))
				return;

			const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};
			CollidableImpl(pChr, Points, QuadMotion);
		};

		for(const CQuadData &QuadData : Quads())
			TestQuad(QuadData);
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
		if(!pEnt->GetTuning(pEnt->TuneZone())->m_MovingTiles)
			continue;

		const vec2 Pos = pEnt->GetPos();
		const vec2 Size = vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius()) * 0.55f;

		const auto TestQuad = [&](const CQuadData &QuadData) {
			const vec2 QuadMotion = QuadData.MotionAt(pEnt->GetPos());

			// The same reference CollidableImpl resolves against, or the filter and the push
			// disagree about who is touching what
			if(!InsideQuad(Pos, QuadData, Size))
				return;

			const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};
			CollidableImpl(pEnt, Points, QuadMotion);
		};

		for(const CQuadData &QuadData : Quads())
			TestQuad(QuadData);
	}
}
