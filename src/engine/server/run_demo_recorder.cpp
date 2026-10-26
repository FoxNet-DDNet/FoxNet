#include "run_demo_recorder.h"

#include "webhook_format.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/http.h>
#include <engine/shared/demo.h>
#include <engine/storage.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
	std::string JsonEscape(const std::string &Value)
	{
		std::string Result;
		for(unsigned char Ch : Value)
		{
			switch(Ch)
			{
			case '\\': Result += "\\\\"; break;
			case '"': Result += "\\\""; break;
			case '\n': Result += "\\n"; break;
			case '\r': Result += "\\r"; break;
			case '\t': Result += "\\t"; break;
			default:
				if(Ch >= 0x20)
					Result += static_cast<char>(Ch);
			}
		}
		return Result;
	}

	std::string SafeDiscordName(const char *pName)
	{
		std::string Clean;
		for(const unsigned char Ch : std::string(pName ? pName : ""))
			Clean += Ch < 0x20 || Ch == 0x7f ? ' ' : static_cast<char>(Ch);
		return WebhookFormat::Name(Clean.c_str());
	}

	std::string FilenameComponent(const char *pText, const char *pFallback)
	{
		std::string Result;
		for(const unsigned char Ch : std::string(pText ? pText : ""))
		{
			if(Result.size() >= 48)
				break;
			if((Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9'))
				Result += static_cast<char>(Ch);
			else if(!Result.empty() && Result.back() != '-')
				Result += '-';
		}
		while(!Result.empty() && Result.back() == '-')
			Result.pop_back();
		return Result.empty() ? pFallback : Result;
	}

	std::string ShortRunId(const std::string &Base)
	{
		uint64_t Hash = 14695981039346656037ULL;
		for(const unsigned char Ch : Base)
		{
			Hash ^= Ch;
			Hash *= 1099511628211ULL;
		}
		std::ostringstream Result;
		Result << std::hex << std::setfill('0') << std::setw(12) << (Hash & 0xffffffffffffULL);
		return Result.str();
	}
} // namespace

CRunDemoRecorder::CRunDemoRecorder(IStorage *pStorage, IHttp *pHttp, CSnapshotDelta *pSnapshotDelta, const char *pFolder, const char *pWebhookUrl, int Port, const char *pServerInstance) :
	m_pStorage(pStorage),
	m_pHttp(pHttp),
	m_pSnapshotDelta(pSnapshotDelta),
	m_Folder(pFolder),
	m_WebhookUrl(pWebhookUrl),
	m_Port(Port),
	m_ServerInstance(pServerInstance ? pServerInstance : "")
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_aCancelled[i].store(0);
		m_aOverflow[i].store(false);
	}
	m_Writer = std::thread(&CRunDemoRecorder::WriterLoop, this);
	m_Uploader = std::thread(&CRunDemoRecorder::UploadLoop, this);
}

CRunDemoRecorder::~CRunDemoRecorder()
{
	AbortAll();
	m_Stopping.store(true, std::memory_order_release);
	m_Events.Signal();
	if(m_Writer.joinable())
		m_Writer.join();
	{
		std::lock_guard Lock(m_UploadMutex);
		m_UploadStopping = true;
	}
	m_UploadCondition.notify_one();
	if(m_Uploader.joinable())
		m_Uploader.join();
}

bool CRunDemoRecorder::TryQueue(CEvent &&Event)
{
	const size_t Bytes = Event.m_Data.size() + sizeof(CEvent);
	const size_t Write = m_WriteIndex.load(std::memory_order_relaxed);
	const size_t Read = m_ReadIndex.load(std::memory_order_acquire);
	if(m_Stopping.load(std::memory_order_acquire) || Write - Read >= MAX_QUEUED_EVENTS || m_QueuedBytes.load(std::memory_order_acquire) + Bytes > MAX_QUEUED_BYTES)
		return false;
	m_aQueue[Write % MAX_QUEUED_EVENTS] = std::move(Event);
	m_QueuedBytes.fetch_add(Bytes, std::memory_order_release);
	m_WriteIndex.store(Write + 1, std::memory_order_release);
	m_Events.Signal();
	return true;
}

void CRunDemoRecorder::Cancel(int ClientId)
{
	m_aActive[ClientId] = false;
	m_aCancelled[ClientId].store(m_aGeneration[ClientId], std::memory_order_release);
	if(!m_CancelWake.exchange(true, std::memory_order_acq_rel))
		m_Events.Signal();
}

