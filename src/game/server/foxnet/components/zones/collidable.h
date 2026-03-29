#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H

#include "zone.h"

#include <game/gamecore.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>

class CQuadData;

class CCollidableZone : public IZone
{
	void CollidableImpl(const CQuadData &QuadData, CEntity *pEnt);

	void HandleCharacters();
	void HandlePickups();

	bool m_Solid = false;

public:
	CCollidableZone(CGameContext *pGameContext, size_t MapIndex, bool Solid) :
		IZone(pGameContext, MapIndex)
	{
		m_Solid = Solid;
	}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
