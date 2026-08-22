
#include "entities/pickupdrop.h"
#include "fontconvert.h"
#include "persistent_data.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/map.h>
#include <engine/message.h>
#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex_msgs.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/component.h>
#include <game/server/foxnet/components/accounts/accounts.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/score.h>
#include <game/server/teams.h>
#include <game/teamscore.h>
#include <game/voting.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

void CMultiMaps::InitTuning(CGameContext *pGameContext, size_t MultiMapIndex)
{
	// Global tuning = m_aTuningList[0]
	// Tune zones = anything above index 0

	for(auto &Tune : m_aTuningList)
	{
		Tune = pGameContext->DDNetDefaultTuning();
	}

	// Reset Tuning
	if(g_Config.m_SvTuneReset)
	{
		pGameContext->ResetTuning(MultiMapIndex);
	}
	else
	{
		m_aTuningList[0] = pGameContext->DDNetDefaultTuning();
	}
}

void CMultiMaps::ApplySoloServerTuning()
{
	if(!g_Config.m_SvSoloServer)
		return;

	// Global tuning = m_aTuningList[0]
	for(auto &Tune : m_aTuningList)
	{
		Tune.Set("player_collision", 0);
		Tune.Set("player_hooking", 0);
	}
}

void CGameContext::FoxNetTick()
{
	for(size_t Idx = 0; Idx < m_vMultiMaps.size(); ++Idx)
	{
		if(g_Config.m_SvSetupDestroyer && Server()->Tick() % (Server()->TickSpeed() * 5) == 0)
		{
			const float Default = m_vMultiMaps[Idx]->m_HookFireSpeed;
			constexpr float Min = 0.10f;
			constexpr float Max = 0.30f;
			std::uniform_real_distribution<float> Range(Min, Max);
			std::uniform_int_distribution<int> Negative(0, 1);
			int IsNegative = Negative(Rng());
			float RandAdjust = Range(Rng());
			if(IsNegative)
				RandAdjust = -RandAdjust;

			GlobalTuning(Idx)->Set("hook_fire_speed", std::max(0.0f, Default + RandAdjust));
			SendTuningParams(-1);
		}
	}

	if(m_BoostData.m_Ticks > 0)
		m_BoostData.m_Ticks--;
	else
		m_BoostData.m_Boost = 0.0f;

	for(auto &pComponent : m_vpComponents)
		pComponent->OnTick();

	HandleEffects();

	if(g_Config.m_SvBanSyncing)
		BanSync();

	// Save all logged in accounts every 15 minutes
	if(Server()->Tick() % (Server()->TickSpeed() * 60 * 15) == 0)
		m_AccountManager.SaveAllAccounts();

	for(int ClientId = 0; ClientId < Server()->MaxClients(); ++ClientId)
	{
		CPlayer *pPlayer = m_apPlayers[ClientId];
		if(!pPlayer)
			continue;

		pPlayer->HandleTelekinesis();
	}
}

void CGameContext::OnFoxNetConsoleInit()
{
	m_vpComponents.insert(m_vpComponents.end(), {
							    &m_ZoneManager,
							    &m_PowerUps,
							    &m_Scripting,
							    &m_VoteMenu,
							    &m_AccountManager,
							    &m_Shop,
						    });

	for(auto &pComponent : m_vpComponents)
		pComponent->InitComponent(this);

	for(auto &pComponent : m_vpComponents)
		pComponent->OnConsoleInit();

	RegisterFoxNetCommands();
}

void CGameContext::LoadMapByName(const char *pMapName, EMapType Type)
{
	if(!g_Config.m_SvMultimap)
	{
		log_error("multimap", "Failed to load map '%s': multimap is disabled", pMapName);
		return;
	}

	for(size_t Idx = 1; Idx < m_vMultiMaps.size(); ++Idx)
	{
		if(str_comp(m_vMultiMaps[Idx]->m_pMap->BaseName(), pMapName) == 0)
		{
			log_error("multimap", "Failed to load map '%s': already loaded", pMapName);
			return;
		}
		else if(Type != EMapType::None && Type == m_vMultiMaps[Idx]->m_MapType)
		{
			log_error("multimap", "Failed to load map '%s': a map of type %" PRIzu " is already loaded", pMapName, (size_t)Type);
			return;
		}
	}

	std::unique_ptr<CMultiMaps> pNewMap = std::make_unique<CMultiMaps>();
	pNewMap->m_pMap = CreateMap();

	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);
	if(!str_valid_filename(fs_filename(aBuf)))
	{
		log_error("multimap", "The name '%s' cannot be used for maps because not all platforms support it", aBuf);
		return;
	}
	// Write the settings we generate for this map into it before it gets loaded, the clients are
	// sent this exact file so they end up with the same tune zones as the server
	char aRewritten[IO_MAX_PATH_LENGTH] = "";
	RewriteMapSettings(pMapName, aBuf, sizeof(aBuf), nullptr, aRewritten, sizeof(aRewritten));

	if(!pNewMap->m_pMap->Load(pMapName, Storage(), aBuf, IStorage::TYPE_ALL))
	{
		log_error("multimap", "Failed to load map '%s'", aBuf);
		if(aRewritten[0])
			Storage()->RemoveFile(aRewritten, IStorage::TYPE_SAVE);
		return;
	}
	// Keep the bytes we just loaded, then drop the file. Clients are served from this copy, so
	// nothing can replace the map underneath us between loading it and sending it.
	const bool Cached = CacheMapData(pNewMap.get(), pMapName, aBuf);
	if(aRewritten[0])
		Storage()->RemoveFile(aRewritten, IStorage::TYPE_SAVE);
	if(!Cached)
	{
		log_error("multimap", "Failed to keep map data for '%s', clients could not be sent it", aBuf);
		return;
	}
	log_info("multimap", "Map loaded: %s", aBuf);
	pNewMap->Init();
	pNewMap->m_MapType = Type;
	pNewMap->m_CreatedEntities = false;
	pNewMap->m_LoadedSwitchers = false;

	const size_t NewMapIndex = m_vMultiMaps.size();
	m_vMultiMaps.push_back(std::move(pNewMap));
	m_vMultiMaps[NewMapIndex]->InitTuning(this, NewMapIndex);

	LoadMapSettings(NewMapIndex);

	m_vMultiMaps[NewMapIndex]->ApplySoloServerTuning();

	for(auto &pComponent : m_vpComponents)
		pComponent->OnMapLoad(NewMapIndex);
}

