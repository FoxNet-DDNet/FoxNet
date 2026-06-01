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

void IZone::ReserveQuads(int AdditionalQuads)
{
	if(AdditionalQuads <= 0)
		return;

	m_vQuads.reserve(m_vQuads.size() + (size_t)AdditionalQuads);
	m_vAnimatedQuadIndices.reserve(m_vAnimatedQuadIndices.size() + (size_t)AdditionalQuads);
}

void IZone::AddQuad(const CQuadData &QuadData)
{
	m_vQuads.push_back(QuadData);
	if(QuadData.m_Animated)
		m_vAnimatedQuadIndices.push_back((int)m_vQuads.size() - 1);
}

void IZone::Init(CMapItemLayerQuads *pQuadsLayer)
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

CCollision *IZone::Collision() const
{
	if(MultiMapIndex() >= GameServer()->m_vMultiMaps.size())
		return GameServer()->Collision();

	return GameServer()->Collision(MultiMapIndex());
}

IServer *IZone::Server() const
{
	return GameServer()->Server();
}

bool IZone::InsideQuad(const vec2 &Pos, const CQuadData &QuadData, const vec2 &Size) const
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

void IZone::UpdateCache()
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