#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/layers.h>
#include <game/quad_data.h>

#include <algorithm>
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

	// Same uniform grid as the collision's own, see CCollision::RebuildQuadGrid. A layer of
	// thousands is walked per entity per tick without it, and the sweep asks several times a tick.
	static constexpr int ms_GridMaxCells = 1 << 16;
	static constexpr float ms_GridMinCellSize = 128.0f;
	/*
	 * Below this a bounds compare per quad answers faster than any grid lookup, and a layer whose
	 * quads are animated throws the grid away every tick -- a handful of quads spread over a map
	 * asks for thousands of cells, so building it cost far more than the walk it saved.
	 */
	static constexpr size_t ms_GridMinQuads = 64;
	vec2 m_GridAabbMin = vec2(0.0f, 0.0f);
	vec2 m_GridAabbMax = vec2(0.0f, 0.0f);
	float m_GridCellSize = ms_GridMinCellSize;
	int m_GridWidth = 0;
	int m_GridHeight = 0;
	std::vector<int> m_vGridStart; // start offset into m_vGridIndices per cell, plus a final end
	std::vector<int> m_vGridIndices;
	std::vector<int> m_vGridFill; // scratch for the rebuild, kept to kill a per tick allocation
	std::vector<int> m_vGridStamp; // last query that already took each quad, so none comes twice
	int m_GridQueryStamp = 0;
	bool m_GridValid = false;
	void RebuildGrid();

	/*
	 * Each static quad's static neighbours, settled once. Asking the grid for them per contact
	 * reads the quad's own bounds, and one long diagonal quad covers a great many cells, so that
	 * walked thousands of entries to come back with a dozen. Which static quads overlap which
	 * never changes, and an animated one is handled apart, see QueryNeighbours.
	 */
	std::vector<int> m_vNeighbourStart; // per quad, offset into the indices below, plus a final end
	std::vector<int> m_vNeighbourIndices;
	std::vector<int> m_vNeighbourScratch;
	bool m_NeighbourCacheValid = false;
	void BuildNeighbourCache();

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
	/*
	 * Defined here rather than in the .cpp on purpose. The AABB reject is the branch
	 * almost every call takes -- a few float comparisons -- and out of line it cost a
	 * full call each time, which is a lot when it runs once per entity per quad.
	 * InsideQuadrilateral is itself inline in vmath.h, so the whole thing folds in.
	 */
	[[nodiscard]] bool InsideQuad(const vec2 &Pos, const CQuadData &QuadData, const vec2 &Size = vec2(0, 0)) const
	{
		if(Size.x == 0 && Size.y == 0)
		{
			if(!QuadData.AabbContains(Pos))
				return false;
		}
		else if(!QuadData.AabbIntersects(Pos, Size))
		{
			return false;
		}

		const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};

		return ::InsideQuadrilateral(Pos, Points, Size);
	}
	/*
	 * Whether the path an entity took this tick touches the quad anywhere along it, rather than
	 * only where it ended up. A quad thinner than one tick of travel is otherwise passed straight
	 * through at speed, the same skip the push had, see CCollidableZone::SweepResolve. This one
	 * needs no sampling: nothing is resolved, so the question is only whether the path crossed it.
	 */
	[[nodiscard]] bool SegmentInsideQuad(const vec2 &From, const vec2 &To, const CQuadData &QuadData) const
	{
		if(std::max(From.x, To.x) < QuadData.m_AabbMin.x || std::min(From.x, To.x) > QuadData.m_AabbMax.x)
			return false;
		if(std::max(From.y, To.y) < QuadData.m_AabbMin.y || std::min(From.y, To.y) > QuadData.m_AabbMax.y)
			return false;

		const vec2 Points[4] = {QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[2], QuadData.m_aPoints[3]};

		return ::SegmentHitsQuadrilateral(From, To, Points);
	}
	[[nodiscard]] vec2 RandomPointInQuad(const CQuadData &QuadData) const;
	// Indices of the quads whose bounds reach this box, replacing whatever vOut held
	void QueryQuads(const vec2 &Min, const vec2 &Max, std::vector<int> &vOut);
	// The quads overlapping this one, itself excluded, replacing whatever vOut held
	void QueryNeighbours(int QuadId, std::vector<int> &vOut);
	void UpdateCache();

	CQuadZone(CGameContext *pGameContext, size_t MapIndex) :
		m_pGameContext(pGameContext), m_MultiMapIndex(MapIndex) {}

	virtual void Init(CMapItemLayerQuads *pQuadsLayer);
	virtual void OnTick() {}
	// see CServerComponent::OnPostTick
	virtual void OnPostTick() {}

	virtual ~CQuadZone() = default;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
