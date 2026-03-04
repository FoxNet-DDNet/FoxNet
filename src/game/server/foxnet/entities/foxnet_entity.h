// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_FOXNET_ENTITY_H
#define GAME_SERVER_FOXNET_COSMETICS_FOXNET_ENTITY_H

#include <game/server/entity.h>
#include <base/vmath.h>
#include <engine/shared/protocol.h>
#include <game/collision.h>
#include <game/server/foxnet/item_registry.h>

class CGameWorld;
class CPlayer;
class CCharacter;

class CEntityOwned : public CEntity
{
public:
	int m_Owner;
	CClientMask m_StartTeamMask = CClientMask().set();

	CEntityOwned(CGameWorld *pGameWorld, int Owner, int Objtype, vec2 Pos = vec2(0, 0), int ProximityRadius = 0);

	bool CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer = nullptr);

	CPlayer *GetPlayer();
	CCharacter *GetCharacter();
	// Gets the actual collision of the map thats loaded
	CCollision *GetCollision();

	CClientMask CosmeticMask(const EItemType ItemType);
	CClientMask TeamMask();

	int GetOwnerId() const override { return m_Owner; }

	void Reset() override {}
	void Tick() override {}
	void TickDeferred() override {}
	void TickPaused() override {}
	void Snap(int SnappingClient) override {}
};

#endif // GAME_SERVER_FOXNET_COSMETICS_FOXNET_ENTITY_H