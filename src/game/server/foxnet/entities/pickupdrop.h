// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_PICKUPDROP_H
#define GAME_SERVER_FOXNET_ENTITIES_PICKUPDROP_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/server/entity.h>
#include <game/server/gameworld.h>
#include <game/teamscore.h>

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

	// A drop spends nearly all of its lifetime motionless on the floor. No client
	// predicts a drop and snaps carry integer positions, so once the truncated
	// position stops changing the remaining simulation is unobservable. Park the
	// drop and re-run a full tick occasionally to notice the world changing.
	static constexpr int SLEEP_AFTER_RESTING_TICKS = 10;
	static constexpr int SLEEP_RECHECK_TICKS = 50;

	bool m_Sleeping;
	int m_RestTicks;
	int m_SleepRecheck;

	void WakeUp()
	{
		m_Sleeping = false;
		m_RestTicks = 0;
	}

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

	// Whether a drop is visible depends only on (SnappingClient, MultiMapIdx, Team) --
	// never on the drop itself -- yet Snap evaluated it per drop, twice. CGameWorld
	// snaps every drop for one client before moving to the next, so memoising the
	// answer for the current client collapses that to one evaluation per team.
	struct SSnapVisibility
	{
		int m_Tick;
		int m_SnappingClient;
		signed char m_aVisible[NUM_DDRACE_TEAMS]; // -1 unknown, 0 hidden, 1 visible
		int m_aMultiMapIdx[NUM_DDRACE_TEAMS];
	};
	static SSnapVisibility ms_SnapVisibility;

	bool CanSnapTo(int SnappingClient);

	int m_LastOwner;

	void SnapPickupDropPickup(int SnappingClient, int SnapId, int OldFlags, const vec2 &Pos, int Type, int SubType, int Rotation, int Alpha, int Flags);
	void SnapPickupDropLaser(int SnappingClient, int SnapId, const vec2 &From, const vec2 &To, int Type, int Alpha, int Flags);
	void SnapPickupDropProjectile(int SnapId, int SnappingClient, int Type, vec2 Pos, vec2 Dir);

public:
	CClientMask PickupMask(int Asker);

	int Team() const { return m_Team; }
	int TeleCheckpoint() const { return m_TeleCheckpoint; }
	int MoveRestrictions() const { return m_MoveRestrictions; }

	bool m_InsideFreeze;

	void TakeDamage(vec2 Force);

	void SetRawVelocity(vec2 Vel) override
	{
		m_Vel = Vel;
		WakeUp();
	}
	const vec2 &GetVelocity() const override { return m_Vel; }
	void ForceSetPos(vec2 Pos) override;
	int TuneZone() const override { return m_TuneZone; }

	CPickupDrop(CGameWorld *pGameWorld, int MultiMapIndex, int LastOwner, vec2 Pos, int Team, int TeleCheckpoint, vec2 Dir, int Lifetime /*Seconds*/, int Type);

	void RemoveFromOwner();
	void Reset(bool PickedUp);
	void Reset() override { Reset(false); }
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_PICKUPDROP_H
