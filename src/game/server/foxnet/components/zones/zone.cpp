#include "zone.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/map.h>

#include <game/collision.h>
#include <game/envelopeaccess.h>
#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

void IZone::GetAnimationTransform(int MultiMapIndex, float GlobalTime, int Env, vec2 &Position, float &Angle) const
{
	Position.x = 0.0f;
	Position.y = 0.0f;
	Angle = 0.0f;

	if(Env < 0)
		return;

	int Start, Num;
	IMap *pMap = GameServer()->Map(MultiMapIndex);
	pMap->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
	if(Env >= Num)
		return;

	CMapItemEnvelope *pItem = (CMapItemEnvelope *)pMap->GetItem(Start + Env, 0, 0);
	if(pItem->m_NumPoints == 0)
		return;

	CMapBasedEnvelopePointAccess EnvelopePoints(pMap);
	EnvelopePoints.SetPointsRange(pItem->m_StartPoint, pItem->m_NumPoints);
	if(EnvelopePoints.NumPoints() == 0)
		return;

	// Single point shortcut
	if(EnvelopePoints.NumPoints() == 1)
	{
		const CEnvPoint *pOnly = EnvelopePoints.GetPoint(0);
		Position.x = fx2f(pOnly->m_aValues[0]);
		Position.y = fx2f(pOnly->m_aValues[1]);
		Angle = fx2f(pOnly->m_aValues[2]) / 360.0f * pi * 2.0f;
		return;
	}

	const int NumPoints = EnvelopePoints.NumPoints();
	const CEnvPoint *pLastPoint = EnvelopePoints.GetPoint(NumPoints - 1);

	// Convert GlobalTime (seconds) to milliseconds like RenderEvalEnvelope logic
	double GlobalMillis = (double)GlobalTime * 1000.0;
	const int64_t LoopMillis = (int64_t)pLastPoint->m_Time.GetInternal();
	if(LoopMillis > 0)
		GlobalMillis = std::fmod(GlobalMillis, (double)LoopMillis);
	else
		GlobalMillis = 0.0; // degenerate envelope

	// Locate current segment
	int FoundIndex = EnvelopePoints.FindPointIndex(CFixedTime(GlobalMillis));
	if(FoundIndex == -1)
	{
		// After last point
		Position.x = fx2f(pLastPoint->m_aValues[0]);
		Position.y = fx2f(pLastPoint->m_aValues[1]);
		Angle = fx2f(pLastPoint->m_aValues[2]) / 360.0f * pi * 2.0f;
		return;
	}

	const CEnvPoint *pCur = EnvelopePoints.GetPoint(FoundIndex);
	const CEnvPoint *pNext = EnvelopePoints.GetPoint(FoundIndex + 1);
	CFixedTime Delta = pNext->m_Time - pCur->m_Time;
	if(Delta <= CFixedTime(0))
	{
		Position.x = fx2f(pCur->m_aValues[0]);
		Position.y = fx2f(pCur->m_aValues[1]);
		Angle = fx2f(pCur->m_aValues[2]) / 360.0f * pi * 2.0f;
		return;
	}

	float a = (float)(GlobalMillis - pCur->m_Time.GetInternal()) / (float)Delta.GetInternal();
	switch(pCur->m_Curvetype)
	{
	case CURVETYPE_STEP:
		a = 0.0f;
		break;
	case CURVETYPE_SLOW:
		a = a * a * a;
		break;
	case CURVETYPE_FAST:
		a = 1.0f - a;
		a = 1.0f - a * a * a;
		break;
	case CURVETYPE_SMOOTH:
		a = -2.0f * a * a * a + 3.0f * a * a; // Hermite smoothstep
		break;
	case CURVETYPE_BEZIER:
	{
		const CEnvPointBezier *pCurBez = EnvelopePoints.GetBezier(FoundIndex);
		const CEnvPointBezier *pNextBez = EnvelopePoints.GetBezier(FoundIndex + 1);
		if(pCurBez && pNextBez)
		{
			float Channels[3] = {0.f, 0.f, 0.f};
			for(size_t c = 0; c < 3; ++c)
			{
				// 2D cubic bezier in (time,value) space (time in ms)
				vec2 P0 = vec2(pCur->m_Time.GetInternal(), fx2f(pCur->m_aValues[c]));
				vec2 P3 = vec2(pNext->m_Time.GetInternal(), fx2f(pNext->m_aValues[c]));
				vec2 OutTang = vec2(pCurBez->m_aOutTangentDeltaX[c].GetInternal(), fx2f(pCurBez->m_aOutTangentDeltaY[c]));
				vec2 InTang = vec2(pNextBez->m_aInTangentDeltaX[c].GetInternal(), fx2f(pNextBez->m_aInTangentDeltaY[c]));
				vec2 P1 = P0 + OutTang;
				vec2 P2 = P3 + InTang;
				P1.x = std::clamp(P1.x, P0.x, P3.x);
				P2.x = std::clamp(P2.x, P0.x, P3.x);
				float t = std::clamp(SolveBezier((float)GlobalMillis, P0.x, P1.x, P2.x, P3.x), 0.0f, 1.0f);
				Channels[c] = bezier(P0.y, P1.y, P2.y, P3.y, t);
			}
			Position.x = Channels[0];
			Position.y = Channels[1];
			Angle = Channels[2] / 360.0f * pi * 2.0f;
			return; // Bezier done
		}
		// fallthrough to linear if bezier data missing
		break;
	}
	case CURVETYPE_LINEAR:
	default:
		break; // linear handled below
	}

	// Linear interpolation (or shaped 'a')
	const float x0 = fx2f(pCur->m_aValues[0]);
	const float x1 = fx2f(pNext->m_aValues[0]);
	const float y0 = fx2f(pCur->m_aValues[1]);
	const float y1 = fx2f(pNext->m_aValues[1]);
	const float r0 = fx2f(pCur->m_aValues[2]);
	const float r1 = fx2f(pNext->m_aValues[2]);
	Position.x = x0 + (x1 - x0) * a;
	Position.y = y0 + (y1 - y0) * a;
	Angle = (r0 + (r1 - r0) * a) / 360.0f * pi * 2.0f;
}

