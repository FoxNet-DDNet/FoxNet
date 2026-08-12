#ifndef GAME_SERVER_FOXNET_COMPONENTS_POWERUP_H
#define GAME_SERVER_FOXNET_COMPONENTS_POWERUP_H

#include <base/lock.h>
#include <base/net.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/server/foxnet/component.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

static constexpr int NUM_LASERS = 5;

enum class EPowerUp
{
	INVALID = 0,
	XP,
	MONEY,
	BOOST,
	NUM_TYPES
};

class CPowerupData
{
public:
	EPowerUp m_Type = EPowerUp::INVALID;
	long m_Value = 0;
};

/*
 * Powerups and the spots they are allowed to appear in.
 *
 * A powerup never moves, never collides and never reacts to anything: it sits at a fixed position
 * and asks whether somebody is standing on it. That does not need an entity, so the component owns
 * them outright and snaps them itself, which also keeps them out of the world's tick and snap loops.
 */
class CPowerUps : public CServerComponent
{
	class CPowerUp
	{
	public:
		class CSnapData
		{
		public:
			std::optional<int> m_Id;
			vec2 m_To = vec2(0, 0);
			vec2 m_From = vec2(0, 0);
			int m_LaserType = 0;
		};

		class CClientData
		{
		public:
			bool m_Collected = false;
			bool m_WasLoggedIn = false;
			NETADDR m_Addr = NETADDR();
		};

		vec2 m_Pos = vec2(0, 0);
		size_t m_MultiMapIdx = 0;

		int m_StartTick = 0;
		int m_Lifetime = 0;
		bool m_Switch = false;
		int m_MaxCollections = 0;

		CPowerupData m_Data;

		// The pickup in the middle plus the laser square around it
		std::optional<int> m_PickupId;
		std::array<CSnapData, NUM_LASERS> m_aSnap;

		CClientData m_aClients[MAX_CLIENTS];
	};

	std::vector<CPowerUp> m_vPowerups;
	int64_t m_SpawnDelay = 0;

	void SetData(CPowerUp &Powerup);
	// The square never changes once the type is picked, so this only runs on spawn
	void SetVisual(CPowerUp &Powerup);
	void FreeIds(CPowerUp &Powerup);

	void TrySpawn();
	// Returns false once the powerup is used up and should be dropped
	bool TickPowerup(CPowerUp &Powerup);
	void HandleClient(CPowerUp &Powerup, int ClientId);
	void CollectPowerup(const CPowerUp &Powerup, CPlayer *pPlayer);
	void SnapPowerup(const CPowerUp &Powerup, int SnappingClient);

	struct SSharedState
	{
		mutable CLock m_CacheLock;
		std::map<const CMultiMaps *, std::vector<vec2>> m_CachedCandidates GUARDED_BY(m_CacheLock);
		std::unordered_map<const CMultiMaps *, uint64_t> m_RebuildGenerations GUARDED_BY(m_CacheLock);
		bool m_RebuildBusy GUARDED_BY(m_CacheLock) = false;
		bool m_RebuildDeferred GUARDED_BY(m_CacheLock) = false;
	};

	std::shared_ptr<SSharedState> m_pShared = std::make_shared<SSharedState>();

	void QueueRebuildSnapshot(size_t MapIdx);
	void RebuildAsync(size_t MapIdx);

public:
	~CPowerUps() override;

	void OnMapLoad(size_t MapIdx) override;
	void OnMapUnload(size_t MapIdx) override;
	void OnShutdown(void *pPersistentData) override;

	void Rebuild(size_t MapIdx);
	bool TryPickCachedCandidate(size_t MapIdx, vec2 &Out) const;

	size_t SpawnCandidateCount(size_t MapIdx) const;

	std::optional<vec2> GetRandomAccessiblePos();
	void OnTick() override;
	void OnClientEnter(int ClientId) override;
	void OnSnap(int ClientId, bool GlobalSnap, bool RecordingDemo) override;

	void ClearPowerups();
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_POWERUP_H
