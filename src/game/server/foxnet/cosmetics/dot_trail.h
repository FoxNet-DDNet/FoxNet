// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_DOT_TRAIL_H
#define GAME_SERVER_FOXNET_COSMETICS_DOT_TRAIL_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CDotTrail : public CEntityOwned
{
public:
	CDotTrail(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_DOT_TRAIL_H