void CGameContext::UnloadMapByName(const char *pMapName)
{
	auto It = std::find_if(m_vMultiMaps.begin(), m_vMultiMaps.end(), [pMapName](const auto &pMapOverride) {
		return pMapOverride && pMapOverride->m_pMap && str_comp(pMapOverride->m_pMap->BaseName(), pMapName) == 0;
	});
	if(It != m_vMultiMaps.end())
	{
		int Idx = std::distance(m_vMultiMaps.begin(), It);
		if(Idx == DefaultMapIndex)
		{
			log_error("multimap", "Failed to unload map '%s': cannot unload default map", pMapName);
			return;
		}
		m_pController->ClearSpawnPoints(Idx);
		m_World.DestroyEntitiesOfMap(Idx);

		for(CPlayer *pPlayer : m_apPlayers)
		{
			if(!pPlayer)
				continue;
			if(pPlayer->MultiMapIdx() != Idx)
				continue;
			pPlayer->SendToMap(DefaultMapIndex);
		}

		for(auto &pComponent : m_vpComponents)
			pComponent->OnMapUnload(Idx);

		log_info("multimap", "Map unloaded of type %d: %s", (int)(*It)->m_MapType, pMapName);
		(*It)->Unload();
		m_vMultiMaps.erase(It);
	}
	else
		log_error("multimap", "Failed to unload map '%s': not found", pMapName);
}

void CGameContext::ReloadMapByName(const char *pMapName)
{
	auto It = std::find_if(m_vMultiMaps.begin(), m_vMultiMaps.end(), [pMapName](const auto &pMapOverride) {
		return pMapOverride && pMapOverride->m_pMap && str_comp(pMapOverride->m_pMap->BaseName(), pMapName) == 0;
	});

	if(It == m_vMultiMaps.end())
	{
		log_error("multimap", "Failed to reload map '%s': not found", pMapName);
		return;
	}

	const int Idx = std::distance(m_vMultiMaps.begin(), It);
	if(Idx == DefaultMapIndex)
	{
		log_error("multimap", "Failed to reload map '%s': cannot reload default map", pMapName);
		return;
	}

	const EMapType Type = (*It)->m_MapType;

	std::unique_ptr<CMultiMaps> pReloadedMap = std::make_unique<CMultiMaps>();
	pReloadedMap->m_pMap = CreateMap();

	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);
	if(!str_valid_filename(fs_filename(aBuf)))
	{
		log_error("multimap", "The name '%s' cannot be used for maps because not all platforms support it", aBuf);
		return;
	}
	char aRewritten[IO_MAX_PATH_LENGTH] = "";
	RewriteMapSettings(pMapName, aBuf, sizeof(aBuf), nullptr, aRewritten, sizeof(aRewritten));

	if(!pReloadedMap->m_pMap->Load(pMapName, Storage(), aBuf, IStorage::TYPE_ALL))
	{
		log_error("multimap", "Failed to reload map '%s'", aBuf);
		if(aRewritten[0])
			Storage()->RemoveFile(aRewritten, IStorage::TYPE_SAVE);
		return;
	}
	// See the load path: the bytes are kept, the file is not
	const bool Cached = CacheMapData(pReloadedMap.get(), pMapName, aBuf);
	if(aRewritten[0])
		Storage()->RemoveFile(aRewritten, IStorage::TYPE_SAVE);
	if(!Cached)
	{
		log_error("multimap", "Failed to keep map data for '%s', clients could not be sent it", aBuf);
		return;
	}

	bool aWasOnMap[MAX_CLIENTS] = {false};

	for(int ClientId = 0; ClientId < Server()->MaxClients(); ++ClientId)
	{
		if(Server()->ClientSlotEmpty(ClientId))
			continue;
		CPlayer *pPlayer = m_apPlayers[ClientId];
		if(!pPlayer)
			continue;

		if(pPlayer->MultiMapIdx() == Idx)
		{
			aWasOnMap[ClientId] = true;
			pPlayer->SendToMap(DefaultMapIndex);
		}
	}

	m_pController->ClearSpawnPoints(Idx);
	m_World.DestroyEntitiesOfMap(Idx);

	for(auto &pComponent : m_vpComponents)
		pComponent->OnMapUnload(Idx);

	(*It)->Unload();

	pReloadedMap->Init();
	m_vMultiMaps[Idx] = std::move(pReloadedMap);
	m_vMultiMaps[Idx]->m_MapType = Type;
	m_vMultiMaps[Idx]->m_CreatedEntities = false;
	m_vMultiMaps[Idx]->m_LoadedSwitchers = false;
	m_vMultiMaps[Idx]->InitTuning(this, Idx);

	LoadMapSettings(Idx);

	m_vMultiMaps[Idx]->ApplySoloServerTuning();

	for(auto &pComponent : m_vpComponents)
		pComponent->OnMapLoad(Idx);

	for(int ClientId = 0; ClientId < Server()->MaxClients(); ++ClientId)
	{
		if(!aWasOnMap[ClientId])
			continue;

		CPlayer *pPlayer = m_apPlayers[ClientId];
		if(!pPlayer)
			continue;

		if(!pPlayer->SendToMap(Idx))
			log_error("multimap", "Failed to send client %d back to reloaded map '%s'", ClientId, pMapName);
	}

	log_info("multimap", "Map reloaded of type %d: %s", (int)Type, pMapName);
}

void CGameContext::UnloadMapsAll()
{
	for(size_t Idx = 1; Idx < m_vMultiMaps.size(); ++Idx)
	{
		log_info("multimap", "Map unloaded of type %d: %s", (int)m_vMultiMaps[Idx]->m_MapType, m_vMultiMaps[Idx]->m_pMap->BaseName());
		m_vMultiMaps[Idx]->Unload();
		m_pController->ClearSpawnPoints(Idx);
		m_World.DestroyEntitiesOfMap(Idx);
		for(CPlayer *pPlayer : m_apPlayers)
		{
			if(!pPlayer)
				continue;
			pPlayer->SendToMap(DefaultMapIndex);
		}

		for(auto &pComponent : m_vpComponents)
			pComponent->OnMapUnload(Idx);
	}
	m_vMultiMaps.clear();
}