void IZone::ReserveQuads(int AdditionalQuads)
{
	if(AdditionalQuads <= 0)
		return;

	m_vQuads.reserve(m_vQuads.size() + (size_t)AdditionalQuads);
	m_vAnimatedQuadIndices.reserve(m_vAnimatedQuadIndices.size() + (size_t)AdditionalQuads);
}

void IZone::InitQuadData(CQuadData &QuadData, CMapItemLayerQuads *pQuadsLayer, CQuad *pQuad) const
{
	QuadData.m_pQuad = pQuad;
	QuadData.m_pLayer = pQuadsLayer;
	QuadData.m_Type = m_QuadType;
	QuadData.m_MapIndex = m_MultiMapIndex;
	QuadData.m_Animated = pQuad->m_PosEnv >= 0;

	for(int j = 0; j < 5; j++)
	{
		QuadData.m_LocalPos[j] = vec2(fx2f(pQuad->m_aPoints[j].x), fx2f(pQuad->m_aPoints[j].y));
		QuadData.m_Pos[j] = QuadData.m_LocalPos[j];
	}
	QuadData.UpdateAabb();
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
		InitQuadData(QuadData, pQuadsLayer, &pQuads[NumQuads]);
		AddQuad(QuadData);
	}
}

CCollision *IZone::Collision() const
{
	if(MultiMapIndex() >= GameServer()->m_vMultiMaps.size())
		return GameServer()->Collision();

	return GameServer()->Collision(MultiMapIndex());
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
	const vec2 Points[4] = {QuadData.m_Pos[0], QuadData.m_Pos[1], QuadData.m_Pos[3], QuadData.m_Pos[2]};
	
	return ::InsideQuad(Pos, Points, Size);
}

void IZone::UpdateCache()
{
	if(m_vAnimatedQuadIndices.empty())
		return;

	const double Time = GameServer()->m_pController->GetTime();

	for(int QuadIndex : m_vAnimatedQuadIndices)
	{
		CQuadData &QuadData = m_vQuads[QuadIndex];

		vec2 Position = vec2(0, 0);
		GetAnimationTransform(QuadData.m_MapIndex, Time + (QuadData.m_pQuad->m_PosEnvOffset / 1000.0), QuadData.m_pQuad->m_PosEnv, Position, QuadData.m_Angle);

		for(int i = 0; i < 5; i++)
			QuadData.m_Pos[i] = Position + QuadData.m_LocalPos[i];

		if(QuadData.m_Angle != 0)
		{
			for(int i = 0; i < 4; i++)
				Rotate(QuadData.m_Pos[4], &QuadData.m_Pos[i], QuadData.m_Angle);
		}

		QuadData.UpdateAabb();
	}
}