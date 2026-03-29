#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H

#include "zone.h"

#include <base/vmath.h>

#include <game/collision.h>

#include <utility>
#include <vector>

class CFreezeZone : public IZone
{
public:
	CFreezeZone(CGameContext *pGameContext, size_t MapIndex) :
		IZone(pGameContext, MapIndex) {}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H
