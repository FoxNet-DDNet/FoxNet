#ifndef GAME_SERVER_FOXNET_COMPONENTS_SPAWNCANDIDATES_H
#define GAME_SERVER_FOXNET_COMPONENTS_SPAWNCANDIDATES_H

#include <base/lock.h>
#include <base/vmath.h>

#include <game/server/foxnet/component.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class CSpawnCandidates : public CServerComponent
{
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
	~CSpawnCandidates() override;

	void OnMapLoad(size_t MapIdx) override;
	void OnMapUnload(size_t MapIdx) override;
	void OnShutdown(void *pPersistentData) override;

	void Rebuild(size_t MapIdx);
	bool TryPickCachedCandidate(size_t MapIdx, vec2 &Out) const;

	size_t SpawnCandidateCount(size_t MapIdx) const;

	std::optional<vec2> GetRandomAccessiblePos();
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_SPAWNCANDIDATES_H
