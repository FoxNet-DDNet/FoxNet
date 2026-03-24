#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H

#include <base/vmath.h>
#include <utility>
#include <vector>
#include <game/collision.h>
#include "zone.h"

class CUnfreezeZone : public IZone
{
public:
	CUnfreezeZone(CGameContext *pGameContext, size_t MapIndex) :
		IZone(pGameContext, MapIndex) {}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_FREEZE_H
