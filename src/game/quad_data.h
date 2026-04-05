#ifndef GAME_QUAD_DATA_H
#define GAME_QUAD_DATA_H

#include <base/vmath.h>

class CQuad;
class CMapItemLayerQuads;

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
	Num,
};

class CQuadData
{
public:
	int m_MapIndex = 0;

	CQuad *m_pQuad = nullptr;
	CMapItemLayerQuads *m_pLayer = nullptr;
	EZoneType m_Type = EZoneType::Num;
	uint8_t m_SubType = 0;
	bool m_Animated = false;
	vec2 m_LocalPos[5] = {vec2(0, 0)};
	vec2 m_Pos[5] = {vec2(0, 0)};
	float m_Angle = 0.0f;

	vec2 m_AabbMin = vec2(0, 0);
	vec2 m_AabbMax = vec2(0, 0);
	void UpdateAabb()
	{
		m_AabbMin = m_Pos[0];
		m_AabbMax = m_Pos[0];
		for(int i = 1; i < 4; i++)
		{
			if(m_Pos[i].x < m_AabbMin.x)
				m_AabbMin.x = m_Pos[i].x;
			if(m_Pos[i].y < m_AabbMin.y)
				m_AabbMin.y = m_Pos[i].y;
			if(m_Pos[i].x > m_AabbMax.x)
				m_AabbMax.x = m_Pos[i].x;
			if(m_Pos[i].y > m_AabbMax.y)
				m_AabbMax.y = m_Pos[i].y;
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