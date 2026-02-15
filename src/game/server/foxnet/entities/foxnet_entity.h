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

class CFoxNetEntity : public CEntity
{
public:
	int m_Owner;

	CFoxNetEntity(CGameWorld *pGameWorld, CCollision *pCollision, int Objtype, vec2 Pos = vec2(0, 0), int ProximityRadius = 0);

	bool CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer = nullptr);

	CPlayer *GetPlayer();
	CCharacter *GetCharacter();	

	CClientMask CosmeticMask(const EItemType ItemType);
	CClientMask TeamMask();


	void Reset() override {}
	void Tick() override {}
	void TickDeferred() override {}
	void TickPaused() override {}
	void Snap(int SnappingClient) override {}
};

#endif // GAME_SERVER_FOXNET_COSMETICS_FOXNET_ENTITY_H