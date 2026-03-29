#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_CHECKTELE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_CHECKTELE_H

#include "zone.h"

#include <game/gamecore.h>
#include <game/server/gamecontext.h>

class CCheckpointFromZone : public IZone
{
public:
	CCheckpointFromZone(CGameContext *pGameContext, size_t MapIndex) :
		IZone(pGameContext, MapIndex) {}

	void OnTick() override;

	void HandleTeleport(CEntity *pEnt);
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_CHECKTELE_H
