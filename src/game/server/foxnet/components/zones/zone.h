#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/layers.h>
#include <game/quad_data.h>

#include <utility>
#include <vector>

class CGameContext;
class CQuad;
class CMapItemLayerQuads;

class IZone
{
	CGameContext *m_pGameContext = nullptr;
	size_t m_MultiMapIndex = 0;
	EZoneType m_QuadType = EZoneType::Num;
	std::vector<int> m_vAnimatedQuadIndices;

	class CAnimationTransformCache
	{
	public:
		vec2 m_Position = vec2(0.0f, 0.f);
		float m_Angle = 0;
		int m_PosEnv = -1;
		int m_PosEnvOffset = 0;
	};
	void GetAnimationTransform(int MultiMapIndex, float GlobalTime, int Env, vec2 &Position, float &Angle) const;

protected:
	void ReserveQuads(int AdditionalQuads);
	void InitQuadData(CQuadData &QuadData, CMapItemLayerQuads *pQuadsLayer, CQuad *pQuad) const;
	void AddQuad(const CQuadData &QuadData);

public:
	std::vector<CQuadData> m_vQuads;

	CGameContext *GameServer() const { return m_pGameContext; }
	CCollision *Collision() const;

	[[nodiscard]] const std::vector<CQuadData> &Quads() const { return m_vQuads; }
	[[nodiscard]] size_t MultiMapIndex() const { return m_MultiMapIndex; }
	[[nodiscard]] bool HasAnimatedQuads() const { return !m_vAnimatedQuadIndices.empty(); }
	[[nodiscard]] bool InsideQuad(const vec2 &Pos, const CQuadData &QuadData, const vec2 &Size = vec2(0, 0)) const;
	void UpdateCache();

	IZone(CGameContext *pGameContext, size_t MapIndex, EZoneType QuadType = EZoneType::Num) :
		m_pGameContext(pGameContext), m_MultiMapIndex(MapIndex), m_QuadType(QuadType) {}

	virtual void Init(CMapItemLayerQuads *pQuadsLayer);
	virtual void OnTick() {}

	virtual ~IZone() = default;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
