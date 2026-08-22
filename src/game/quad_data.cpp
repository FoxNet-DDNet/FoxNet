#include "quad_data.h"

#include "envelopes.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/map.h>

#include <game/mapitems.h>

#include <limits>

class CQuad;

namespace
{
	struct SEnvelopeExtrema
	{
		bool m_Available = false;
		bool m_Rotating = false;
		vec2 m_Min = vec2(0.0f, 0.0f);
		vec2 m_Max = vec2(0.0f, 0.0f);
	};

	SEnvelopeExtrema GetEnvelopeExtrema(IMap *pMap, int Env)
	{
		SEnvelopeExtrema Extrema;
		if(!pMap || Env < 0)
		{
			Extrema.m_Available = Env == -1;
			return Extrema;
		}

		int Start, Num;
		pMap->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		if(Env >= Num)
			return Extrema;

		const CMapItemEnvelope *pItem = static_cast<const CMapItemEnvelope *>(pMap->GetItem(Start + Env, nullptr, nullptr));
		if(!pItem || pItem->m_Channels != 3)
			return Extrema;

		CMapBasedEnvelopeAccess EnvelopePoints(pMap);
		EnvelopePoints.SetPointsRange(pItem->m_StartPoint, pItem->m_NumPoints);
		if(EnvelopePoints.NumPoints() == 0)
			return Extrema;

		Extrema.m_Min = vec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		Extrema.m_Max = vec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		for(int PointId = 0; PointId < EnvelopePoints.NumPoints(); ++PointId)
		{
			const CEnvPoint *pEnvPoint = EnvelopePoints.GetPoint(PointId);
			if(!pEnvPoint)
				return {};

			if(pEnvPoint->m_aValues[2] != 0)
				Extrema.m_Rotating = true;

			const vec2 Value(fx2f(pEnvPoint->m_aValues[0]), fx2f(pEnvPoint->m_aValues[1]));
			Extrema.m_Min.x = std::min(Extrema.m_Min.x, Value.x);
			Extrema.m_Min.y = std::min(Extrema.m_Min.y, Value.y);
			Extrema.m_Max.x = std::max(Extrema.m_Max.x, Value.x);
			Extrema.m_Max.y = std::max(Extrema.m_Max.y, Value.y);

			if(PointId < EnvelopePoints.NumPoints() - 1 && pEnvPoint->m_Curvetype == CURVETYPE_BEZIER)
			{
				const CEnvPointBezier *pBezier = EnvelopePoints.GetBezier(PointId);
				if(!pBezier)
					return {};

				const vec2 OutValue(
					fx2f(pEnvPoint->m_aValues[0] + pBezier->m_aOutTangentDeltaY[0]),
					fx2f(pEnvPoint->m_aValues[1] + pBezier->m_aOutTangentDeltaY[1]));
				Extrema.m_Min.x = std::min(Extrema.m_Min.x, OutValue.x);
				Extrema.m_Min.y = std::min(Extrema.m_Min.y, OutValue.y);
				Extrema.m_Max.x = std::max(Extrema.m_Max.x, OutValue.x);
				Extrema.m_Max.y = std::max(Extrema.m_Max.y, OutValue.y);
			}

			if(PointId > 0)
			{
				const CEnvPoint *pPrevPoint = EnvelopePoints.GetPoint(PointId - 1);
				if(pPrevPoint && pPrevPoint->m_Curvetype == CURVETYPE_BEZIER)
				{
					const CEnvPointBezier *pBezier = EnvelopePoints.GetBezier(PointId);
					if(!pBezier)
						return {};

					const vec2 InValue(
						fx2f(pEnvPoint->m_aValues[0] + pBezier->m_aInTangentDeltaY[0]),
						fx2f(pEnvPoint->m_aValues[1] + pBezier->m_aInTangentDeltaY[1]));
					Extrema.m_Min.x = std::min(Extrema.m_Min.x, InValue.x);
					Extrema.m_Min.y = std::min(Extrema.m_Min.y, InValue.y);
					Extrema.m_Max.x = std::max(Extrema.m_Max.x, InValue.x);
					Extrema.m_Max.y = std::max(Extrema.m_Max.y, InValue.y);
				}
			}
		}

		Extrema.m_Available = true;
		return Extrema;
	}
}

