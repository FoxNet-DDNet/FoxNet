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

class CCollidableZone : public IZone
{
	void CollidableImpl(CEntity *pEnt, const vec2 aPoints[4]);

	void HandleCharacters();
	void HandlePickups();

	uint8_t m_Type;

public:
	void Init(CMapItemLayerQuads *pQuadsLayer) override;

	CCollidableZone(CGameContext *pGameContext, size_t MapIndex, uint8_t Type) :
		IZone(pGameContext, MapIndex)
	{
		m_Type = Type;
	}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