void CRunDemoRecorder::Start(int ClientId, const char *pNetVersion, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc)
{
	if(m_aActive[ClientId])
		Abort(ClientId);
	const uint64_t Generation = ++m_aGeneration[ClientId];
	m_aOverflow[ClientId].store(false);
	CEvent Event{EEventType::START, ClientId, Generation};
	Event.m_Map = pMap;
	Event.m_NetVersion = pNetVersion;
	Event.m_MapSha256 = MapSha256;
	Event.m_MapCrc = MapCrc;
	m_aActive[ClientId] = TryQueue(std::move(Event));
	if(!m_aActive[ClientId])
		Cancel(ClientId);
}

bool CRunDemoRecorder::IsRecording(int ClientId)
{
	if(m_aOverflow[ClientId].load(std::memory_order_acquire))
	{
		m_aActive[ClientId] = false;
		return false;
	}
	return m_aActive[ClientId];
}

void CRunDemoRecorder::Snapshot(int ClientId, int Tick, const void *pData, int Size)
{
	if(!IsRecording(ClientId))
		return;
	CEvent Event{EEventType::SNAPSHOT, ClientId, m_aGeneration[ClientId]};
	Event.m_Tick = Tick;
	Event.m_Data.assign(static_cast<const unsigned char *>(pData), static_cast<const unsigned char *>(pData) + Size);
	if(!TryQueue(std::move(Event)))
		Cancel(ClientId);
}

void CRunDemoRecorder::Message(int ClientId, const void *pData, int Size)
{
	if(!IsRecording(ClientId))
		return;
	CEvent Event{EEventType::MESSAGE, ClientId, m_aGeneration[ClientId]};
	Event.m_Data.assign(static_cast<const unsigned char *>(pData), static_cast<const unsigned char *>(pData) + Size);
	if(!TryQueue(std::move(Event)))
		Cancel(ClientId);
}

void CRunDemoRecorder::Finish(int ClientId, float Time, const char *pPlayer, std::shared_ptr<std::atomic<int>> pRank, bool RankUnchanged, const char *pAccountUsername)
{
	if(!IsRecording(ClientId))
		return;
	CEvent Event{EEventType::FINISH, ClientId, m_aGeneration[ClientId]};
	Event.m_Time = Time;
	Event.m_Player = pPlayer;
	Event.m_AccountUsername = pAccountUsername ? pAccountUsername : "";
	Event.m_pRank = std::move(pRank);
	Event.m_RankUnchanged = RankUnchanged;
	if(!TryQueue(std::move(Event)))
		Cancel(ClientId);
	else
		m_aActive[ClientId] = false;
}

void CRunDemoRecorder::Abort(int ClientId)
{
	if(!m_aActive[ClientId])
		return;
	CEvent Event{EEventType::ABORT, ClientId, m_aGeneration[ClientId]};
	if(!TryQueue(std::move(Event)))
		Cancel(ClientId);
	else
		m_aActive[ClientId] = false;
}

void CRunDemoRecorder::AbortAll()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
		Abort(i);
}

void CRunDemoRecorder::CloseRun(CRun &Run, bool KeepFiles)
{
	if(Run.m_pRecorder && Run.m_pRecorder->IsRecording())
	{
		Run.m_pRecorder->Stop(KeepFiles ? IDemoRecorder::EStopMode::KEEP_FILE : IDemoRecorder::EStopMode::REMOVE_FILE);
	}
	if(!KeepFiles)
	{
		for(const std::string &Part : Run.m_vParts)
		{
			std::error_code Error;
			fs::remove(fs::path(m_Folder) / Part, Error);
		}
	}
	Run.m_pRecorder.reset();
	Run.m_vParts.clear();
	Run.m_Generation = 0;
}

bool CRunDemoRecorder::StartPart(CRun &Run)
{
	const int Part = static_cast<int>(Run.m_vParts.size()) + 1;
	if(Part > MAX_PARTS)
		return false;
	char aPart[8];
	str_format(aPart, sizeof(aPart), "_p%02d.tmp.demo", Part);
	const std::string Filename = Run.m_Base + aPart;
	Run.m_pRecorder = std::make_unique<CDemoRecorder>(m_pSnapshotDelta, true);
	if(Run.m_pRecorder->Start(m_pStorage, (std::string("demos/runs/") + Filename).c_str(), Run.m_NetVersion.c_str(), Run.m_Map.c_str(), Run.m_MapSha256, Run.m_MapCrc, "server", 0, nullptr, nullptr, nullptr, nullptr) != 0)
	{
		Run.m_pRecorder.reset();
		return false;
	}
	Run.m_vParts.push_back(Filename);
	return true;
}

