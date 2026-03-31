#ifndef GAME_SERVER_FOXNET_COMPONENTS_SPAWNCANDIDATES_H
#define GAME_SERVER_FOXNET_COMPONENTS_SPAWNCANDIDATES_H

#include <base/lock.h>
#include <base/vmath.h>

#include <game/server/foxnet/component.h>

#include <cstdint>
#include <map>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

class CSpawnCandidates : public CServerComponent
{
	mutable CLock m_CacheLock;
	std::map<const CMultiMaps *, std::vector<vec2>> m_CachedCandidates GUARDED_BY(m_CacheLock);
	std::unordered_map<const CMultiMaps *, std::thread> m_RebuildThreads GUARDED_BY(m_CacheLock);
	std::unordered_map<const CMultiMaps *, uint64_t> m_RebuildGenerations GUARDED_BY(m_CacheLock);

	void JoinRebuildThread(const CMultiMaps *pMultiMap) REQUIRES(!m_CacheLock);
	void RebuildAsync(size_t MapIdx) REQUIRES(!m_CacheLock);
	void StoreRebuildResult(const CMultiMaps *pMultiMap, uint64_t Generation, std::vector<vec2> &&vSpawnCandidates) REQUIRES(!m_CacheLock);

public:
	~CSpawnCandidates() override REQUIRES(!m_CacheLock);

	void OnMapLoad(size_t MapIdx) override REQUIRES(!m_CacheLock);
	void OnMapUnload(size_t MapIdx) override REQUIRES(!m_CacheLock);
	void OnShutdown(void *pPersistentData) override REQUIRES(!m_CacheLock);

	void Rebuild(size_t MapIdx) REQUIRES(!m_CacheLock);
	bool TryPickCachedCandidate(size_t MapIdx, vec2 &Out) const REQUIRES(!m_CacheLock);

	size_t SpawnCandidateCount(size_t MapIdx) const REQUIRES(!m_CacheLock);

	std::optional<vec2> GetRandomAccessiblePos() REQUIRES(!m_CacheLock);
	void OnTick() override REQUIRES(!m_CacheLock);
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_SPAWNCANDIDATES_H