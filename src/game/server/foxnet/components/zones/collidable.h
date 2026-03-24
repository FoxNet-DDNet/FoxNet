#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H

#include "zone.h"
#include <game/gamecore.h>
#include <game/server/gamecontext.h>
#include <game/server/entity.h>

class CQuadData;

class CCollidableZone : public IZone
{
	bool m_AlwaysGivesJump;

	void CollidableImpl(const CQuadData &QuadData, CEntity *pEnt);

	void HandleCharacters();
	void HandlePickups();

public:
	CCollidableZone(CGameContext *pGameContext, size_t MapIndex, bool AlwaysGivesJump) :
		IZone(pGameContext, MapIndex)
	{
		m_AlwaysGivesJump = AlwaysGivesJump;
	}
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_COLLIDABLE_H
