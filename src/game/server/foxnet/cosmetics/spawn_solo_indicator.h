// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_SPAWN_SOLO_INDICATOR_H
#define GAME_SERVER_FOXNET_COSMETICS_SPAWN_SOLO_INDICATOR_H

#include <game/server/foxnet/entities/foxnet_entity.h>

class CSpawnSoloIndicator : public CEntityOwned
{
public:
	CSpawnSoloIndicator(CGameWorld *pGameWorld, int Owner, vec2 Pos);
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_SPAWN_SOLO_INDICATOR_H