void CGameContext::FoxNetInit()
{
	for(auto &pComponent : m_vpComponents)
		pComponent->OnInit();
	m_PowerUps.ClearPowerups();
	m_BanSaveDelay = Server()->Tick() + Server()->TickSpeed() * (g_Config.m_SvBanSyncingDelay * 60);

	if(Score() && g_Config.m_SvAccounts)
		Score()->CacheMapInfo();

	if(!m_InitRandomMap)
	{
		if(g_Config.m_SvRandomMapVoteOnStart)
		{
			if(RandomMapVote())
				m_InitRandomMap = true;
		}
		else
			m_InitRandomMap = true;
	}
}

int CGameContext::RandGeometric(std::mt19937 &Rng, int Min, int Max, double P)
{
	if(Max < Min)
		std::swap(Min, Max);
	P = std::clamp(P, 1e-9, 1.0 - 1e-9);
	std::geometric_distribution<int> Geo(P);
	int Range = Max - Min;
	int k = Geo(Rng);
	if(k > Range)
		k = Range;
	return Min + k;
}

void CGameContext::HandleEffects()
{
	// Handle DamageInd effect
	for(auto It = m_vDamageIndEffects.begin(); It != m_vDamageIndEffects.end();)
	{
		if(It->m_Remaining > 0 && Server()->Tick() >= It->m_NextTick)
		{
			int Angles = It->m_vAngles.size() - It->m_Remaining;
			if(Angles < 0)
				Angles = 0;
			int Positions = It->m_vPos.size() - It->m_Remaining;
			if(Positions < 0)
				Positions = 0;

			CreateDamageInd(It->m_vPos.at(Positions), It->m_vAngles.at(Angles), 1, It->m_Mask);
			It->m_Remaining--;
			It->m_NextTick = Server()->Tick() + It->m_Delay;
		}
		if(It->m_Remaining <= 0)
			It = m_vDamageIndEffects.erase(It);
		else
			++It;
	}
}

void CGameContext::FoxNetSnap(int ClientId, bool GlobalSnap, bool RecordingDemo)
{
	for(auto &pComponent : m_vpComponents)
		pComponent->OnSnap(ClientId, GlobalSnap, RecordingDemo);
}

void CGameContext::BanSync()
{
	static int64_t s_ExecSaveDelay = Server()->Tick() + Server()->TickSpeed();
	if(m_BanSaveDelay < Server()->Tick())
	{
		static bool s_ExecBans = false;

		if(Storage()->FileExists("Bans.cfg", IStorage::TYPE_ALL))
		{
			if(!s_ExecBans)
			{
				Server()->SetQuietBan(true);
				Console()->ExecuteBansFile();
				s_ExecBans = true;
				s_ExecSaveDelay = Server()->Tick() + Server()->TickSpeed();
			}
		}
		else
		{
			// Info Message
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ban-sync", "Couldn't find \"Bans.cfg\", disabling component ");
			g_Config.m_SvBanSyncing = 0;
			if(g_Config.m_SvBanSyncing == 0)
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ban-sync", "fs_ban_syncing set to 0");
		}

		if(s_ExecSaveDelay < Server()->Tick() && s_ExecBans)
		{
			Console()->ExecuteLine("bans_save \"Bans.cfg\"", IConsole::CLIENT_ID_UNSPECIFIED);

			// Info Message
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ban-sync", "Saved Bans");

			s_ExecBans = false;
			m_BanSaveDelay = Server()->Tick() + Server()->TickSpeed() * (g_Config.m_SvBanSyncingDelay * 60);
		}
	}
	Server()->SetQuietBan(false);
}

void CGameContext::ClearVotes(int ClientId)
{
	if(ClientId == -1)
	{
		for(int i = 0; i < Server()->MaxClients(); i++)
		{
			if(m_apPlayers[i] && !Server()->ClientSlotEmpty(i))
				ClearVotes(i);
		}
		return;
	}

	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientId);
	m_VoteMenu.PrepareVoteOptions(ClientId);
}

static bool TryingToBeFunny(const char *pMsg)
{
	const char *pFunsies[] = {
		"ddnet.org",
		"tater", "tclient", "t client", "t-client", "tclient.app", // TClient
		"aiodob", "aidob", "a-client", "A Client", "A client", // AClient
		"eclient", "e client", "entity client", "e-client", "entityclient", // EClient
		"chiller", "cactus" // Chillerbot/Cactus
	}; // Other

	return std::any_of(std::begin(pFunsies), std::end(pFunsies), [pMsg](const char *pFun) {
		return str_find_nocase(pMsg, pFun);
	});
}

int CGameContext::GetMapIndexByType(EMapType MapType) const
{
	for(size_t i = 0; i < m_vMultiMaps.size(); i++)
	{
		if(m_vMultiMaps[i]->m_MapType == MapType)
			return i;
	}
	return -1;
}
int CGameContext::GetMapIndexByMapName(const char *pMapName) const
{
	for(size_t i = 0; i < m_vMultiMaps.size(); i++)
	{
		if(str_comp(m_vMultiMaps[i]->m_pMap->BaseName(), pMapName) == 0)
			return i;
	}
	return -1;
}
const char *CGameContext::MapName(int ClientId)
{
	if(!g_Config.m_SvMultimap)
		return Map()->BaseName();

	if(!CheckClientId(ClientId))
		return Map()->BaseName();

	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return Map()->BaseName();

	int PlayerMapIndex = pPlayer->MultiMapIdx();
	if(PlayerMapIndex >= 0 && PlayerMapIndex < (int)m_vMultiMaps.size())
		return m_vMultiMaps[PlayerMapIndex]->m_pMap->BaseName();
	return Map()->BaseName();
}

