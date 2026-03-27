#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H

#include <base/vmath.h>
#include <utility>
#include <vector>
#include <game/collision.h>
#include <game/layers.h>
#include <game/quad_data.h>

class CGameContext;
class CQuad;
class CMapItemLayerQuads;

class IZone
{
	CGameContext *m_pGameContext = nullptr;
	size_t m_MultiMapIndex = 0;
	EZoneType m_QuadType = EZoneType::Num;

	class CAnimationTransformCache
	{
	public:
		vec2 Position = vec2(0.0f, 0.f);
		float Angle = 0;
		int PosEnv = -1;
		int PosEnvOffset = 0;
	};
	void GetAnimationTransform(int MultiMapIndex, float GlobalTime, int Env, vec2 &Position, float &Angle) const;

public:
	std::vector<CQuadData> m_vQuads;

	CGameContext *GameServer() const { return m_pGameContext; }
	CCollision *Collision() const;

	[[nodiscard]] const std::vector<CQuadData> &Quads() const { return m_vQuads; }
	[[nodiscard]] size_t MultiMapIndex() const { return m_MultiMapIndex; }

	void UpdateCache();

	IZone(CGameContext *pGameContext, size_t MapIndex, EZoneType QuadType = EZoneType::Num) :
		m_pGameContext(pGameContext), m_MultiMapIndex(MapIndex), m_QuadType(QuadType) {}

	virtual void Init(CMapItemLayerQuads *pQuadsLayer);
	virtual void OnTick() {}

	virtual ~IZone() = default;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
