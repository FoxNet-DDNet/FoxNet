#include "zone.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/map.h>
#include <engine/server.h>

#include <game/collision.h>
#include <game/envelopes.h>
#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <random>

void CQuadZone::ReserveQuads(int AdditionalQuads)
{
	if(AdditionalQuads <= 0)
		return;

	m_vQuads.reserve(m_vQuads.size() + (size_t)AdditionalQuads);
	m_vAnimatedQuadIndices.reserve(m_vAnimatedQuadIndices.size() + (size_t)AdditionalQuads);
}

void CQuadZone::AddQuad(const CQuadData &QuadData)
{
	m_vQuads.push_back(QuadData);
	if(QuadData.m_Animated)
		m_vAnimatedQuadIndices.push_back((int)m_vQuads.size() - 1);
}

void CQuadZone::Init(CMapItemLayerQuads *pQuadsLayer)
{
	CQuad *pQuads = (CQuad *)GameServer()->Map(MultiMapIndex())->GetDataSwapped(pQuadsLayer->m_Data);
	ReserveQuads(pQuadsLayer->m_NumQuads);
	for(int NumQuads = 0; NumQuads < pQuadsLayer->m_NumQuads; NumQuads++)
	{
		CQuadData QuadData;
		QuadData.Init(&pQuads[NumQuads], GameServer()->Map(MultiMapIndex()));
		AddQuad(QuadData);
	}
}

CCollision *CQuadZone::Collision() const
{
	if(MultiMapIndex() >= GameServer()->m_vMultiMaps.size())
		return GameServer()->Collision();

	return GameServer()->Collision(MultiMapIndex());
}

IServer *CQuadZone::Server() const
{
	return GameServer()->Server();
}

bool CQuadZone::InsideQuad(const vec2 &Pos, const CQuadData &QuadData, const vec2 &Size) const
{
	if(Size.x == 0 && Size.y == 0)
	{
		if(!QuadData.AabbContains(Pos))
			return false;
	}
	else
	{
		if(!QuadData.AabbIntersects(Pos, Size))
			return false;
	}
	const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};

	return ::InsideQuadrilateral(Pos, Points, Size);
}

static float TriangleArea(const vec2 &A, const vec2 &B, const vec2 &C)
{
	return std::abs(
		       (B.x - A.x) * (C.y - A.y) -
		       (B.y - A.y) * (C.x - A.x)) *
	       0.5f;
}

static vec2 RandomPointInTriangle(const vec2 &A, const vec2 &B, const vec2 &C)
{
	float r1 = random_float();
	float r2 = random_float();

	float s = std::sqrt(r1);

	float u = 1.0f - s;
	float v = s * (1.0f - r2);
	float w = s * r2;

	return A * u + B * v + C * w;
}

vec2 CQuadZone::RandomPointInQuad(const CQuadData &QuadData) const
{
	const vec2 &A = QuadData.m_aPoints[0];
	const vec2 &B = QuadData.m_aPoints[1];
	const vec2 &C = QuadData.m_aPoints[2];
	const vec2 &D = QuadData.m_aPoints[3];

	float AreaABC = TriangleArea(A, B, C);
	float AreaACD = TriangleArea(A, C, D);

	std::uniform_real_distribution<float> Range(0.001f, 1.0f);
	float Pick = Range(Rng()) * (AreaABC + AreaACD);

	if(Pick < AreaABC)
		return RandomPointInTriangle(A, B, C);

	return RandomPointInTriangle(A, C, D);
}

void CQuadZone::UpdateCache()
{
	if(m_vAnimatedQuadIndices.empty())
		return;

	const double Time = GameServer()->m_pController->GetTime();

	for(int QuadIndex : m_vAnimatedQuadIndices)
	{
		CQuadData &QuadData = m_vQuads[QuadIndex];
		QuadData.UpdatePositionEnvelope(Time, Collision()->Layers()->Map());
		QuadData.UpdateAabb();
	}
}