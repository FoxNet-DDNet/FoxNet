#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/layers.h>
#include <game/quad_data.h>

#include <utility>
#include <vector>

class CGameContext;
class IServer;
class CQuad;
class CMapItemLayerQuads;

/*
 * A passive, purely spatial zone: the quads of one map layer plus the queries that go with them.
 * Zones of this kind only ever act from their own OnTick, they are never asked about players,
 * snapping or commands, which keeps them out of every dispatch loop in CZoneManager.
 * Anything that needs those hooks is a minigame, see minigame.h
 */
class CQuadZone
{
	CGameContext *m_pGameContext = nullptr;
	size_t m_MultiMapIndex = 0;
	std::vector<int> m_vAnimatedQuadIndices;

	class CAnimationTransformCache
	{
	public:
		vec2 m_Position = vec2(0.0f, 0.f);
		float m_Angle = 0;
		int m_PosEnv = -1;
		int m_PosEnvOffset = 0;
	};

protected:
	void ReserveQuads(int AdditionalQuads);
	void AddQuad(const CQuadData &QuadData);

public:
	std::vector<CQuadData> m_vQuads;

	CGameContext *GameServer() const { return m_pGameContext; }
	IServer *Server() const;
	CCollision *Collision() const;

	[[nodiscard]] const std::vector<CQuadData> &Quads() const { return m_vQuads; }
	[[nodiscard]] size_t MultiMapIndex() const { return m_MultiMapIndex; }
	[[nodiscard]] bool HasAnimatedQuads() const { return !m_vAnimatedQuadIndices.empty(); }
	[[nodiscard]] bool InsideQuad(const vec2 &Pos, const CQuadData &QuadData, const vec2 &Size = vec2(0, 0)) const;
	[[nodiscard]] vec2 RandomPointInQuad(const CQuadData &QuadData) const;
	void UpdateCache();

	CQuadZone(CGameContext *pGameContext, size_t MapIndex) :
		m_pGameContext(pGameContext), m_MultiMapIndex(MapIndex) {}

	virtual void Init(CMapItemLayerQuads *pQuadsLayer);
	virtual void OnTick() {}

	virtual ~CQuadZone() = default;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