void CRunDemoRecorder::CheckRollover(int ClientId, CRun &Run)
{
	if(!Run.m_pRecorder || Run.m_pRecorder->FileSize() < PART_LIMIT)
		return;
	Run.m_pRecorder->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
	if(!StartPart(Run))
	{
		Run.m_Overflow = true;
		m_aOverflow[ClientId].store(true, std::memory_order_release);
		log_error("run_demo", "run exceeded ten demo parts; kept partial files under %s", Run.m_Base.c_str());
	}
}

void CRunDemoRecorder::HandleEvent(const CEvent &Event)
{
	CRun &Run = m_aRuns[Event.m_ClientId];
	if(Event.m_Generation <= m_aCancelled[Event.m_ClientId].load(std::memory_order_acquire))
	{
		if(Run.m_Generation == Event.m_Generation)
		{
			log_error("run_demo", "recording queue overflow; incomplete run retained under %s", Run.m_Base.c_str());
			CloseRun(Run, true);
		}
		return;
	}
	if(Event.m_Type == EEventType::START)
	{
		if(Run.m_Generation)
			CloseRun(Run, false);
		Run.m_Generation = Event.m_Generation;
		Run.m_Map = Event.m_Map;
		Run.m_NetVersion = Event.m_NetVersion;
		Run.m_MapSha256 = Event.m_MapSha256;
		Run.m_MapCrc = Event.m_MapCrc;
		Run.m_Overflow = false;
		Run.m_Base = std::to_string(time_timestamp()) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "_" + std::to_string(m_Port) + "_" + std::to_string(Event.m_ClientId) + "_" + std::to_string(++m_NextId);
		if(!StartPart(Run))
		{
			m_aOverflow[Event.m_ClientId].store(true, std::memory_order_release);
			log_error("run_demo", "could not start recording run %s", Run.m_Base.c_str());
		}
		return;
	}
	if(Run.m_Generation != Event.m_Generation)
		return;
	if(Event.m_Type == EEventType::ABORT)
	{
		CloseRun(Run, false);
		return;
	}
	if(Run.m_Overflow || !Run.m_pRecorder)
		return;
	if(Event.m_Type == EEventType::SNAPSHOT)
		Run.m_pRecorder->RecordSnapshot(Event.m_Tick, Event.m_Data.data(), static_cast<int>(Event.m_Data.size()));
	else if(Event.m_Type == EEventType::MESSAGE)
		Run.m_pRecorder->RecordMessage(Event.m_Data.data(), static_cast<int>(Event.m_Data.size()));
	else if(Event.m_Type == EEventType::FINISH)
	{
		Run.m_pRecorder->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
		char aWhen[64];
		str_timestamp_format(aWhen, sizeof(aWhen), TimestampFormat::SPACE);
		std::string When = aWhen;
		for(char &Ch : When)
			if(Ch == ' ')
				Ch = '_';
			else if(Ch == ':')
				Ch = '-';
		const long long Centiseconds = std::max(0LL, std::llround(Event.m_Time * 100.0));
		const long long Hours = Centiseconds / 360000;
		const long long Minutes = Centiseconds / 6000 % 60;
		const long long Seconds = Centiseconds / 100 % 60;
		const long long Hundredths = Centiseconds % 100;
		char aDisplayTime[32];
		char aFilenameTime[32];
		if(Hours)
		{
			str_format(aDisplayTime, sizeof(aDisplayTime), "%lld:%02lld:%02lld.%02lld", Hours, Minutes, Seconds, Hundredths);
			str_format(aFilenameTime, sizeof(aFilenameTime), "%lldh%02lldm%02lld.%02llds", Hours, Minutes, Seconds, Hundredths);
		}
		else
		{
			str_format(aDisplayTime, sizeof(aDisplayTime), "%lld:%02lld.%02lld", Minutes, Seconds, Hundredths);
			str_format(aFilenameTime, sizeof(aFilenameTime), "%02lldm%02lld.%02llds", Minutes, Seconds, Hundredths);
		}
		const size_t Count = Run.m_vParts.size();
		const std::string NameBase = When + "_" + FilenameComponent(Run.m_Map.c_str(), "map") + "_" + FilenameComponent(Event.m_Player.c_str(), "player") + "_" + aFilenameTime + "_" + ShortRunId(Run.m_Base);
		std::string FinalBase = NameBase;
		for(unsigned Collision = 1;; ++Collision)
		{
			std::error_code ManifestError;
			bool Exists = fs::exists(fs::path(m_Folder) / (FinalBase + ".txt"), ManifestError);
			for(size_t i = 0; i < Count; ++i)
			{
				char aPart[24];
				str_format(aPart, sizeof(aPart), "_part%02" PRIzu "-of%02" PRIzu, i + 1, Count);
				const fs::path PartBase = fs::path(m_Folder) / (FinalBase + aPart);
				std::error_code Error;
				if(fs::exists(PartBase.string() + ".demo", Error) || fs::exists(PartBase.string() + ".txt", Error))
					Exists = true;
			}
			if(!Exists)
				break;
			FinalBase = NameBase + "_" + std::to_string(Collision);
		}
		bool Finalized = true;
		for(size_t i = 0; i < Count; ++i)
		{
			const fs::path TempPath = fs::path(m_Folder) / Run.m_vParts[i];
			char aPart[24];
			str_format(aPart, sizeof(aPart), "_part%02" PRIzu "-of%02" PRIzu, i + 1, Count);
			const std::string ReadyName = FinalBase + aPart;
			const fs::path DemoPath = fs::path(m_Folder) / (ReadyName + ".demo");
			std::error_code Error;
			fs::rename(TempPath, DemoPath, Error);
			if(Error)
			{
				log_error("run_demo", "could not finalize %s: %s", TempPath.string().c_str(), Error.message().c_str());
				Finalized = false;
			}
		}
		if(Finalized)
		{
			const fs::path MetaPath = fs::path(m_Folder) / (FinalBase + ".txt");
			const fs::path TempMetaPath = fs::path(m_Folder) / (FinalBase + ".txt.tmp");
			bool MetaWritten = false;
			{
				std::ofstream Meta(TempMetaPath, std::ios::binary);
				Meta << "FOXNET_RUN_DEMO_V2\n"
				     << Count << '\n'
				     << Event.m_RankUnchanged << '\n';
				Meta << "Player: " << SafeDiscordName(Event.m_Player.c_str()) << " (" << Event.m_ClientId;
				if(!Event.m_AccountUsername.empty())
					Meta << ", acc: " << SafeDiscordName(Event.m_AccountUsername.c_str());
				Meta << ")\nMap: " << SafeDiscordName(Run.m_Map.c_str()) << "\nTime: " << aDisplayTime << "\nRank: ";
				Meta.flush();
				MetaWritten = Meta.good();
			}
			std::error_code Error;
			if(MetaWritten)
				fs::rename(TempMetaPath, MetaPath, Error);
			if(MetaWritten && !Error)
				QueueUpload({FinalBase, Event.m_pRank});
			else
				log_error("run_demo", "could not save metadata for %s: %s", FinalBase.c_str(), MetaWritten ? Error.message().c_str() : "write failed");
		}
		Run.m_pRecorder.reset();
		Run.m_vParts.clear();
		Run.m_Generation = 0;
		return;
	}
	CheckRollover(Event.m_ClientId, Run);
}

