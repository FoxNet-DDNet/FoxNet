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
#include <random>
#include <vector>

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
	m_GridValid = false;
	m_NeighbourCacheValid = false;
}

void CQuadZone::BuildNeighbourCache()
{
	m_NeighbourCacheValid = false;
	m_vNeighbourStart.assign(m_vQuads.size() + 1, 0);
	m_vNeighbourIndices.clear();

	if(m_vQuads.empty())
		return;

	// QueryQuads below settles whether this layer wants a grid at all
	for(size_t QuadId = 0; QuadId < m_vQuads.size(); QuadId++)
	{
		m_vNeighbourStart[QuadId] = (int)m_vNeighbourIndices.size();

		// An animated quad stands somewhere else every tick, so nothing about it is settled here
		if(m_vQuads[QuadId].m_Animated)
			continue;

		const CQuadData &QuadData = m_vQuads[QuadId];
		QueryQuads(QuadData.m_AabbMin, QuadData.m_AabbMax, m_vNeighbourScratch);
		for(int OtherId : m_vNeighbourScratch)
		{
			if(OtherId == (int)QuadId || m_vQuads[OtherId].m_Animated)
				continue;
			m_vNeighbourIndices.push_back(OtherId);
		}
	}
	m_vNeighbourStart[m_vQuads.size()] = (int)m_vNeighbourIndices.size();

	m_NeighbourCacheValid = true;
}

void CQuadZone::QueryNeighbours(int QuadId, std::vector<int> &vOut)
{
	vOut.clear();

	if(QuadId < 0 || (size_t)QuadId >= m_vQuads.size())
		return;

	if(!m_NeighbourCacheValid)
		BuildNeighbourCache();

	const CQuadData &QuadData = m_vQuads[QuadId];

	// Nothing is settled about where an animated quad stands, so this one is asked for real
	if(!m_NeighbourCacheValid || QuadData.m_Animated)
	{
		QueryQuads(QuadData.m_AabbMin, QuadData.m_AabbMax, vOut);
		vOut.erase(std::remove(vOut.begin(), vOut.end(), QuadId), vOut.end());
		return;
	}

	for(int i = m_vNeighbourStart[QuadId]; i < m_vNeighbourStart[QuadId + 1]; i++)
		vOut.push_back(m_vNeighbourIndices[i]);

	// The animated ones are few and move, so they are tested against as they stand now
	for(int OtherId : m_vAnimatedQuadIndices)
	{
		if(OtherId == QuadId)
			continue;
		const CQuadData &Other = m_vQuads[OtherId];
		if(Other.m_AabbMax.x < QuadData.m_AabbMin.x || Other.m_AabbMin.x > QuadData.m_AabbMax.x)
			continue;
		if(Other.m_AabbMax.y < QuadData.m_AabbMin.y || Other.m_AabbMin.y > QuadData.m_AabbMax.y)
			continue;
		vOut.push_back(OtherId);
	}
}

