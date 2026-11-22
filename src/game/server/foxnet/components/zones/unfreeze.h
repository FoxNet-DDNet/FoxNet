#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H

#include "zone.h"

#include <base/vmath.h>

#include <game/collision.h>

#include <utility>
#include <vector>

class CUnfreezeZone : public CQuadZone
{
	// Grid answers, a member only to keep an allocation off the per player path
	std::vector<int> m_vCandidates;

public:
	CUnfreezeZone(CGameContext *pGameContext, size_t MapIndex) :
		CQuadZone(pGameContext, MapIndex) {}
	void OnPostTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H