bool CGameContext::ChatDetection(int ClientId, const char *pMsg)
{
	// Thx to Pointer31 for the blueprint
	if(ClientId < 0)
		return false;

	if(Server()->IsRconAuthed(ClientId))
		return false;

	if(m_vChatDetection.empty())
		return false;

	const char *pIgnoredPrefixes[] = {"/report"};

	for(const char *pPrefix : pIgnoredPrefixes)
	{
		if(str_startswith_nocase(pMsg, pPrefix))
			return false;
	}

	float Count = 0; // amount of flagged strings (some strings may count more than others)
	int BanDuration = 0;
	char Reason[64] = "Chat Detection Auto Ban";
	bool IsBan = false;

	const char *ClientName = Server()->ClientName(ClientId);
	const char *pText = FontConvert(pMsg);
	// const char *pActualText = pMsg;

	// make a check for the latest message and the new message coming in right now
	// if the message is the same as the latest one but in a fancy font, then they use some bot client
	// aka ban them

	std::vector<std::string> FoundStrings;

	for(const auto &Entry : m_vChatDetection)
	{
		if(Entry.String()[0] == '\0')
			continue;

		if(str_find_nocase(pText, Entry.String()))
		{
			FoundStrings.emplace_back(Entry.String());

			if(Entry.Time() > BanDuration && BanDuration >= 0)
				BanDuration = Entry.Time();
			else if(Entry.Time() == -1)
				BanDuration = -1;

			Count += Entry.Addition();

			if(!IsBan) // if one of the strings is a ban string, then we set IsBan to true
				IsBan = Entry.IsBan();

			if(str_comp(Entry.Reason(), "") != 0)
				str_copy(Reason, Entry.Reason());
		}
	}

	char InfoMsg[256] = "";
	if(!FoundStrings.empty())
	{
		for(const auto &Str : FoundStrings)
		{
			str_append(InfoMsg, Str.c_str());
			if(&Str != &FoundStrings.back())
				str_append(InfoMsg, ", ");
		}
		log_info("chat-detection", "Name: %s | Strings Found: %s", ClientName, InfoMsg);
	}

	if(g_Config.m_SvAntiAdBot)
	{
		// anti whisper ad bot
		if((str_find_nocase(pText, "/whisper") || str_find_nocase(pText, "/w")) && str_find_nocase(pText, "bro, check out this client"))
		{
			if(!TryingToBeFunny(pText))
			{
				str_copy(Reason, "Bot Client Message");
				IsBan = true;
				Count += 2;
				BanDuration = 1000;
			}
		}

		// anti mass ping ad bot
		if((str_find_nocase(pText, "stop being a noob") && str_find_nocase(pText, "get good with")) ||
			(str_find_nocase(pText, "Think you could do better") && str_find_nocase(pText, "Not without")) ||
			str_find_nocase(pText, "if I was cheating")) // mass ping advertising
		{
			if(str_length(pText) > 70) // Usually it pings alot of people
			{
				// try to not remove their message if they are just trying to be funny
				if(!TryingToBeFunny(pText))
				{
					IsBan = true;
					Count += 2;
					BanDuration = 1200;
				}
				if(str_find(pText, " ")) // This is the little white space it uses between some letters
				{
					IsBan = true;
					Count += 2;
					BanDuration = 1200;
				}
				str_copy(Reason, "Bot Client Message");
			}
		}
		if(IsBan && FoundStrings.empty())
			str_copy(InfoMsg, "Bot Client Message");
	}

	if(Count >= 2.0 && BanDuration > 0)
	{
		if(IsBan)
		{
			const char *pClientAddr = Server()->ClientAddrString(ClientId, false);
			char aBanBuf[1024];
			str_format(aBanBuf, sizeof(aBanBuf),
				"`%s` [||%s||] was banned for %d minutes for triggering the Chat-Detection.\n"
				"Strings: `%s`\n"
				"Msg: `%s`\n"
				"ver: %s (%d) [%s]",
				Server()->ClientName(ClientId),
				pClientAddr,
				BanDuration,
				InfoMsg,
				pMsg, // Unconverted message for better understanding of what they tried to send
				Server()->GetCustomClient(ClientId),
				GetClientVersion(ClientId),
				GetClientVersionStr(ClientId));
			char aTitle[48];
			str_format(aTitle, sizeof(aTitle), "[BAN] - Chat Detection (%d%s)", Server()->Port(), FormatServerInsntance(" | "));
			Server()->SendWebhookMessage(g_Config.m_DcBansWebhookUrl, aBanBuf, aTitle);
			char aCmdBuf[512];
			str_format(aCmdBuf, sizeof(aCmdBuf), "ban %s %d %s", pClientAddr, BanDuration, Reason);
			Console()->ExecuteLine(aCmdBuf, IConsole::CLIENT_ID_FOXNET);
		}
		else
			MuteWithMessage(Server()->ClientAddr(ClientId), BanDuration * 60, Reason, ClientName);

		return true; // Don't send their chat message
	}
	return false;
}

bool CGameContext::NameDetection(int ClientId, const char *pName, bool PreventNameChange)
{
	if(ClientId < 0)
		return false;

	if(Server()->IsRconAuthed(ClientId))
		return false;

	if(m_vNameDetection.empty())
		return false;

	const char *ClientName = pName;

	int BanDuration = 0;
	char Reason[64] = "Name Detection Auto Ban";

	std::vector<std::string> FoundStrings;

	for(const auto &Entry : m_vNameDetection)
	{
		if(Entry.String()[0] == '\0')
			continue;

		bool FoundEntry = false;

		int ExactMatch = Entry.ExactMatch();

		switch(ExactMatch)
		{
		case 0:
			if(str_find_nocase(ClientName, Entry.String()))
				FoundEntry = true;
			break;
		case 1:
			if(!str_comp(ClientName, Entry.String()))
				FoundEntry = true;
			break;
		case 2:
			if(!str_comp_nocase(ClientName, Entry.String()))
				FoundEntry = true;
			break;

		default:
			break;
		}

		if(FoundEntry)
		{
			FoundStrings.emplace_back(Entry.String());

			if(Entry.Time() > BanDuration && BanDuration >= 0)
				BanDuration = Entry.Time();
			else if(Entry.Time() == -1)
				BanDuration = -1;

			if(str_comp(Entry.Reason(), "") != 0)
				str_copy(Reason, Entry.Reason());
		}
	}

	if(!FoundStrings.empty())
	{
		char InfoMsg[256] = "";
		for(const auto &Str : FoundStrings)
		{
			str_append(InfoMsg, Str.c_str());
			if(&Str != &FoundStrings.back())
				str_append(InfoMsg, ", ");
		}
		log_info("name-detection", "Name: %s | Strings Found: %s", ClientName, InfoMsg);

		if(!PreventNameChange && BanDuration > 0)
		{
			const char *pClientAddr = Server()->ClientAddrString(ClientId, false);
			char aBanBuf[1024];
			str_format(aBanBuf, sizeof(aBanBuf),
				"`%s` [||%s||] was banned for %d minutes for triggering the Name-detection.\n"
				"Strings: %s\n"
				"ver: %s (%d) [%s]",
				Server()->ClientName(ClientId),
				Server()->ClientAddrString(ClientId, false),
				BanDuration,
				InfoMsg,
				Server()->GetCustomClient(ClientId),
				GetClientVersion(ClientId),
				GetClientVersionStr(ClientId));
			char aTitle[48];
			str_format(aTitle, sizeof(aTitle), "[BAN] - Name Detection (%d%s)", Server()->Port(), FormatServerInsntance(" | "));
			Server()->SendWebhookMessage(g_Config.m_DcBansWebhookUrl, aBanBuf, aTitle);

			char aCmdBuf[512];
			str_format(aCmdBuf, sizeof(aCmdBuf), "ban %s %d %s", pClientAddr, BanDuration, Reason);
			Console()->ExecuteLine(aCmdBuf, IConsole::CLIENT_ID_FOXNET);
		}
		return true;
	}

	return false;
}

