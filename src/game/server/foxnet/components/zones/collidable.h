#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H

#include "zone.h"

#include <game/gamecore.h>
#include <game/quad_data.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>

#include <vector>

enum CollidableZoneType
{
	COLLZONE_STOPA,
	COLLZONE_HOOK,
	COLLZONE_UNHOOK,
};

class CCollidableZone : public CQuadZone
{
	void CollidableImpl(CEntity *pEnt, int QuadId, vec2 QuadMotion);

	// Scratch for CollidableImpl's seam test, a member only to keep it off the per-push path
	std::vector<const CQuadData *> m_vpNeighbours;
	// Grid answers, kept here for the same reason. Three lists because the passes nest.
	std::vector<int> m_vNeighbourIds;
	std::vector<int> m_vSweepCandidates;
	std::vector<int> m_vOverlapCandidates;

	// Floor on the sampling step, so a sliver of a quad cannot ask for an unbounded number
	static constexpr float ms_MinSampleStep = 4.0f;
	// Ceiling on how many samples one tick of travel is cut into
	static constexpr int ms_MaxSampleSteps = 64;

	// How far apart this tick's travel may be sampled without a quad on it being stepped over
	float SampleStep(CEntity *pEnt, const vec2 &From, const vec2 &To);
	/*
	 * Runs the push at points along the travel rather than only at its end, which is what keeps
	 * a quad from being stepped over at speed. Travel short enough to be in no danger of that is
	 * one sample at the end, which is the behaviour this always had.
	 */
	void SweepResolve(CEntity *pEnt);
	// The push itself, unchanged: every contact is still resolved here and only here
	void ResolveOverlaps(CEntity *pEnt);

	void HandleCharacters();
	void HandlePickups();

	void HandleSolidQuads();
	bool SolidQuadPush(CEntity *pEnt);

	uint8_t m_Type;

public:
	void Init(CMapItemLayerQuads *pQuadsLayer) override;

	CCollidableZone(CGameContext *pGameContext, size_t MapIndex, uint8_t Type) :
		CQuadZone(pGameContext, MapIndex)
	{
		m_Type = Type;
	}
	void OnPostTick() override;

	void TickSharedQuads();
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
