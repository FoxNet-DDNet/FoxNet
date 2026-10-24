#ifndef ENGINE_SERVER_RUN_DEMO_RECORDER_H
#define ENGINE_SERVER_RUN_DEMO_RECORDER_H

#include <base/hash.h>
#include <base/sphore.h>

#include <engine/shared/protocol.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CDemoRecorder;
class CSnapshotDelta;
class IHttp;
class IStorage;

// The game thread only copies events into a bounded queue. Demo encoding, disk
// access, part rollover, and Discord requests run on separate threads.
class CRunDemoRecorder
{
	static constexpr size_t MAX_QUEUED_BYTES = 64 * 1024 * 1024;
	static constexpr size_t MAX_QUEUED_EVENTS = 8192;
	static constexpr int64_t PART_LIMIT = 19'000'000;
	static constexpr int MAX_PARTS = 10;
	static constexpr int64_t DISCORD_FILE_LIMIT = 20'000'000;
	// Discord caps a message request at 25 MiB; reserve 1 MiB for multipart framing.
	static constexpr int64_t DISCORD_REQUEST_FILE_BUDGET = 24 * 1024 * 1024;

	enum class EEventType
	{
		START,
		SNAPSHOT,
		MESSAGE,
		FINISH,
		ABORT,
	};

	struct CEvent
	{
		EEventType m_Type;
		int m_ClientId;
		uint64_t m_Generation;
		int m_Tick = 0;
		float m_Time = 0.0f;
		unsigned m_MapCrc = 0;
		SHA256_DIGEST m_MapSha256{};
		std::string m_Map;
		std::string m_NetVersion;
		std::string m_Player;
		std::string m_AccountUsername;
		std::shared_ptr<std::atomic<int>> m_pRank;
		bool m_RankUnchanged = false;
		std::vector<unsigned char> m_Data;
	};

	struct CUpload
	{
		std::string m_Base;
		std::shared_ptr<std::atomic<int>> m_pRank;
	};

	struct CRun
	{
		uint64_t m_Generation = 0;
		std::string m_Base;
		std::string m_Map;
		std::string m_NetVersion;
		SHA256_DIGEST m_MapSha256{};
		unsigned m_MapCrc = 0;
		std::unique_ptr<CDemoRecorder> m_pRecorder;
		std::vector<std::string> m_vParts;
		bool m_Overflow = false;
	};

	IStorage *m_pStorage;
	IHttp *m_pHttp;
	CSnapshotDelta *m_pSnapshotDelta;
	std::string m_Folder;
	std::string m_WebhookUrl;
	int m_Port;
	uint64_t m_NextId = 0;
	std::array<uint64_t, MAX_CLIENTS> m_aGeneration{};
	std::array<bool, MAX_CLIENTS> m_aActive{};
	std::array<std::atomic<uint64_t>, MAX_CLIENTS> m_aCancelled{};
	std::array<std::atomic<bool>, MAX_CLIENTS> m_aOverflow{};
	std::array<CRun, MAX_CLIENTS> m_aRuns;

	// One game-thread producer and one writer-thread consumer. No lock or wait
	// is taken by the game thread, including on overflow.
	std::array<CEvent, MAX_QUEUED_EVENTS> m_aQueue;
	std::atomic<size_t> m_ReadIndex{0};
	std::atomic<size_t> m_WriteIndex{0};
	std::atomic<size_t> m_QueuedBytes{0};
	CSemaphore m_Events;
	std::atomic<bool> m_Stopping{false};
	std::atomic<bool> m_CancelWake{false};
	std::thread m_Writer;

	std::mutex m_UploadMutex;
	std::condition_variable m_UploadCondition;
	std::deque<CUpload> m_UploadQueue;
	bool m_UploadStopping = false;
	std::thread m_Uploader;

	bool TryQueue(CEvent &&Event);
	void Cancel(int ClientId);
	void WriterLoop();
	void UploadLoop();
	void HandleEvent(const CEvent &Event);
	void CloseRun(CRun &Run, bool KeepFiles);
	bool StartPart(CRun &Run);
	void CheckRollover(int ClientId, CRun &Run);
	void QueueUpload(CUpload Upload);
	bool Upload(const CUpload &Upload);

public:
	CRunDemoRecorder(IStorage *pStorage, IHttp *pHttp, CSnapshotDelta *pSnapshotDelta, const char *pFolder, const char *pWebhookUrl, int Port);
	~CRunDemoRecorder();
	CRunDemoRecorder(const CRunDemoRecorder &) = delete;
	CRunDemoRecorder &operator=(const CRunDemoRecorder &) = delete;

	void Start(int ClientId, const char *pNetVersion, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc);
	void Snapshot(int ClientId, int Tick, const void *pData, int Size);
	void Message(int ClientId, const void *pData, int Size);
	void Finish(int ClientId, float Time, const char *pPlayer, std::shared_ptr<std::atomic<int>> pRank, bool RankUnchanged, const char *pAccountUsername);
	void Abort(int ClientId);
	bool IsRecording(int ClientId);
	void AbortAll();
};

#endif