void CQuadData::Init(CQuad *pQuad, IMap *pMap)
{
	m_pQuad = pQuad;
	for(int i = 0; i < 5; i++)
		m_aLocalPoints[i] = vec2(fx2f(pQuad->m_aPoints[i].x), fx2f(pQuad->m_aPoints[i].y));
	std::swap(m_aLocalPoints[2], m_aLocalPoints[3]);
	for(int i = 0; i < 5; i++)
		m_aPoints[i] = m_aLocalPoints[i];
	m_Animated = pQuad->m_PosEnv >= 0;
	m_PrevPivot = m_aPoints[4];
	m_PrevAngle = 0.0f;
	UpdateAabb();

	if(!m_Animated || !pMap)
		return;

	const SEnvelopeExtrema EnvExtrema = GetEnvelopeExtrema(pMap, pQuad->m_PosEnv);
	if(!EnvExtrema.m_Available)
		return;

	if(EnvExtrema.m_Rotating)
	{
		const vec2 Pivot = m_aLocalPoints[4];
		float Radius = 0.0f;
		for(int i = 0; i < 4; ++i)
			Radius = std::max(Radius, distance(m_aLocalPoints[i], Pivot));

		m_AabbMin = vec2(Pivot.x + EnvExtrema.m_Min.x - Radius, Pivot.y + EnvExtrema.m_Min.y - Radius);
		m_AabbMax = vec2(Pivot.x + EnvExtrema.m_Max.x + Radius, Pivot.y + EnvExtrema.m_Max.y + Radius);
	}
	else
	{
		m_AabbMin += EnvExtrema.m_Min;
		m_AabbMax += EnvExtrema.m_Max;
	}
}

void CQuadData::UpdatePositionEnvelope(double Time, IMap *pMap)
{
	auto GetAnimationTransform = [&](vec2 &Position, float &Angle) {
		Position.x = 0.0f;
		Position.y = 0.0f;
		Angle = 0.0f;

		const int Env = m_pQuad->m_PosEnv;
		const double Offset = m_pQuad->m_PosEnvOffset / 1000.0;

		if(Env < 0)
			return;

		int Start, Num;
		pMap->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		if(Env >= Num)
			return;

		CMapItemEnvelope *pItem = (CMapItemEnvelope *)pMap->GetItem(Start + Env, 0, 0);
		if(pItem->m_NumPoints == 0)
			return;

		CMapBasedEnvelopeAccess EnvelopePoints(pMap);
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
		double GlobalMillis = (double)(Time + Offset) * 1000.0;
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
			a = -2.0f * a * a * a + 3.0f * a * a;
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
		const float X0 = fx2f(pCur->m_aValues[0]);
		const float X1 = fx2f(pNext->m_aValues[0]);
		const float Y0 = fx2f(pCur->m_aValues[1]);
		const float Y1 = fx2f(pNext->m_aValues[1]);
		const float R0 = fx2f(pCur->m_aValues[2]);
		const float R1 = fx2f(pNext->m_aValues[2]);
		Position.x = X0 + (X1 - X0) * a;
		Position.y = Y0 + (Y1 - Y0) * a;
		Angle = (R0 + (R1 - R0) * a) / 360.0f * pi * 2.0f;
	};

	m_PrevPivot = m_aPoints[4];
	m_PrevAngle = m_Angle;

	vec2 Position = vec2(0, 0);
	GetAnimationTransform(Position, m_Angle);

	for(int i = 0; i < 5; i++)
		m_aPoints[i] = Position + m_aLocalPoints[i];

	if(m_Angle != 0)
	{
		for(int i = 0; i < 4; i++)
			Rotate(m_aPoints[4], &m_aPoints[i], m_Angle);
	}
}
