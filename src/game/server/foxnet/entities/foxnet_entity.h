// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_FOXNET_ENTITY_H
#define GAME_SERVER_FOXNET_ENTITIES_FOXNET_ENTITY_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/collision.h>
#include <game/server/entity.h>
#include <game/server/foxnet/item_registry.h>

class CGameWorld;
class CPlayer;
class CCharacter;
class CSnapContext;

class CEntityOwned : public CEntity
{
public:
	int m_Owner;
	CClientMask m_StartTeamMask = CClientMask().set();

	CEntityOwned(CGameWorld *pGameWorld, int Owner, int Objtype, vec2 Pos = vec2(0, 0), int ProximityRadius = 0);

	bool CanSnapEntityNoChar(int SnappingClient, CPlayer **ppSnapPlayer = nullptr);
	bool CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer = nullptr);

	CPlayer *GetPlayer();
	CCharacter *GetCharacter();
	// Gets the actual collision of the map thats loaded
	CCollision *GetCollision();

	CClientMask CosmeticMask(EItemType ItemType);
	CClientMask TeamMask();

	bool SnapCosmeticPickupPos(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Pos, int Type, int SubType, int Rotation = 0, int Alpha = -1, int Flags = 0);
	bool SnapCosmeticLaserPos(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha = -1, int Flags = 0);
	
	bool SnapCosmeticPickup(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Offset, int Type, int SubType, int Rotation = 0, int Alpha = -1, int Flags = 1);
	bool SnapCosmeticLaser(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha = -1, int Flags = 1);
	bool SnapCosmeticProjectile(int SnappingClient, int SnapId, int Owner, const vec2 &Offset, const vec2 &Target, int StartTick, int Type, int Alpha = -1, int Flags = 1);

	void Reset() override {}
	void Tick() override {}
	void TickDeferred() override {}
	void TickPaused() override {}
	void Snap(int SnappingClient) override {}
};

int OffsetToHeight(int TickOffset);

#endif // GAME_SERVER_FOXNET_ENTITIES_FOXNET_ENTITY_H