void CQuadZone::RebuildGrid()
{
	m_GridValid = false;
	m_vGridStart.clear();
	m_vGridIndices.clear();
	m_GridWidth = 0;
	m_GridHeight = 0;

	if(m_vQuads.empty())
		return;

	m_GridAabbMin = m_vQuads[0].m_AabbMin;
	m_GridAabbMax = m_vQuads[0].m_AabbMax;
	for(const CQuadData &QuadData : m_vQuads)
	{
		m_GridAabbMin.x = std::min(m_GridAabbMin.x, QuadData.m_AabbMin.x);
		m_GridAabbMin.y = std::min(m_GridAabbMin.y, QuadData.m_AabbMin.y);
		m_GridAabbMax.x = std::max(m_GridAabbMax.x, QuadData.m_AabbMax.x);
		m_GridAabbMax.y = std::max(m_GridAabbMax.y, QuadData.m_AabbMax.y);
	}

	// A handful of quads spread over a whole map would ask for a huge grid, so the cells grow
	// until the grid fits a fixed budget instead
	const vec2 Extent = m_GridAabbMax - m_GridAabbMin;
	m_GridCellSize = ms_GridMinCellSize;
	while(true)
	{
		m_GridWidth = std::max(1, (int)(Extent.x / m_GridCellSize) + 1);
		m_GridHeight = std::max(1, (int)(Extent.y / m_GridCellSize) + 1);
		if((int64_t)m_GridWidth * (int64_t)m_GridHeight <= ms_GridMaxCells)
			break;
		m_GridCellSize *= 2.0f;
	}

	const auto CellRange = [&](const CQuadData &QuadData, int *pX0, int *pY0, int *pX1, int *pY1) {
		*pX0 = std::clamp((int)((QuadData.m_AabbMin.x - m_GridAabbMin.x) / m_GridCellSize), 0, m_GridWidth - 1);
		*pX1 = std::clamp((int)((QuadData.m_AabbMax.x - m_GridAabbMin.x) / m_GridCellSize), 0, m_GridWidth - 1);
		*pY0 = std::clamp((int)((QuadData.m_AabbMin.y - m_GridAabbMin.y) / m_GridCellSize), 0, m_GridHeight - 1);
		*pY1 = std::clamp((int)((QuadData.m_AabbMax.y - m_GridAabbMin.y) / m_GridCellSize), 0, m_GridHeight - 1);
	};

	// Counting sort into one flat array, so no bucket needs an allocation of its own
	const int NumCells = m_GridWidth * m_GridHeight;
	m_vGridStart.assign(NumCells + 1, 0);
	for(const CQuadData &QuadData : m_vQuads)
	{
		int X0, Y0, X1, Y1;
		CellRange(QuadData, &X0, &Y0, &X1, &Y1);
		for(int y = Y0; y <= Y1; y++)
			for(int x = X0; x <= X1; x++)
				m_vGridStart[y * m_GridWidth + x + 1]++;
	}
	for(int Cell = 0; Cell < NumCells; Cell++)
		m_vGridStart[Cell + 1] += m_vGridStart[Cell];

	m_vGridIndices.resize(m_vGridStart[NumCells]);
	m_vGridFill.assign(m_vGridStart.begin(), m_vGridStart.end() - 1);
	for(size_t QuadIndex = 0; QuadIndex < m_vQuads.size(); QuadIndex++)
	{
		int X0, Y0, X1, Y1;
		CellRange(m_vQuads[QuadIndex], &X0, &Y0, &X1, &Y1);
		for(int y = Y0; y <= Y1; y++)
			for(int x = X0; x <= X1; x++)
				m_vGridIndices[m_vGridFill[y * m_GridWidth + x]++] = (int)QuadIndex;
	}

	m_vGridStamp.assign(m_vQuads.size(), 0);
	m_GridQueryStamp = 0;
	m_GridValid = true;
}

void CQuadZone::QueryQuads(const vec2 &Min, const vec2 &Max, std::vector<int> &vOut)
{
	vOut.clear();

	if(m_vQuads.empty())
		return;

	// A small layer is walked instead, see ms_GridMinQuads
	if(m_vQuads.size() < ms_GridMinQuads)
	{
		for(int QuadId = 0; QuadId < (int)m_vQuads.size(); QuadId++)
		{
			const CQuadData &QuadData = m_vQuads[QuadId];
			if(QuadData.m_AabbMax.x < Min.x || QuadData.m_AabbMin.x > Max.x)
				continue;
			if(QuadData.m_AabbMax.y < Min.y || QuadData.m_AabbMin.y > Max.y)
				continue;
			vOut.push_back(QuadId);
		}
		return;
	}

	if(!m_GridValid)
		RebuildGrid();

	if(!m_GridValid)
	{
		for(int QuadId = 0; QuadId < (int)m_vQuads.size(); QuadId++)
			vOut.push_back(QuadId);
		return;
	}

	if(Max.x < m_GridAabbMin.x || Min.x > m_GridAabbMax.x || Max.y < m_GridAabbMin.y || Min.y > m_GridAabbMax.y)
		return;

	const int X0 = std::clamp((int)((Min.x - m_GridAabbMin.x) / m_GridCellSize), 0, m_GridWidth - 1);
	const int X1 = std::clamp((int)((Max.x - m_GridAabbMin.x) / m_GridCellSize), 0, m_GridWidth - 1);
	const int Y0 = std::clamp((int)((Min.y - m_GridAabbMin.y) / m_GridCellSize), 0, m_GridHeight - 1);
	const int Y1 = std::clamp((int)((Max.y - m_GridAabbMin.y) / m_GridCellSize), 0, m_GridHeight - 1);

	m_GridQueryStamp++;
	for(int y = Y0; y <= Y1; y++)
	{
		for(int x = X0; x <= X1; x++)
		{
			const int Cell = y * m_GridWidth + x;
			for(int i = m_vGridStart[Cell]; i < m_vGridStart[Cell + 1]; i++)
			{
				const int QuadId = m_vGridIndices[i];
				if(m_vGridStamp[QuadId] == m_GridQueryStamp)
					continue; // a quad spanning several cells sits in each of them
				m_vGridStamp[QuadId] = m_GridQueryStamp;

				const CQuadData &QuadData = m_vQuads[QuadId];
				if(QuadData.m_AabbMax.x < Min.x || QuadData.m_AabbMin.x > Max.x)
					continue;
				if(QuadData.m_AabbMax.y < Min.y || QuadData.m_AabbMin.y > Max.y)
					continue;

				vOut.push_back(QuadId);
			}
		}
	}
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

// InsideQuad is defined inline in zone.h -- see the comment there.

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

	m_GridValid = false;
}