void CRunDemoRecorder::WriterLoop()
{
	std::error_code Error;
	fs::create_directories(m_Folder, Error);
	if(Error)
		log_error("run_demo", "could not create demo directory: %s", Error.message().c_str());
	while(true)
	{
		m_Events.Wait();
		if(m_CancelWake.exchange(false, std::memory_order_acq_rel))
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
				if(m_aRuns[i].m_Generation && m_aRuns[i].m_Generation <= m_aCancelled[i].load(std::memory_order_acquire))
				{
					log_error("run_demo", "recording queue overflow; incomplete run retained under %s", m_aRuns[i].m_Base.c_str());
					CloseRun(m_aRuns[i], true);
				}
		}
		const size_t Read = m_ReadIndex.load(std::memory_order_relaxed);
		if(Read == m_WriteIndex.load(std::memory_order_acquire))
		{
			if(m_Stopping.load(std::memory_order_acquire))
				break;
			continue;
		}
		CEvent Event = std::move(m_aQueue[Read % MAX_QUEUED_EVENTS]);
		m_QueuedBytes.fetch_sub(Event.m_Data.size() + sizeof(CEvent), std::memory_order_release);
		m_ReadIndex.store(Read + 1, std::memory_order_release);
		HandleEvent(Event);
	}
	for(CRun &Run : m_aRuns)
		if(Run.m_Generation)
			CloseRun(Run, true);
}

