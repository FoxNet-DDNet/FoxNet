#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H

#include <base/vmath.h>
#include <utility>
#include <vector>
#include <game/collision.h>
#include "zone.h"

class CFreezeZone : public IZone
{
public:
	CFreezeZone(CGameContext *pGameContext, size_t MapIndex) :
		IZone(pGameContext, MapIndex) {}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_UNFREEZE_H
