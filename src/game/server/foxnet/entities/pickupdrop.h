// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_PICKUPDROP_H
#define GAME_SERVER_FOXNET_ENTITIES_PICKUPDROP_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/server/entity.h>
#include <game/server/gameworld.h>

class CPickupDrop : public CEntity
{
	int m_StartTick;

	int m_Lifetime; // In ticks
	int m_PickupDelay; // In ticks
	int m_Type;

	std::optional<int> m_aIds[2]; // Extra Ids

	int m_Team;

	vec2 m_GroundElasticity;
	vec2 m_PrevPos;
	vec2 m_Vel;

	static bool IsSwitchActiveCb(unsigned char Number, void *pUser);
	bool IsGrounded();
	void HandleSkippableTiles(int Index);
	void HandleTiles(int Index);
	int m_TeleCheckpoint;
	int m_TileIndex;
	int m_TileFIndex;
	int m_TuneZone;
	int m_MoveRestrictions;

	bool CollectItem();
	bool CheckArmor();
	bool CanBeSeenBy(int ClientId, int Asker = -1);

	int m_LastOwner;

	bool SnapPickupDropPickup(int SnappingClient, int SnapId, int OldFlags, const vec2 &Pos, int Type, int SubType, int Rotation, int Alpha, int Flags);
	bool SnapPickupDropLaser(int SnappingClient, int SnapId, const vec2 &From, const vec2 &To, int Type, int Alpha, int Flags);

public:
	CClientMask PickupMask(int Asker);

	int Team() const { return m_Team; }
	int TeleCheckpoint() const { return m_TeleCheckpoint; }
	int MoveRestrictions() const { return m_MoveRestrictions; }

	bool m_InsideFreeze;

	void TakeDamage(vec2 Force);

	void SetRawVelocity(vec2 Vel) override { m_Vel = Vel; }
	const vec2 &GetVelocity() const override { return m_Vel; }
	void ForceSetPos(vec2 Pos) override;
	int TuneZone() const override { return m_TuneZone; }

	CPickupDrop(CGameWorld *pGameWorld, int MultiMapIndex, int LastOwner, vec2 Pos, int Team, int TeleCheckpoint, vec2 Dir, int Lifetime /*Seconds*/, int Type);

	void Reset(bool PickedUp);
	void Reset() override { Reset(false); }
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_PICKUPDROP_H
