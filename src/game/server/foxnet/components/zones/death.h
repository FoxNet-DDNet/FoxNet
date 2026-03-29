#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_DEATH_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_DEATH_H

#include "zone.h"

#include <base/vmath.h>

#include <game/collision.h>

#include <utility>
#include <vector>

class CDeathZone : public IZone
{
public:
	CDeathZone(CGameContext *pGameContext, size_t MapIndex) :
		IZone(pGameContext, MapIndex) {}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_DEATH_H