void CGameContext::OnLogin(int ClientId)
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients())
		return;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	pPlayer->m_LastReport = Server()->Tick();

	if(g_Config.m_SvAccountsForced && pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		SendMovingTilesInfo(ClientId);
		pPlayer->SetTeam(TEAM_GAME, false);
	}

	pPlayer->CheckLevelUp();

	// pPlayer->m_AccLoginAttempts = 0; // reset login attempts on successful login

	if(pPlayer->Acc()->m_aLastName[0] == '\0')
	{
		SendChatTarget(ClientId, "This seems to be your first Login, welcome!");
		SendChatTarget(ClientId, "Most special features are accessible trough the vote menu");
		SendChatTarget(ClientId, "For more Info about this server head to the vote menu and double click on 'Server Info'");
	}
	else if(!Server()->ClientPrevIngame(ClientId))
	{
		int UnreadMails = 0;
		for(const CMailBox::CMail &Mail : pPlayer->Acc()->m_MailBox.m_vMails)
		{
			const bool Unread = Mail.m_Unread || (!Mail.m_UsedCmd && Mail.m_aCmd[0] != '\0');
			if(Unread)
				UnreadMails++;
		}

		char aBuf[256];
		if(UnreadMails == 0)
			str_format(aBuf, sizeof(aBuf), "Welcome back, %s!", Server()->ClientName(ClientId));
		else
			str_format(aBuf, sizeof(aBuf), "Welcome back, %s! You have %d unread mail%s", Server()->ClientName(ClientId), UnreadMails, UnreadMails == 1 ? "" : "s");
		SendChatTarget(ClientId, aBuf);
	}

	ClearVotes(ClientId);
}

void CGameContext::OnLogout(int ClientId)
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients())
		return;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	// Stakes are settled in CAccounts::Logout, before the account is written out. Doing it here would
	// be too late: by this point m_Money has already been copied into the save.

	if(g_Config.m_SvAccountsForced && pPlayer->GetTeam() != TEAM_SPECTATORS)
		pPlayer->SetTeam(TEAM_SPECTATORS, false);

	pPlayer->DisableAllCosmetics();
	ClearVotes(ClientId);
}

void CGameContext::SendEmote(int ClientId, int Type, int TargetId)
{
	if(Server()->ClientSlotEmpty(ClientId) || Server()->ClientSlotEmpty(TargetId))
		return;

	if(!m_apPlayers[ClientId] || !m_apPlayers[TargetId])
		return;

	SendEmoticon(ClientId, Type, TargetId);
}

void CGameContext::CreateIndEffect(int Type, vec2 Pos, vec2 Direction, CClientMask Mask)
{
	float AngleOffset = 0;
	float StarDistance = 0.18f;
	float Angle = -std::atan2(Direction.x, Direction.y);

	CDamageIndEffects Effect;
	Effect.m_Mask = Mask;
	if(Type >= INDTYPE_CLOCKWISE && Type <= INDTYPE_COUNTERWISE)
	{
		AngleOffset = 0.80f;
		Effect.m_Remaining = 10;
		for(int Remaining = 0; Remaining < Effect.m_Remaining; Remaining++)
		{
			if(Type == INDTYPE_CLOCKWISE)
				Effect.m_vAngles.push_back(Angle - AngleOffset + (Remaining * StarDistance));
			else
				Effect.m_vAngles.push_back(Angle + AngleOffset - (Remaining * StarDistance));
		}
		Effect.m_vPos.push_back(Pos);
		Effect.m_Delay = 1;
		Effect.m_NextTick = Server()->Tick();
		m_vDamageIndEffects.push_back(Effect);
	}
	else if(Type == INDTYPE_INWARD)
	{
		AngleOffset = -0.90f;

		for(int i = 0; i < 2; i++)
		{
			Effect.m_Remaining = 5;
			for(int Remaining = 0; Remaining < Effect.m_Remaining; Remaining++)
			{
				if(i == 0)
					Effect.m_vAngles.push_back(Angle + AngleOffset + (Remaining * StarDistance));
				else
					Effect.m_vAngles.push_back(Angle - AngleOffset - (Remaining * StarDistance));
			}
			Effect.m_vPos.push_back(Pos);
			Effect.m_Delay = 2;
			Effect.m_NextTick = Server()->Tick();
			m_vDamageIndEffects.push_back(Effect);
		}
	}
	else if(Type == INDTYPE_OUTWARD)
	{
		AngleOffset = 0.20f;

		for(int i = 0; i < 2; i++)
		{
			Effect.m_Remaining = 5;
			for(int Remaining = 0; Remaining < Effect.m_Remaining; Remaining++)
			{
				if(i == 0)
					Effect.m_vAngles.push_back(Angle - AngleOffset - (Remaining * StarDistance));
				else
					Effect.m_vAngles.push_back(Angle + AngleOffset + (Remaining * StarDistance));
			}
			Effect.m_vPos.push_back(Pos);
			Effect.m_Delay = 2;
			Effect.m_NextTick = Server()->Tick();
			m_vDamageIndEffects.push_back(Effect);
		}
	}
	else if(Type == INDTYPE_LINE)
	{
		Effect.m_Remaining = 6;
		for(int Remaining = 0; Remaining < Effect.m_Remaining; Remaining++)
		{
			float Offset = Remaining * 15.0f;
			vec2 CalcPos = Pos - Direction * 25.0f + Direction * Offset;
			Effect.m_vPos.push_back(CalcPos);
		}
		Effect.m_vAngles.push_back(Angle - AngleOffset);

		Effect.m_Delay = 1;
		Effect.m_NextTick = Server()->Tick();
		m_vDamageIndEffects.push_back(Effect);
	}
	else if(Type == INDTYPE_CRISSCROSS)
	{
		Effect.m_Remaining = 3;
		for(int Remaining = 0; Remaining < Effect.m_Remaining; Remaining++)
		{
			vec2 CalcPos;
			float PerpAngle = 0.0f;

			float GetAngle = angle(Direction);
			if(GetAngle < 0.0f)
				GetAngle += 2.0f * pi;

			if(Remaining == 0)
			{
				PerpAngle = GetAngle - AngleOffset + pi / 2;
				Effect.m_vAngles.push_back(Angle - AngleOffset - 0.85f);
				CalcPos = Pos + vec2(cosf(PerpAngle), sinf(PerpAngle)) * 25.0f;
			}
			else if(Remaining == 1)
			{
				CalcPos = Pos - Direction * 15.0f;
				Effect.m_vAngles.push_back(Angle - AngleOffset);
			}
			else
			{
				PerpAngle = GetAngle - AngleOffset - pi / 2;
				Effect.m_vAngles.push_back(Angle - AngleOffset + 0.85f);
				CalcPos = Pos + vec2(cosf(PerpAngle), sinf(PerpAngle)) * 25.0f;
			}

			Effect.m_vPos.push_back(CalcPos);
		}

		Effect.m_Delay = 1;
		Effect.m_NextTick = Server()->Tick();
		m_vDamageIndEffects.push_back(Effect);
	}
	else
	{
		CreateDamageInd(Pos, Angle, 10, Mask);
	}
}

