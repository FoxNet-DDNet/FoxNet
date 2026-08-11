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

	bool CanSnapEntityMask(int SnappingClient, CClientMask Mask, CPlayer **ppSnapPlayer = nullptr);
	bool CanSnapEntityNoChar(int SnappingClient, CPlayer **ppSnapPlayer = nullptr);
	bool CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer = nullptr);

	CPlayer *GetPlayer();
	CCharacter *GetCharacter();
	// Gets the actual collision of the map thats loaded
	CCollision *GetCollision();

	CClientMask CosmeticMask(EItemType ItemType);
	CClientMask TeamMask();

	/*
	 * Snapped objects carry their owner as a client id the receiving client looks straight up, to anchor
	 * a cosmetic to that tee and to pick its alpha. Since 128 player support that id lives in the snapping
	 * client's own id space, which is no longer ours.
	 *
	 * Returns false if that client cannot see the owner at all. An object drawn at an absolute position can
	 * still be sent as ownerless, but anything anchored to the owner must not be snapped: it would hang off
	 * whoever happens to hold the slot instead.
	 */
	bool TranslateOwner(int SnappingClient, int *pOwner);

	void SnapCosmeticPickupPos(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Pos, int Type, int SubType, int Rotation = 0, int Alpha = -1, int Flags = 0);
	void SnapCosmeticLaserPos(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha = -1, int Flags = 0);
	void SnapCosmeticProjectilePos(int SnappingClient, int SnapId, int Type, vec2 Pos, vec2 Dir = vec2(0, 0));

	void SnapCosmeticPickup(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Offset, int Type, int SubType, int Rotation = 0, int Alpha = -1, int Flags = 1);
	void SnapCosmeticLaser(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha = -1, int Flags = 1);
	void SnapCosmeticProjectile(int SnappingClient, int SnapId, int Owner, const vec2 &Offset, const vec2 &Target, int StartTick, int Type, int Alpha = -1, int Flags = 1);

	void Reset() override {}
	void Tick() override {}
	void TickDeferred() override {}
	void TickPaused() override {}
	void Snap(int SnappingClient) override {}
};

int OffsetToHeight(int TickOffset);

#endif // GAME_SERVER_FOXNET_ENTITIES_FOXNET_ENTITY_H
