#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_DEATH_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_DEATH_H

#include <base/vmath.h>
#include <utility>
#include <vector>
#include <game/collision.h>
#include "zone.h"

class CDeathZone : public IZone
{
public:
	CDeathZone(CGameContext *pGameContext, size_t MapIndex) :
		IZone(pGameContext, MapIndex) {}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_DEATH_H
