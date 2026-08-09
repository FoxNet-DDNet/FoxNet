#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H

#include "zone.h"

#include <base/vmath.h>

#include <game/collision.h>

#include <utility>
#include <vector>

class CFreezeZone : public CQuadZone
{
public:
	CFreezeZone(CGameContext *pGameContext, size_t MapIndex) :
		CQuadZone(pGameContext, MapIndex) {}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H