void CRunDemoRecorder::QueueUpload(CUpload Upload)
{
	{
		std::lock_guard Lock(m_UploadMutex);
		m_UploadQueue.push_back(std::move(Upload));
	}
	m_UploadCondition.notify_one();
}

bool CRunDemoRecorder::Upload(const CUpload &Upload)
{
	const fs::path MetaPath = fs::path(m_Folder) / (Upload.m_Base + ".txt");
	std::error_code Error;
	if(!fs::exists(MetaPath, Error))
		return true;
	std::ifstream Meta(MetaPath, std::ios::binary);
	if(!Meta)
		return false;
	std::string Content((std::istreambuf_iterator<char>(Meta)), std::istreambuf_iterator<char>());
	int PartCount = 1;
	bool RankUnchanged = false;
	bool Grouped = false;
	std::istringstream Parsed(Content);
	std::string Magic;
	std::getline(Parsed, Magic);
	if(Magic == "FOXNET_RUN_DEMO_V2")
	{
		Grouped = true;
		if(!(Parsed >> PartCount >> RankUnchanged) || PartCount < 1 || PartCount > MAX_PARTS)
		{
			log_error("run_demo", "invalid run metadata: %s", MetaPath.string().c_str());
			return true;
		}
		Parsed.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		Content.assign(std::istreambuf_iterator<char>(Parsed), std::istreambuf_iterator<char>());
		if(Upload.m_pRank)
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while(Upload.m_pRank->load(std::memory_order_acquire) < 0 && std::chrono::steady_clock::now() < Deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		const int Rank = Upload.m_pRank ? Upload.m_pRank->load(std::memory_order_acquire) : 0;
		Content += Rank > 0 ? "#" + std::to_string(Rank) + (RankUnchanged ? " (unchanged)" : "") : "unavailable";
	}
	while(true)
	{
		std::vector<fs::path> vFiles;
		std::vector<uintmax_t> vSizes;
		std::vector<int> vPartNumbers;
		uintmax_t TotalSize = 0;
		bool MoreParts = false;
		for(int i = 1; i <= PartCount; ++i)
		{
			char aPart[24];
			str_format(aPart, sizeof(aPart), "_part%02d-of%02d", i, PartCount);
			const fs::path DemoPath = fs::path(m_Folder) / (Upload.m_Base + (Grouped ? aPart : "") + ".demo");
			const bool Exists = fs::exists(DemoPath, Error);
			if(Error)
				return false;
			if(!Exists)
				continue;
			const uintmax_t Size = fs::file_size(DemoPath, Error);
			if(Error || Size > DISCORD_FILE_LIMIT)
			{
				log_error("run_demo", "demo exceeds Discord upload limit or cannot be read: %s", DemoPath.string().c_str());
				return true; // Retain the run for manual recovery.
			}
			if(TotalSize + Size > DISCORD_REQUEST_FILE_BUDGET)
			{
				MoreParts = true;
				break;
			}
			TotalSize += Size;
			vFiles.push_back(DemoPath);
			vSizes.push_back(Size);
			vPartNumbers.push_back(i);
		}
		if(vFiles.empty())
		{
			fs::remove(MetaPath, Error);
			return true;
		}
		std::string Message = Content;
		for(const char *pOldHeading : {"**Run finished**\n", "🏁 **Run finished**\n"})
		{
			const size_t HeadingLength = str_length(pOldHeading);
			if(Message.compare(0, HeadingLength, pOldHeading) == 0)
			{
				Message.erase(0, HeadingLength);
				break;
			}
		}
		for(const char *pLabel : {"Map", "Player", "Account", "Time", "Rank"})
		{
			const std::string OldLabel = std::string("**") + pLabel + ":**";
			for(size_t Pos = 0; (Pos = Message.find(OldLabel, Pos)) != std::string::npos; Pos += str_length(pLabel) + 1)
				Message.replace(Pos, OldLabel.size(), std::string(pLabel) + ":");
		}
		if(Grouped)
		{
			Message += "\nDemos: ";
			if(vFiles.size() == static_cast<size_t>(PartCount))
				Message += std::to_string(PartCount) + (PartCount == 1 ? " part" : " parts");
			else if(vFiles.size() == 1)
				Message += "Part " + std::to_string(vPartNumbers.front()) + " of " + std::to_string(PartCount);
			else
				Message += "Parts " + std::to_string(vPartNumbers.front()) + "-" + std::to_string(vPartNumbers.back()) + " of " + std::to_string(PartCount);
		}
		const std::string ServerTitle = "server: " + std::to_string(m_Port) + (m_ServerInstance.empty() ? "" : " | " + WebhookFormat::EscapeText(m_ServerInstance.c_str()));
		const std::string Payload = "{\"username\":\"FoxNet Runs\",\"embeds\":[{\"title\":\"" + JsonEscape(ServerTitle) +
			"\",\"description\":\"" + JsonEscape(Message) + "\",\"color\":5763719}],\"allowed_mentions\":{\"parse\":[]}}";
		const std::string Boundary = "FoxNetRunDemo" + ShortRunId(Upload.m_Base);
		std::string Body;
		Body.reserve(static_cast<size_t>(TotalSize) + 4096);
		Body += "--" + Boundary + "\r\nContent-Disposition: form-data; name=\"payload_json\"\r\nContent-Type: application/json\r\n\r\n";
		Body += Payload + "\r\n";
		for(size_t i = 0; i < vFiles.size(); ++i)
		{
			Body += "--" + Boundary + "\r\nContent-Disposition: form-data; name=\"files[" + std::to_string(i) + "]\"; filename=\"" + vFiles[i].filename().string() + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
			std::ifstream File(vFiles[i], std::ios::binary);
			if(!File)
				return false;
			const size_t Start = Body.size();
			Body.resize(Start + static_cast<size_t>(vSizes[i]));
			File.read(Body.data() + Start, static_cast<std::streamsize>(vSizes[i]));
			if(!File)
				return false;
			Body += "\r\n";
		}
		Body += "--" + Boundary + "--\r\n";
		const std::string Url = m_WebhookUrl + (m_WebhookUrl.find('?') == std::string::npos ? "?wait=true" : "&wait=true");
		std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(Url.c_str());
		pRequest->Post(reinterpret_cast<const unsigned char *>(Body.data()), Body.size());
		pRequest->HeaderString("Content-Type", ("multipart/form-data; boundary=" + Boundary).c_str());
		pRequest->Timeout(CTimeout{10000, 90000, 0, 0});
		pRequest->MaxResponseSize(65536);
		m_pHttp->Run(pRequest);
		pRequest->Wait();
		const EHttpState State = pRequest->State();
		const int Status = State == EHttpState::DONE ? pRequest->StatusCode() : 0;
		if(State != EHttpState::DONE || Status != 200)
		{
			log_error("run_demo", "upload failed for %s: state=%d http=%d", Upload.m_Base.c_str(), static_cast<int>(State), Status);
			return false;
		}
		for(const fs::path &Path : vFiles)
		{
			fs::remove(Path, Error);
			if(Error)
			{
				log_error("run_demo", "uploaded but could not remove %s: %s", Path.string().c_str(), Error.message().c_str());
				return true; // Retain the manifest for manual recovery.
			}
		}
		log_info("run_demo", "uploaded %s (%" PRIzu " part%s)", Upload.m_Base.c_str(), vFiles.size(), vFiles.size() == 1 ? "" : "s");
		if(!MoreParts)
		{
			fs::remove(MetaPath, Error);
			return true;
		}
	}
}

void CRunDemoRecorder::UploadLoop()
{
	std::error_code Error;
	fs::create_directories(m_Folder, Error);
	if(!Error)
	{
		for(fs::directory_iterator It(m_Folder, Error), End; !Error && It != End; It.increment(Error))
		{
			std::error_code EntryError;
			if(It->is_regular_file(EntryError) && It->path().extension() == ".txt")
				QueueUpload({It->path().stem().string(), nullptr});
		}
	}
	while(true)
	{
		CUpload Upload;
		{
			std::unique_lock Lock(m_UploadMutex);
			m_UploadCondition.wait(Lock, [this] { return m_UploadStopping || !m_UploadQueue.empty(); });
			if(m_UploadStopping)
				break;
			Upload = std::move(m_UploadQueue.front());
			m_UploadQueue.pop_front();
		}
		if(!this->Upload(Upload))
		{
			std::unique_lock Lock(m_UploadMutex);
			m_UploadQueue.push_back(std::move(Upload));
			m_UploadCondition.wait_for(Lock, std::chrono::seconds(30), [this] { return m_UploadStopping; });
		}
	}
}