bool CGameContext::IsValidHookPower(int HookPower)
{
	return HookPower == HOOKTYPE_NORMAL || HookPower == HOOKTYPE_RAINBOW || HookPower == HOOKTYPE_BLOODY;
}

const char *CGameContext::HookTypeName(int HookType)
{
	switch(HookType)
	{
	case HOOKTYPE_NORMAL:
		return "Normal Hook";
	case HOOKTYPE_RAINBOW:
		return "Rainbow Hook";
	case HOOKTYPE_BLOODY:
		return "Bloody Hook";
	}
	return "Unknown";
}

void CGameContext::UnsetTelekinesis(int ClientId) const
{
	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(pPlayer && pPlayer->m_TelekinesisId == ClientId)
		{
			pPlayer->m_TelekinesisId = -1;
			break; // can break here, every entity can only be picked by one player using telekinesis at the time
		}
	}
}

bool CGameContext::SendFakeTuningParams(int ClientId, const CTuningParams &FakeTuning, bool RealTune)
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients() || !GetPlayerChar(ClientId))
		return false;

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	for(unsigned i = 0; i < sizeof(FakeTuning) / sizeof(int); i++)
	{
		Msg.AddInt(((int *)&FakeTuning)[i]);
	}

	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientId);

	if(RealTune)
		GetPlayerChar(ClientId)->SetFakeTuned(true, FakeTuning);
	else
		GetPlayerChar(ClientId)->SetFakeTuned(true);

	return true;
}

bool CGameContext::ResetFakeTunes(int ClientId, int Zone)
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients() || !GetPlayerChar(ClientId))
		return false;

	GetPlayerChar(ClientId)->SetFakeTuned(false);
	SendTuningParams(ClientId, Zone);
	return true;
}

void CGameContext::Explosion(vec2 Pos, CClientMask Mask)
{
	CNetEvent_Explosion *pEvent = m_Events.Create<CNetEvent_Explosion>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

int CGameContext::GetWeaponType(int Weapon)
{
	if(const CCustomWeaponInfo *pWeaponInfo = GetCustomWeaponInfo(Weapon))
		return pWeaponInfo->m_SnapWeapon;
	return Weapon;
}

static const char *GetMapName(const char *pCmd)
{
	const char *pChangeMap = str_find(pCmd, "change_map ");
	if(pChangeMap)
	{
		pChangeMap += str_length("change_map ");
		// Copy until space, semicolon, or end
		static char s_aMapName[64] = {0};
		int i = 0;
		while(pChangeMap[i] && pChangeMap[i] != ' ' && pChangeMap[i] != ';' && i < (int)sizeof(s_aMapName) - 1)
		{
			s_aMapName[i] = pChangeMap[i];
			i++;
		}
		s_aMapName[i] = 0;
		return s_aMapName;
	}
	return "";
}

bool CGameContext::RandomMapVote()
{
	int Count = 0;
	std::vector<const char *> MapVotes;

	for(CVoteOptionServer *pOption = m_pVoteOptionFirst; pOption; pOption = pOption->m_pNext, Count++)
	{
		if(!str_find(pOption->m_aCommand, "change_map "))
			continue;

		if(!str_comp(GetMapName(pOption->m_aCommand), Map()->BaseName()))
			continue;

		MapVotes.push_back(pOption->m_aCommand);
	}

	if(MapVotes.empty())
		return false;

	std::random_device Rd;
	std::uniform_int_distribution<int> Dist(0, (int)MapVotes.size() - 1);
	int Random = Dist(Rd);

	Console()->ExecuteLine(MapVotes[Random], IConsole::CLIENT_ID_UNSPECIFIED);
	return true;
}

void CGameContext::SendCommandInfo(int ClientId, const char *pName, const char *pParams, const char *pHelp) const
{
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	for(const std::string &ReceivedName : pPlayer->m_vReceivedConditionals)
	{
		if(ReceivedName == pName)
			return;
	}

	if(Server()->IsSixup(ClientId))
	{
		protocol7::CNetMsg_Sv_CommandInfo Msg;
		Msg.m_pName = pName;
		Msg.m_pArgsFormat = pParams;
		Msg.m_pHelpText = pHelp;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
	}
	else
	{
		CNetMsg_Sv_CommandInfo Msg;
		Msg.m_pName = pName;
		Msg.m_pArgsFormat = pParams;
		Msg.m_pHelpText = pHelp;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
	}

	pPlayer->m_vReceivedConditionals.emplace_back(pName);
}

void CGameContext::SendCommandInfoRemove(int ClientId, const char *pName) const
{
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const auto It = std::find(
		pPlayer->m_vReceivedConditionals.begin(),
		pPlayer->m_vReceivedConditionals.end(),
		pName);

	if(It == pPlayer->m_vReceivedConditionals.end())
		return;

	if(Server()->IsSixup(ClientId))
	{
		protocol7::CNetMsg_Sv_CommandInfoRemove Msg;
		Msg.m_pName = pName;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
	}
	else
	{
		CNetMsg_Sv_CommandInfoRemove Msg;
		Msg.m_pName = pName;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
	}

	pPlayer->m_vReceivedConditionals.erase(It);
}

bool CGameContext::SendServerAlert(const char *pMessage, int ClientId) const
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients())
		return false;
	if(Server()->ClientSlotEmpty(ClientId))
		return false;
	if(!m_apPlayers[ClientId])
		return false;

	if(m_apPlayers[ClientId]->GetClientVersion() >= VERSION_DDNET_IMPORTANT_ALERT)
	{
		CNetMsg_Sv_ServerAlert Msg;
		Msg.m_pMessage = pMessage;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
		return true;
	}
	return false;
}

