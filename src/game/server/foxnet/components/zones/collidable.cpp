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

void CCollidableZone::OnTick()
{
	bool UsingQuads = GameServer()->GlobalTuning(MultiMapIndex())->m_MovingTiles;

	if(Quads().empty())
	{
		const double Time = static_cast<double>(GameServer()->Server()->Tick() - GameServer()->m_pController->m_QuadStartTick) / GameServer()->Server()->TickSpeed();
		Collision()->UpdateQuads(UsingQuads, Time);
	}

	if(!UsingQuads)
		return;
	HandleCharacters();
	HandlePickups();
}

void CCollidableZone::CollidableImpl(CEntity *pEnt, const vec2 aPoints[4])
{
	const float Radius = pEnt->GetProximityRadius() * 0.55f;
	const vec2 P = pEnt->GetPos();

	const vec2 TL = aPoints[0];
	const vec2 TR = aPoints[1];
	const vec2 BL = aPoints[2];
	const vec2 BR = aPoints[3];

	const vec2 aA[4] = {TL, TR, BL, BR};
	const vec2 aB[4] = {TR, BL, BR, TL};

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
		const float d = dot(P - aA[i], N_in);
		float Penetration = d + Radius;

		if(Penetration < MinPenetration)
		{
			MinPenetration = Penetration;
			BestInwardNormal = N_in;
			BestEdgeIdx = i;
			BestEdgeVec = E;
		}
	}

	if(MinPenetration == std::numeric_limits<float>::infinity())
		return;

	if(MinPenetration > 0.0f)
	{
		const float Epsilon = 0.0f;
		vec2 MTV = -BestInwardNormal * (MinPenetration + Epsilon);

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

		const float vIn = dot(Vel, BestInwardNormal);
		if(vIn > 0.0f)
			pEnt->SetRawVelocity(Vel - BestInwardNormal * vIn);

		if(AppliedX.x == 0.0f && MTV.x != 0.0f)
			pEnt->SetRawVelocity(vec2(0.0f, Vel.y));
		if(AppliedY.y == 0.0f && MTV.y != 0.0f)
			pEnt->SetRawVelocity(vec2(Vel.x, 0.0f));

		if(pEnt->ObjectType() == CGameWorld::ENTTYPE_CHARACTER)
		{
			CCharacter *pChr = static_cast<CCharacter *>(pEnt);

			if(!pChr)
				return;

			bool GiveJump = false;
			if(m_Type != COLLZONE_STOPA)
				GiveJump = true;
			else
				GiveJump = g_Config.m_SvQStopaGivesDj;

			pChr->SetResendCore(true);

			if(GiveJump && BestEdgeIdx >= 0)
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
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer || !pPlayer->GetCharacter())
			continue;
		if(pPlayer->MultiMapIdx() != (int)MultiMapIndex())
			continue;
		CCharacter *pChr = pPlayer->GetCharacter();
		if(!pChr || !pChr->IsAlive())
			continue;
		const vec2 Size = vec2(pChr->GetProximityRadius(), pChr->GetProximityRadius()) * 0.55f;

		if(!Quads().empty())
		{
			for(const CQuadData &QuadData : Quads())
			{
				if(!InsideQuad(pChr->GetPos(), QuadData, Size))
					continue;

				const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};
				CollidableImpl(pChr, Points);
			}
		}
		else
		{
			for(const CQuadData &QuadData : Collision()->Quads())
			{
				if(!InsideQuad(pChr->GetPos(), QuadData, Size))
					continue;

				const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};
				CollidableImpl(pChr, Points);
			}
		}
	}
}

void CCollidableZone::HandlePickups()
{
	std::vector<CEntity *> apEnts = GameServer()->m_World.EntitiesOfType(CGameWorld::ENTTYPE_PICKUPDROP);

	for(CEntity *pEnt : apEnts)
	{
		if(pEnt->MultiMapIdx() != (int)MultiMapIndex())
			continue;
		if(!pEnt->GetTuning(pEnt->TuneZone())->m_MovingTiles)
			continue;

		if(!Quads().empty())
		{
			for(const CQuadData &QuadData : Quads())
			{
				const vec2 Size = vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius()) * 0.55f;

				if(!InsideQuad(pEnt->GetPos(), QuadData, Size))
					continue;
				const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};

				CollidableImpl(pEnt, Points);
			}
		}
		else
		{
			for(const CQuadData &QuadData : Collision()->Quads())
			{
				const vec2 Size = vec2(pEnt->GetProximityRadius(), pEnt->GetProximityRadius()) * 0.55f;

				if(!InsideQuad(pEnt->GetPos(), QuadData, Size))
					continue;

				const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};

				CollidableImpl(pEnt, Points);
			}
		}
	}
}
