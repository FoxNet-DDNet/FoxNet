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
};

#endif