void CGameContext::SendMovingTilesInfo(int ClientId)
{
	/*if(Collision()->HasMovingQuads())
	{
		const char *pWarn = "Turn off entities, this map uses Moving Tiles";

		if(!SendServerAlert(pWarn, ClientId))
			SendBroadcast(pWarn, ClientId);
		SendChatTarget(ClientId, pWarn);
	}*/
}

void CGameContext::OnFoxNetMessage(int MsgId, CUnpacker *pUnpacker, int ClientId)
{
	CAccountSession &Acc = m_aAccounts[ClientId];
	switch(MsgId)
	{
	case NETMSG_FOXNET_FASTINPUTS:
	{
		const int Set = pUnpacker->GetInt();
		const int Amount = pUnpacker->GetIntOrDefault(20);
		Acc.m_Configs.m_FastInputs = Set;
		Acc.m_Configs.m_FastInputAmount = Amount;
		Acc.m_Configs.m_SentFastInput = true; // mark as sent to not overwrite on next login
		break;
	}
	case NETMSG_FOXNET_COSMETIC_SNAPS:
	{
		CPlayer *pPlayer = m_apPlayers[ClientId];
		if(!pPlayer)
			return;

		pPlayer->m_SupportsCosmeticSnaps = true;
		break;
	}
	default:
		break;
	}
}

bool CGameContext::IncludedInServerInfo(int ClientId)
{
	bool Included = true;

	if(Server()->DebugDummy(ClientId))
		Included = false;

	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(pPlayer)
	{
		if(pPlayer->m_IncludeServerInfo)
			Included = pPlayer->m_IncludeServerInfo;
		if(pPlayer->m_Vanish)
			Included = false;
	}

	return Included;
}

void CGameContext::OnPreShutdown()
{
	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(!pPlayer)
			continue;

		m_AccountManager.Logout(i, true);
	}

	m_AccountManager.LogoutAllAccountsPort(Server()->Port(), g_Config.m_SvAccountsInstance, true); // Save all info before CPlayer is destroyed
}

void CGameContext::OnPreReload()
{
	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(!pPlayer)
			continue;

		// A reload never reaches CAccounts::Logout, the account is carried across in memory by
		// OnClientDataPersist instead. That copy happens right after this, and the zones are not torn
		// down until later still, so anything staked has to be settled here or it is copied away.
		m_ZoneManager.OnClientReset(i, pPlayer->MultiMapIdx());

		m_apPersistentData[i] = new CSavePlayerData();
		m_apPersistentData[i]->Save(pPlayer);
	}
}

bool CGameContext::IsWeekend() const
{
	using namespace std::chrono;
	auto Now = system_clock::now();
	std::time_t t = system_clock::to_time_t(Now);
	std::tm Tm{};
#if defined(_WIN32)
	const errno_t Err = localtime_s(&Tm, &t);
	if(Err != 0)
		return false;
#else
	if(localtime_r(&t, &Tm) == nullptr)
		return false;
#endif
	return Tm.tm_wday == 5 || Tm.tm_wday == 6 || Tm.tm_wday == 0;
}

int CGameContext::NumPlayersInTeam(int Team) const
{
	CGameTeams &Teams = m_pController->Teams();

	int Count = 0;
	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(Server()->ClientSlotEmpty(i) || !pPlayer)
			continue;
		if(Teams.m_Core.Team(i) == Team)
			Count++;
	}
	return Count;
}

void CGameContext::OnExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, int MultiMapIdx, CClientMask Mask)
{
	// deal damage
	CEntity *apDrops[(int)MAX_CLIENTS * 10];
	float Radius = 135.0f;
	float InnerRadius = 48.0f;
	int NumDrops = m_World.FindEntities(Pos, Radius, apDrops, std::size(apDrops), CGameWorld::ENTTYPE_PICKUPDROP, MultiMapIdx);
	for(int i = 0; i < NumDrops; i++)
	{
		auto *pPickup = static_cast<CPickupDrop *>(apDrops[i]);
		if(!pPickup)
			continue;

		vec2 Diff = pPickup->m_Pos - Pos;
		vec2 ForceDir(0, 1);
		float l = length(Diff);
		if(l)
			ForceDir = normalize(Diff);
		l = 1 - std::clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
		float Strength;
		if(Owner == -1 || !m_apPlayers[Owner] || !m_apPlayers[Owner]->m_TuneZone)
			Strength = GlobalTuning(MultiMapIdx)->m_ExplosionStrength;
		else
			Strength = TuningList(MultiMapIdx)[m_apPlayers[Owner]->m_TuneZone].m_ExplosionStrength;

		float Dmg = Strength * l;
		if(!(int)Dmg)
			continue;

		if((GetPlayerChar(Owner) ? !GetPlayerChar(Owner)->GrenadeHitDisabled() : g_Config.m_SvHit) || NoDamage)
		{
			int PickupTeam = pPickup->Team();

			if(Owner == -1 && ActivatedTeam != -1 && PickupTeam != ActivatedTeam)
				continue;
			if(Owner != -1)
			{
				CCharacter *pOwnerChar = GetPlayerChar(Owner);
				if(pOwnerChar && PickupTeam != pOwnerChar->Team() && ActivatedTeam != TEAM_SUPER && pOwnerChar->Team() != TEAM_SUPER)
					continue;
			}

			// Explode at most once per team
			if((GetPlayerChar(Owner) ? GetPlayerChar(Owner)->GrenadeHitDisabled() : !g_Config.m_SvHit) || NoDamage)
			{
				if(PickupTeam == TEAM_SUPER)
					continue;
				if(!Mask.test(PickupTeam))
					continue;
				Mask.reset(PickupTeam);
			}

			pPickup->TakeDamage(ForceDir * Dmg * 2);
		}
	}
}

