#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H

#include "zone.h"

#include <game/gamecore.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>

class CQuadData;

enum CollidableZoneType
{
	COLLZONE_STOPA,
	COLLZONE_HOOK,
	COLLZONE_UNHOOK,
};

class CCollidableZone : public CQuadZone
{
	void CollidableImpl(CEntity *pEnt, const vec2 aPoints[4]);

	void HandleCharacters();
	void HandlePickups();

	uint8_t m_Type;

public:
	void Init(CMapItemLayerQuads *pQuadsLayer) override;

	CCollidableZone(CGameContext *pGameContext, size_t MapIndex, uint8_t Type) :
		CQuadZone(pGameContext, MapIndex)
	{
		m_Type = Type;
	}
	void OnTick() override;

	/*
	 * Runs the update and collision pass for the per-map collision quad list, which
	 * every zone that pushed its quads there (QHook / QUnHook) shares. CZoneManager
	 * calls this once per map; OnTick() deliberately does not, so the identical pass is
	 * not repeated once per quad layer.
	 */
	void TickSharedQuads();
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
