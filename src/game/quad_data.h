#ifndef GAME_QUAD_DATA_H
#define GAME_QUAD_DATA_H

#include <base/vmath.h>

#include <engine/map.h>

#include <game/mapitems.h>

class CQuad;

enum class EZoneType
{
	Freeze,
	Unfreeze,
	Death,
	StopA,
	CFRM,
	Hookable,
	Unhookable,
	Roulette,
	HideNSeek,
	Num,
};

class CQuadData
{
public:
	size_t m_MapIndex = 0;

	CQuad *m_pQuad = nullptr;

	vec2 m_aPoints[5] = {vec2(0, 0)}; // 4 corners + pivot
	vec2 m_aLocalPoints[5] = {vec2(0, 0)}; // the original points as defined in the map, used for animation

	bool m_Animated = false;
	float m_Angle = 0.0f;
	uint8_t m_SubType = 0;

	vec2 m_AabbMin = vec2(0, 0);
	vec2 m_AabbMax = vec2(0, 0);

	void Init(CQuad *pQuad);
	void UpdatePositionEnvelope(float GlobalTime, IMap *pMap);

	void UpdateAabb()
	{
		m_AabbMin = m_aPoints[0];
		m_AabbMax = m_aPoints[0];
		for(int i = 1; i < 4; i++)
		{
			if(m_aPoints[i].x < m_AabbMin.x)
				m_AabbMin.x = m_aPoints[i].x;
			if(m_aPoints[i].y < m_AabbMin.y)
				m_AabbMin.y = m_aPoints[i].y;
			if(m_aPoints[i].x > m_AabbMax.x)
				m_AabbMax.x = m_aPoints[i].x;
			if(m_aPoints[i].y > m_AabbMax.y)
				m_AabbMax.y = m_aPoints[i].y;
		}
	}
	bool AabbContains(const vec2 &Pos) const
	{
		return Pos.x >= m_AabbMin.x && Pos.x <= m_AabbMax.x && Pos.y >= m_AabbMin.y && Pos.y <= m_AabbMax.y;
	}
	bool AabbIntersects(const vec2 &Pos, const vec2 &Size) const
	{
		return Pos.x + Size.x >= m_AabbMin.x && Pos.x - Size.x <= m_AabbMax.x && Pos.y + Size.y >= m_AabbMin.y && Pos.y - Size.y <= m_AabbMax.y;
	}
};

#endif