void CGameContext::OnHammerHit(CCharacter *pChr, vec2 StartPos, float HammerStrength)
{
	const float Radius = pChr->GetProximityRadius() * 0.5f;
	const vec2 CharPos = pChr->m_Pos;
	const int ActivatedTeam = pChr->Team();

	bool SetToPickupMask = false;
	CClientMask Mask = pChr->TeamMask();

	// deal damage
	CEntity *apDrops[(int)MAX_CLIENTS * 10];
	int Hits = 0;
	int NumDrops = m_World.FindEntities(StartPos, Radius, apDrops, std::size(apDrops), CGameWorld::ENTTYPE_PICKUPDROP, pChr->MultiMapIdx());

	for(int i = 0; i < NumDrops; ++i)
	{
		auto *pPickup = static_cast<CPickupDrop *>(apDrops[i]);
		if(!pPickup)
			continue;

		if(pPickup->Team() != ActivatedTeam && ActivatedTeam != TEAM_SUPER)
			continue;

		if(!SetToPickupMask)
		{
			Mask = pPickup->PickupMask(pChr->GetPlayer()->GetCid());
			SetToPickupMask = true;
		}

		vec2 Dir;
		if(length(pPickup->m_Pos - CharPos) > 0.0f)
			Dir = normalize(pPickup->m_Pos - CharPos);
		else
			Dir = vec2(0.f, -1.f);

		float Strength = HammerStrength;

		vec2 Temp = pPickup->GetVelocity() + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
		Temp = ClampVel(pPickup->MoveRestrictions(), Temp);
		Temp -= pPickup->GetVelocity();
		pPickup->TakeDamage((vec2(0.f, -1.0f) + Temp) * Strength);

		Hits++;
	}
	if(Hits != 0)
	{
		CreateHammerHit(StartPos, Mask); // Could get loud so we do it here
		float FireDelay = pChr->GetTuning(pChr->GetOverriddenTuneZone())->m_HammerHitFireDelay;
		pChr->SetReloadTimer(FireDelay * Server()->TickSpeed() / 1000);
	}
}

bool CGameContext::SetPredictEventsFlag(int ClientId) const
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients())
		return false;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	if(pPlayer->Cosmetics()->m_ConfettiGun)
		return false;
	if(pPlayer->Cosmetics()->m_DamageIndType != INDTYPE_NONE)
		return false;
	if(pPlayer->Cosmetics()->m_GunType != EGunType::None)
		return false;
	if(pPlayer->Cosmetics()->m_PhaseGun)
		return false;
	if(pPlayer->Cosmetics()->m_EmoticonGun)
		return false;

	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		if(i == ClientId)
			continue;
		CPlayer *pOtherPlayer = m_apPlayers[i];
		if(!pOtherPlayer)
			continue;
		if(pOtherPlayer->Cosmetics()->m_PhaseGun)
			return false;
	}

	return true;
}

bool CGameContext::CanUseCmd(int ClientId, const char *pCmd)
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients())
		return false;

	if(Server()->ClientSlotEmpty(ClientId))
		return false;

	const IConsole::ICommandInfo *pInfo = Console()->GetCommandInfo(pCmd, CFGFLAG_CHAT);
	if(!pInfo)
		return false;

	const IConsole::EAccessLevel Required = pInfo->GetAccessLevel();
	IConsole::EAccessLevel ClientLevel = IConsole::EAccessLevel::USER;
	switch(Server()->GetAuthedState(ClientId))
	{
	case AUTHED_ADMIN: ClientLevel = IConsole::EAccessLevel::ADMIN; break;
	case AUTHED_MOD: ClientLevel = IConsole::EAccessLevel::MODERATOR; break;
	case AUTHED_HELPER: ClientLevel = IConsole::EAccessLevel::HELPER; break;
	default: ClientLevel = IConsole::EAccessLevel::USER; break;
	}
	return Required >= ClientLevel;
}

int CGameContext::ClientIdByName(const char *pName) const
{
	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		if(Server()->ClientSlotEmpty(i))
			continue;
		if(str_comp(Server()->ClientName(i), pName) == 0)
			return i;
	}
	return -1;
}

CTuningParams CGameContext::DDNetDefaultTuning() const
{
	CTuningParams Tune = CTuningParams::DEFAULT;
	Tune.m_GunCurvature = 0;
	Tune.m_GunSpeed = 1400;
	Tune.m_ShotgunCurvature = 0;
	Tune.m_ShotgunSpeed = 500;
	Tune.m_ShotgunSpeeddiff = 0;
	return Tune;
}

bool CGameContext::GetNearestAirPos(vec2 Pos, vec2 *pOut, float Radius)
{
	const float Size = Radius * 0.5f;
	const vec2 SizeVec2 = vec2(Size, Size);

	if(!Collision()->TestBox(Pos, SizeVec2))
	{
		*pOut = Pos;
		return true;
	}

	static constexpr int SearchRadius = 12;
	static constexpr float Step = 16.0f;

	float BestDist = std::numeric_limits<float>::max();
	vec2 BestPos = Pos;

	for(int y = -SearchRadius; y <= SearchRadius; y++)
	{
		for(int x = -SearchRadius; x <= SearchRadius; x++)
		{
			vec2 Candidate = Pos + vec2(x * Step, y * Step);
			if(Collision()->TestBox(Pos, SizeVec2))
				continue;

			float Dist = distance(Pos, Candidate);
			if(Dist < BestDist)
			{
				BestDist = Dist;
				BestPos = Candidate;
			}
		}
	}

	if(BestDist < std::numeric_limits<float>::max())
	{
		*pOut = BestPos;
		return true;
	}

	return false;
}
