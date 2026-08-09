#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H

#include "zone.h"

#include <base/vmath.h>

#include <game/collision.h>

#include <utility>
#include <vector>

class CUnfreezeZone : public CQuadZone
{
public:
	CUnfreezeZone(CGameContext *pGameContext, size_t MapIndex) :
		CQuadZone(pGameContext, MapIndex) {}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H
