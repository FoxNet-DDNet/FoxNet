#include "accounts.h"
#include "entities/pickupdrop.h"
#include "entities/powerup.h"
#include "fontconvert.h"
#include "persistent_data.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/message.h>
#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex_msgs.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
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
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <cstdio>
#include <engine/shared/packer.h>
#include <engine/map.h>
#include <base/fs.h>
#include <base/types.h>

CCollision *CGameContext::Collision(int ClientId)
{
	if(!CheckClientId(ClientId))
		return &m_Collision;
	if(Server()->ClientSlotEmpty(ClientId))
		return &m_Collision;

	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return &m_Collision;
	if(pPlayer->m_MapOverridden)
		return &m_MapOverride.m_Collision;
	return &m_Collision;
}

void CGameContext::CMapOverride::Init()
{
	m_Layers.Init(m_pMap.get(), false);
	m_Collision.Init(&m_Layers);
	m_MapLoaded = true;
}

void CGameContext::CMapOverride::Reset()
{
	m_pMap.get()->Unload();
	m_pMap.reset();
	m_Layers.Unload();
	m_Collision.Unload();
	m_MapLoaded = false;
}

void CGameContext::FoxNetTick()
{
	m_VoteMenu.Tick();
	HandleEffects();
	PowerUpSpawner();

	if(g_Config.m_SvAntiBot)
		BotClientTick();

	// process async db account results
	m_AccountManager.Tick();

	if(g_Config.m_SvBanSyncing)
		BanSync();

	// Set moving tiles time for quads with pos envelopes
	m_Collision.SetTime(m_pController->GetTime());
	m_Collision.UpdateQuadCache();

	// Save all logged in accounts every 15 minutes
	if(Server()->Tick() % (Server()->TickSpeed() * 60 * 15) == 0)
	{
		m_AccountManager.SaveAllAccounts();
	}
}

void CGameContext::BotClientTick()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = m_apPlayers[ClientId];
		if(Server()->ClientSlotEmpty(ClientId) || !pPlayer)
			continue;
		if(pPlayer->m_HasBotClient)
			continue;
		if(pPlayer->m_BotChecked)
			continue;

		IServer::CClientInfo Info;
		if(!Server()->GetClientInfo(ClientId, &Info))
			continue;

		if(!Info.m_GotDDNetVersion) // No version info
		{
			// pPlayer->m_HasBotClient = true;
			continue;
		}

		const char *pClientAddr = Server()->ClientAddrString(ClientId, false);
		const char *pClientName = Server()->GetCustomClient(ClientId);
		const int ClientVersion = Info.m_DDNetVersion;
		const char *pVersionStr = Info.m_pDDNetVersionStr ? Info.m_pDDNetVersionStr : "Client too old";

		const char *pStart = str_find(pVersionStr, "(");
		const char *pEnd = str_find(pVersionStr, ")");

		const bool HasGitRevShort = pStart && pEnd && pEnd > pStart;

		bool KnownBot = false;

		// check if git rev short is empty
		if(HasGitRevShort)
		{
			if(pStart && pEnd && pEnd > pStart + 1)
			{
				char aGitRevShort[16];
				str_copy(aGitRevShort, pStart + 1, std::min<size_t>(pEnd - (pStart + 1) + 1, sizeof(aGitRevShort)));
				if(str_length(aGitRevShort) == 0)
					pPlayer->m_HasBotClient = true;
			}
		}

		if(!str_comp_nocase(pClientName, "DDNet"))
		{
			if(str_find(pVersionStr, "18.9.1"))
				pPlayer->m_HasBotClient = true;
		}

		if(ClientVersion == 18091) // Most likely a bot, if not they should just update to the newest ddnet version.
			pPlayer->m_HasBotClient = true;

		if(str_find(pVersionStr, "imacrack")) // free version of a bot client sends this.
			KnownBot = true;

		const char *pVerStart = str_find_nocase(pVersionStr, "DDNet ");
		if(pVerStart)
		{
			pVerStart += str_length("DDNet ");
			int Major = 0;
			int Minor = 0;
			int Patch = 0;
			sscanf(pVerStart, "%d.%d.%d", &Major, &Minor, &Patch);
			int CalculatedVersion = Major * 1000 + Minor * 10 + Patch;
			if(CalculatedVersion != ClientVersion)
				KnownBot = true;
		}

		for(CBotClientDetection &Detection : m_vBotClientDetections)
		{
			if(Detection.m_DDNetVersion != -1)
			{
				if(ClientVersion != Detection.m_DDNetVersion)
					continue;
			}

			if(Detection.m_pClientName[0] != '\0')
			{
				if(str_comp(Detection.m_pClientName, pClientName) != 0)
					continue;
			}

			if(Server()->GotDDNetVersionPacket(ClientId))
			{
				if(Detection.m_pDDNetVersionStr[0] != '\0')
				{
					if(str_comp(Detection.m_pDDNetVersionStr, pVersionStr) != 0)
						continue;
				}
			}
			else
			{
				if(str_comp(Detection.m_pDDNetVersionStr, "Client too old") != 0)
					continue;
			}

			if(Detection.m_Ban)
				KnownBot = true;
			else
				pPlayer->m_HasBotClient = true;
			break;
		}

		if(KnownBot)
		{
			pPlayer->m_HasBotClient = true;
			if(g_Config.m_SvAntiBot == 2)
			{
				char aBanBuf[256];
				str_format(aBanBuf, sizeof(aBanBuf), "`%s` [%s] was banned for %d minutes for using a bot client.\n"
								     "ver: %s (%d) [%s]",
					Server()->ClientName(ClientId),
					pClientAddr,
					g_Config.m_SvAntiBotBantime,
					pClientName,
					ClientVersion,
					pVersionStr);
				char aTitle[32];
				str_format(aTitle, sizeof(aTitle), "[BAN] - Bot Client (%d)", Server()->Port());
				Server()->SendWebhookMessage(g_Config.m_DcBansWebhookUrl, aBanBuf, aTitle);

				char aCmdBuf[512];
				str_format(aCmdBuf, sizeof(aCmdBuf), "ban %s %d %s", pClientAddr, g_Config.m_SvAntiBotBantime, "Download a suitable client form ddnet.org or entityclient.net");
				Console()->ExecuteLine(aCmdBuf, IConsole::CLIENT_ID_FOXNET);
				continue;
			}

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "'%s' is using a Cheat Client, laugh at them.", Server()->ClientName(ClientId));
			SendChat(-1, TEAM_ALL, aBuf);
		}

		pPlayer->m_BotChecked = true;
	}
}

void CGameContext::OnFoxNetConsoleInit()
{
	m_Scripting.OnConsoleInit(this);
	RegisterFoxNetCommands();
	m_MapOverride.m_MapLoaded = false;
	m_MapOverride.m_pMap = CreateMap();

	const char *pMapName = g_Config.m_SvCasinoMapName;
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);
	if(!str_valid_filename(fs_filename(aBuf)))
	{
		log_error("server", "The name '%s' cannot be used for maps because not all platforms support it", aBuf);
		return;
	}
	if(!m_MapOverride.m_pMap.get()->Load(pMapName, Storage(), aBuf, IStorage::TYPE_ALL))
	{
		log_error("server", "Failed to load casino map '%s'", aBuf);
		return;
	}
	log_info("server", "Casino map loaded: %s", aBuf);
	m_MapOverride.Init();
}

void CGameContext::FoxNetInit()
{
	m_Scripting.OnInit(this);
	m_AccountManager.Init(this, ((CServer *)Server())->DbPool());
	m_VoteMenu.Init(this);
	m_Shop.Init(this);
	m_vPowerups.clear();

	m_PowerUpDelay = Server()->Tick() + Server()->TickSpeed() * 5;
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

int CGameContext::RandGeometric(std::mt19937 &rng, int Min, int Max, double p)
{
	if(Max < Min)
		std::swap(Min, Max);
	p = std::clamp(p, 1e-9, 1.0 - 1e-9);
	std::geometric_distribution<int> geo(p);
	int range = Max - Min;
	int k = geo(rng);
	if(k > range)
		k = range;
	return Min + k;
}

void CGameContext::PowerUpSpawner()
{
	if(!g_Config.m_SvSpawnPowerUps)
		return;
	if(!g_Config.m_SvAccounts)
		return; // Powerups require accounts to store the data
	if(m_vPowerups.size() >= 6)
		return;
	if(m_PowerUpDelay > Server()->Tick())
		return;

	const auto RandomPos = GetRandomAccessiblePos();
	if(!RandomPos)
	{
		m_PowerUpDelay = Server()->Tick() + Server()->TickSpeed();
		return;
	}

	std::mt19937 rng{std::random_device{}()};
	std::uniform_int_distribution<int> dist((int)EPowerUp::INVALID + 1, (int)EPowerUp::NUM_TYPES - 1);
	EPowerUp Type = (EPowerUp)dist(rng);
	CPowerUp *NewPowerUp = new CPowerUp(&m_World, Collision(), *RandomPos, Type);

	m_vPowerups.push_back(NewPowerUp);
	m_PowerUpDelay = Server()->Tick() + Server()->TickSpeed() * 15;
}

void CGameContext::HandleEffects()
{
	// Handle DamageInd effect
	for(auto it = m_vDamageIndEffects.begin(); it != m_vDamageIndEffects.end();)
	{
		if(it->m_Remaining > 0 && Server()->Tick() >= it->m_NextTick)
		{
			int Angles = it->m_vAngles.size() - it->m_Remaining;
			if(Angles < 0)
				Angles = 0;
			int Positions = it->m_vPos.size() - it->m_Remaining;
			if(Positions < 0)
				Positions = 0;

			CreateDamageInd(it->m_vPos.at(Positions), it->m_vAngles.at(Angles), 1, it->m_Mask);

			it->m_Remaining--;
			it->m_NextTick = Server()->Tick() + it->m_Delay;
		}
		if(it->m_Remaining <= 0)
			it = m_vDamageIndEffects.erase(it);
		else
			++it;
	}
}

void CGameContext::FoxNetSnap(int ClientId, bool GlobalSnap)
{
	SnapDebuggedQuad(ClientId);

	// Snap the Fake Player
	for(auto pFakePlayer = m_vFakeSnapPlayers.begin(); pFakePlayer < m_vFakeSnapPlayers.end();)
	{
		if(auto *pClientInfo = Server()->SnapNewItem<CNetObj_ClientInfo>(pFakePlayer->m_ClientId))
		{
			StrToInts(pClientInfo->m_aName, std::size(pClientInfo->m_aName), pFakePlayer->m_aName);
			StrToInts(pClientInfo->m_aClan, std::size(pClientInfo->m_aClan), pFakePlayer->m_aClan);
			pClientInfo->m_Country = pFakePlayer->m_Country;
			StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), pFakePlayer->m_aSkinName);
			pClientInfo->m_UseCustomColor = pFakePlayer->m_CustomColors;
			pClientInfo->m_ColorBody = pFakePlayer->m_ColorBody;
			pClientInfo->m_ColorFeet = pFakePlayer->m_ColorFeet;
		}

		if(auto *pPlayerInfo = Server()->SnapNewItem<CNetObj_PlayerInfo>(pFakePlayer->m_ClientId))
		{
			pPlayerInfo->m_Latency = 0;
			pPlayerInfo->m_Score = 0;
			pPlayerInfo->m_Team = TEAM_SPECTATORS;
			pPlayerInfo->m_Local = 0;
			pPlayerInfo->m_ClientId = pFakePlayer->m_ClientId;
		}
		if(pFakePlayer->m_ClientId == -1)
			pFakePlayer = m_vFakeSnapPlayers.erase(pFakePlayer);
		else
			pFakePlayer++;
	}
}

void CGameContext::FoxNetPostGlobalSnap()
{
	for(auto pFakePlayer = m_vFakeSnapPlayers.begin(); pFakePlayer < m_vFakeSnapPlayers.end(); pFakePlayer++)
	{
		const int ClientId = pFakePlayer->m_ClientId;
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;

		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientId = ClientId;
		Msg.m_pMessage = pFakePlayer->m_aMessage;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

		log_info(pFakePlayer->m_Context, "%d:%d:%s: %s", ClientId, Msg.m_Team, pFakePlayer->m_aName, pFakePlayer->m_aMessage);

		pFakePlayer->m_ClientId = -1;
	}
}

void CGameContext::BanSync()
{
	static int64_t ExecSaveDelay = Server()->Tick() + Server()->TickSpeed();
	if(m_BanSaveDelay < Server()->Tick())
	{
		static bool ExecBans = false;

		if(Storage()->FileExists("Bans.cfg", IStorage::TYPE_ALL))
		{
			if(!ExecBans)
			{
				Server()->SetQuietBan(true);
				Console()->ExecuteBansFile();
				ExecBans = true;
				ExecSaveDelay = Server()->Tick() + Server()->TickSpeed();
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

		if(ExecSaveDelay < Server()->Tick() && ExecBans)
		{
			Console()->ExecuteLine("bans_save \"Bans.cfg\"", IConsole::CLIENT_ID_UNSPECIFIED);

			// Info Message
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ban-sync", "Saved Bans");

			ExecBans = false;
			m_BanSaveDelay = Server()->Tick() + Server()->TickSpeed() * (g_Config.m_SvBanSyncingDelay * 60);
		}
	}
	Server()->SetQuietBan(false);
}

void CGameContext::ClearVotes(int ClientId)
{
	if(ClientId == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
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
	const char *pFunsies[] = {"ddnet.org",
		"tater", "tclient","t client", "t-client", "tclient.app", // TClient
		"aiodob", "aidob", "a-client", "A Client", "A client", // AClient
		"eclient", "e client", "entity client", "e-client", "entityclient", // EClient
		"chiller", "cactus" // Chillerbot/Cactus
	}; // Other

	for(const char *pFun : pFunsies)
	{
		if(str_find_nocase(pMsg, pFun))
			return true;
	}
	return false;
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

	float count = 0; // amount of flagged strings (some strings may count more than others)
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
	std::vector<int> Times;
	FoundStrings.clear();
	Times.clear();

	for(const auto &Entry : m_vChatDetection)
	{
		if(Entry.String()[0] == '\0')
			continue;

		if(str_find_nocase(pText, Entry.String()))
		{
			FoundStrings.push_back(Entry.String());
			Times.push_back(Entry.Time());

			count += Entry.Addition();
			BanDuration = Entry.Time();

			if(!IsBan) // if one of the strings is a ban string, then we set IsBan to true
				IsBan = Entry.IsBan();

			if(str_comp(Entry.Reason(), "") != 0)
				str_copy(Reason, Entry.Reason());
		}
	}
	if(!Times.empty())
		BanDuration = *std::max_element(Times.begin(), Times.end());

	char InfoMsg[256] = "";
	if(FoundStrings.size() > 0)
	{
		for(const auto &str : FoundStrings)
		{
			str_append(InfoMsg, str.c_str());
			if(&str != &FoundStrings.back())
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
				count += 2;
				BanDuration = 1000;
			}
		}

		// anti mass ping ad bot
		if((str_find_nocase(pText, "stop being a noob") && str_find_nocase(pText, "get good with")) || (str_find_nocase(pText, "Think you could do better") && str_find_nocase(pText, "Not without"))) // mass ping advertising
		{
			if(str_length(pText) > 70) // Usually it pings alot of people
			{
				// try to not remove their message if they are just trying to be funny
				if(!TryingToBeFunny(pText))
				{
					IsBan = true;
					count += 2;
					BanDuration = 1200;
				}
				if(str_find(pText, " ")) // This is the little white space it uses between some letters
				{
					IsBan = true;
					count += 2;
					BanDuration = 1200;
				}
				str_copy(Reason, "Bot Client Message");
			}
		}
		if(IsBan && FoundStrings.empty())
			str_copy(InfoMsg, "Bot Client Message");
	}

	if(count >= 2.0 && BanDuration > 0)
	{
		if(IsBan)
		{
			const char *pClientAddr = Server()->ClientAddrString(ClientId, false);
			char aBanBuf[256];
			str_format(aBanBuf, sizeof(aBanBuf),
				"`%s` [%s] was banned for %d minutes for triggering the Chat-Detection.\n"
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
			char aTitle[32];
			str_format(aTitle, sizeof(aTitle), "[BAN] - Chat Detection (%d)", Server()->Port());
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
	std::vector<int> Times;
	FoundStrings.clear();
	Times.clear();

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
			FoundStrings.push_back(Entry.String());
			Times.push_back(Entry.Time());

			BanDuration = Entry.Time();

			if(str_comp(Entry.Reason(), "") != 0)
				str_copy(Reason, Entry.Reason());
		}
	}

	if(!Times.empty())
		BanDuration = *std::max_element(Times.begin(), Times.end());
	else
		BanDuration = 0;

	if(FoundStrings.size() > 0)
	{
		char InfoMsg[256] = "";
		if(FoundStrings.size() > 0)
		{
			for(const auto &str : FoundStrings)
			{
				str_append(InfoMsg, str.c_str());
				if(&str != &FoundStrings.back())
					str_append(InfoMsg, ", ");
			}
			log_info("name-detection", "Name: %s | Strings Found: %s", ClientName, InfoMsg);
		}

		if(!PreventNameChange && BanDuration > 0)
		{
			const char *pClientAddr = Server()->ClientAddrString(ClientId, false);
			char aBanBuf[256];
			str_format(aBanBuf, sizeof(aBanBuf),
				"`%s` [%s] was banned for %d minutes for triggering the Name-detection.\n"
				"Strings: %s\n"
				"ver: %s (%d) [%s]",
				Server()->ClientName(ClientId),
				Server()->ClientAddrString(ClientId, false),
				BanDuration,
				InfoMsg,
				Server()->GetCustomClient(ClientId),
				GetClientVersion(ClientId),
				GetClientVersionStr(ClientId));
			char aTitle[32];
			str_format(aTitle, sizeof(aTitle), "[BAN] - Name Detection (%d)", Server()->Port());
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
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(g_Config.m_SvAccountsForced && pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		SendMovingTilesInfo(ClientId);
		pPlayer->SetTeam(TEAM_GAME, false);
	}

	pPlayer->m_AccLoginAttempts = 0; // reset login attempts on successful login

	if(pPlayer->Acc()->m_LastName[0] == '\0')
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
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

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

	CDamageIndEffects effect;
	effect.m_Mask = Mask;
	if(Type >= INDTYPE_CLOCKWISE && Type <= INDTYPE_COUNTERWISE)
	{
		AngleOffset = 0.80f;
		effect.m_Remaining = 10;
		for(int Remaining = 0; Remaining < effect.m_Remaining; Remaining++)
		{
			if(Type == INDTYPE_CLOCKWISE)
				effect.m_vAngles.push_back(Angle - AngleOffset + (Remaining * StarDistance));
			else
				effect.m_vAngles.push_back(Angle + AngleOffset - (Remaining * StarDistance));
		}
		effect.m_vPos.push_back(Pos);
		effect.m_Delay = 1;
		effect.m_NextTick = Server()->Tick();
		m_vDamageIndEffects.push_back(effect);
	}
	else if(Type == INDTYPE_INWARD)
	{
		AngleOffset = -0.90f;

		for(int i = 0; i < 2; i++)
		{
			effect.m_Remaining = 5;
			for(int Remaining = 0; Remaining < effect.m_Remaining; Remaining++)
			{
				if(i == 0)
					effect.m_vAngles.push_back(Angle + AngleOffset + (Remaining * StarDistance));
				else
					effect.m_vAngles.push_back(Angle - AngleOffset - (Remaining * StarDistance));
			}
			effect.m_vPos.push_back(Pos);
			effect.m_Delay = 2;
			effect.m_NextTick = Server()->Tick();
			m_vDamageIndEffects.push_back(effect);
		}
	}
	else if(Type == INDTYPE_OUTWARD)
	{
		AngleOffset = 0.20f;

		for(int i = 0; i < 2; i++)
		{
			effect.m_Remaining = 5;
			for(int Remaining = 0; Remaining < effect.m_Remaining; Remaining++)
			{
				if(i == 0)
					effect.m_vAngles.push_back(Angle - AngleOffset - (Remaining * StarDistance));
				else
					effect.m_vAngles.push_back(Angle + AngleOffset + (Remaining * StarDistance));
			}
			effect.m_vPos.push_back(Pos);
			effect.m_Delay = 2;
			effect.m_NextTick = Server()->Tick();
			m_vDamageIndEffects.push_back(effect);
		}
	}
	else if(Type == INDTYPE_LINE)
	{
		effect.m_Remaining = 6;
		for(int Remaining = 0; Remaining < effect.m_Remaining; Remaining++)
		{
			float Offset = Remaining * 15.0f;
			vec2 CalcPos = Pos - Direction * 25.0f + Direction * Offset;
			effect.m_vPos.push_back(CalcPos);
		}
		effect.m_vAngles.push_back(Angle - AngleOffset);

		effect.m_Delay = 1;
		effect.m_NextTick = Server()->Tick();
		m_vDamageIndEffects.push_back(effect);
	}
	else if(Type == INDTYPE_CRISSCROSS)
	{
		effect.m_Remaining = 3;
		for(int Remaining = 0; Remaining < effect.m_Remaining; Remaining++)
		{
			vec2 CalcPos;
			float perpAngle = 0.0f;

			float GetAngle = angle(Direction);
			if(GetAngle < 0.0f)
				GetAngle += 2.0f * pi;

			if(Remaining == 0)
			{
				perpAngle = GetAngle - AngleOffset + pi / 2;
				effect.m_vAngles.push_back(Angle - AngleOffset - 0.85f);
				CalcPos = Pos + vec2(cosf(perpAngle), sinf(perpAngle)) * 25.0f;
			}
			else if(Remaining == 1)
			{
				CalcPos = Pos - Direction * 15.0f;
				effect.m_vAngles.push_back(Angle - AngleOffset);
			}
			else
			{
				perpAngle = GetAngle - AngleOffset - pi / 2;
				effect.m_vAngles.push_back(Angle - AngleOffset + 0.85f);
				CalcPos = Pos + vec2(cosf(perpAngle), sinf(perpAngle)) * 25.0f;
			}

			effect.m_vPos.push_back(CalcPos);
		}

		effect.m_Delay = 1;
		effect.m_NextTick = Server()->Tick();
		m_vDamageIndEffects.push_back(effect);
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

void CGameContext::UnsetTelekinesis(int ClientId)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacter *pChr = GetPlayerChar(i);
		if(pChr && pChr->m_TelekinesisId == ClientId)
		{
			pChr->m_TelekinesisId = -1;
			break; // can break here, every entity can only be picked by one player using telekinesis at the time
		}
	}
}

bool CGameContext::SendFakeTuningParams(int ClientId, const CTuningParams &FakeTuning, bool RealTune)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GetPlayerChar(ClientId))
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
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GetPlayerChar(ClientId))
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
	switch(Weapon)
	{
	case WEAPON_HAMMER:
		return WEAPON_HAMMER;
	case WEAPON_GUN:
		return WEAPON_GUN;
	case WEAPON_SHOTGUN:
		return WEAPON_SHOTGUN;
	case WEAPON_GRENADE:
		return WEAPON_GRENADE;
	case WEAPON_LASER:
		return WEAPON_LASER;
	case WEAPON_NINJA:
		return WEAPON_NINJA;
	case WEAPON_TELEKINESIS:
		return WEAPON_GUN;
	case WEAPON_HEARTGUN:
		return WEAPON_GUN;
	case WEAPON_LIGHTSABER:
		return WEAPON_GUN;
	case WEAPON_PORTALGUN:
		return WEAPON_LASER;
	}
	return Weapon;
}

void CGameContext::SnapDebuggedQuad(int ClientId)
{
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer || !g_Config.m_SvDebugQuadPos)
		return;

	const auto &Quads = Collision()->QuadLayers();
	if(Quads.empty() || m_vQuadDebugIds.empty())
		return;

	const size_t Count = std::min(Quads.size(), m_vQuadDebugIds.size());

	for(size_t i = 0; i < Count; ++i)
	{
		const CQuadData &Quad = Quads[i];
		const vec2 TopLeft = Quad.m_Pos[0];

		if(CNetObj_DDNetLaser *pObj = Server()->SnapNewItem<CNetObj_DDNetLaser>(m_vQuadDebugIds[i]))
		{
			pObj->m_ToX = (int)TopLeft.x;
			pObj->m_ToY = (int)TopLeft.y;
			pObj->m_FromX = (int)TopLeft.x;
			pObj->m_FromY = (int)TopLeft.y;
			pObj->m_StartTick = Server()->Tick();
			pObj->m_Owner = -1;
			pObj->m_Flags = LASERFLAG_NO_PREDICT;
		}
	}
}

void CGameContext::QuadDebugIds(bool Clear)
{
	if(Clear)
	{
		m_vQuadDebugIds.clear();
		const size_t size = Collision()->QuadLayers().size();
		for(size_t i = 0; i < size; i++)
			m_vQuadDebugIds.push_back(Server()->SnapNewId());
	}
	else if(!Clear && !m_vQuadDebugIds.empty())
	{
		if(g_Config.m_SvLogExtra >= 2)
			log_info("quad-debug", "Freeing Ids");

		for(int i = 0; i < (int)m_vQuadDebugIds.size(); i++)
			Server()->SnapFreeId(m_vQuadDebugIds[i]);
		m_vQuadDebugIds.clear();
	}
}

bool CGameContext::AddFakeMessage(const char *pName, const char *pMessage, const char *pSkinName, bool CustomColor, int ColorBody, int ColorFeet)
{
	if(!pName[0] || !pMessage[0])
		return false;

	static int LastUsedId = -1;

	int FreeId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!m_apPlayers[i] && Server()->ClientSlotEmpty(i) && LastUsedId != i)
		{
			FreeId = i;
			break;
		}
	}
	if(FreeId == -1)
		return false; // no free visual slot
	LastUsedId = FreeId;
	CFakeSnapPlayer FakeSnap;
	FakeSnap.m_ClientId = FreeId;
	str_copy(FakeSnap.m_aName, pName);
	FakeSnap.m_aClan[0] = '\0';
	FakeSnap.m_Country = -1;
	str_copy(FakeSnap.m_aSkinName, pSkinName ? pSkinName : "default");
	FakeSnap.m_CustomColors = CustomColor;
	FakeSnap.m_ColorBody = ColorBody;
	FakeSnap.m_ColorFeet = ColorFeet;
	str_copy(FakeSnap.m_aMessage, pMessage);

	m_vFakeSnapPlayers.push_back(FakeSnap);
	return true;
}

const char *GetMapName(const char *pCmd)
{
	const char *pChangeMap = str_find(pCmd, "change_map ");
	if(pChangeMap)
	{
		pChangeMap += str_length("change_map ");
		// Copy until space, semicolon, or end
		static char aMapName[64] = {0};
		int i = 0;
		while(pChangeMap[i] && pChangeMap[i] != ' ' && pChangeMap[i] != ';' && i < (int)sizeof(aMapName) - 1)
		{
			aMapName[i] = pChangeMap[i];
			i++;
		}
		aMapName[i] = 0;
		return aMapName;
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

	std::random_device rd;
	std::uniform_int_distribution<int> dist(0, (int)MapVotes.size() - 1);
	int Random = dist(rd);

	Console()->ExecuteLine(MapVotes[Random], IConsole::CLIENT_ID_UNSPECIFIED);
	return true;
}

void CGameContext::SendMovingTilesInfo(int ClientId)
{
	if(Collision()->HasMovingQuads())
	{
		const char *pWarn = "Turn off entities, this map uses Moving Tiles";
		SendBroadcast(pWarn, ClientId);
		SendChatTarget(ClientId, pWarn);
	}
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
		if(pPlayer->m_IncludeServerInfo != -1)
			Included = pPlayer->m_IncludeServerInfo;
		if(pPlayer->m_Vanish)
			Included = false;
	}

	return Included;
}

void CGameContext::OnPreShutdown()
{
	m_AccountManager.LogoutAllAccountsPort(Server()->Port()); // Save all info before CPlayer is destroyed
}

void CGameContext::OnPreReload()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(!pPlayer)
			continue;
		m_apPersistentData[i] = new CSavePlayerData();
		m_apPersistentData[i]->Save(pPlayer);
	}
}

std::optional<vec2> CGameContext::GetRandomAccessiblePos()
{
	const auto Dist2 = [](const vec2 &a, const vec2 &b) {
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		return dx * dx + dy * dy;
	};

	constexpr float TileSize = 32.0f;
	constexpr float MinPlayerDist = TileSize * 25.0f;

	for(int Tries = 0; Tries < 16; ++Tries)
	{
		vec2 Pos;
		if(!Collision()->TryPickCachedCandidate(Pos))
			return std::nullopt;

		CEntity *apEnts[64] = {0};
		const int num = m_World.FindEntities(Pos, MinPlayerDist, apEnts, std::size(apEnts), CGameWorld::ENTTYPE_CHARACTER);
		bool NearPlayer = false;
		for(int i = 0; i < num; ++i)
		{
			auto *pChr = static_cast<CCharacter *>(apEnts[i]);
			if(pChr && pChr->IsAlive())
			{
				NearPlayer = true;
				break;
			}
		}
		if(NearPlayer)
			continue;

		return Pos;
	}

	float BestScore = -1.0f;
	vec2 BestPos;
	for(int k = 0; k < 32; ++k)
	{
		vec2 Pos;
		if(!Collision()->TryPickCachedCandidate(Pos))
			break;

		float MinDist2 = std::numeric_limits<float>::infinity();
		CEntity *apEnts[128] = {0};
		const int Num = m_World.FindEntities(Pos, 1024.0f, apEnts, std::size(apEnts), CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; ++i)
		{
			auto *pChr = static_cast<CCharacter *>(apEnts[i]);
			if(!pChr || !pChr->IsAlive())
				continue;
			MinDist2 = std::min(MinDist2, Dist2(pChr->m_Pos, Pos));
			if(MinDist2 == 0.0f)
				break;
		}
		if(MinDist2 > BestScore)
		{
			BestScore = MinDist2;
			BestPos = Pos;
		}
	}
	if(BestScore >= 0.0f)
		return BestPos;

	return std::nullopt;
}

void CGameContext::OnCollectPowerup(int ClientId, const CPowerupData *pData) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const bool HidePowerUps = pPlayer->Acc()->m_Configs.m_HidePowerUps;

	if(!pPlayer->Acc()->m_LoggedIn && !HidePowerUps)
	{
		SendChatTarget(ClientId, "You need to be logged in to collect Powerups");
		SendChatTarget(ClientId, "/register <name> <pw> <pw>");
		return;
	}

	char aBuf[128];

	long MsgAmount = (long)(pData->m_Value * pPlayer->StatMultiplier());

	switch(pData->m_Type)
	{
	case EPowerUp::XP:
		pPlayer->GiveXP(pData->m_Value);
		str_format(aBuf, sizeof(aBuf), "+%ldXP for collecting a PowerUp!", MsgAmount);
		break;
	case EPowerUp::MONEY:
		pPlayer->GiveMoney(pData->m_Value);
		str_format(aBuf, sizeof(aBuf), "+%ld%s for collecting a PowerUp!", MsgAmount, g_Config.m_SvCurrencyName);
		break;
	default:
		break;
	}
	if(!HidePowerUps)
		SendChatTarget(ClientId, aBuf);
}

bool CGameContext::IsWeekend() const
{
	using namespace std::chrono;
	auto now = system_clock::now();
	std::time_t t = system_clock::to_time_t(now);
	std::tm lt{};
#if defined(_WIN32)
	const errno_t Err = localtime_s(&lt, &t);
	if(Err != 0)
		return false;
#else
	if(localtime_r(&t, &lt) == nullptr)
		return false;
#endif
	return lt.tm_wday == 5 || lt.tm_wday == 6 || lt.tm_wday == 0;
}

int CGameContext::DirectionToEditorDeg(const vec2 &Dir)
{
	// Protect against zero-length
	if(fabsf(Dir.x) < 1e-6f && fabsf(Dir.y) < 1e-6f)
		return 0;

	float Rad = atan2(Dir.y, Dir.x); // range: [-pi, pi]
	float Deg = Rad * 180.0f / pi; // convert to degrees
	int Ideg = (int)lrintf(Deg); // round to nearest int
	Ideg %= 360;
	if(Ideg < 0)
		Ideg += 360;
	return Ideg; // 0..359
}

int CGameContext::NumPlayersInTeam(int Team) const
{
	CGameTeams &Teams = m_pController->Teams();

	int Count = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(Server()->ClientSlotEmpty(i) || !pPlayer)
			continue;
		if(Teams.m_Core.Team(i) == Team)
			Count++;
	}
	return Count;
}

void CGameContext::OnExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, CClientMask Mask)
{
	// deal damage
	CEntity *apDrops[(int)MAX_CLIENTS * 10];
	float Radius = 135.0f;
	float InnerRadius = 48.0f;
	int NumDrops = m_World.FindEntities(Pos, Radius, apDrops, std::size(apDrops), CGameWorld::ENTTYPE_PICKUPDROP);
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
			Strength = GlobalTuning()->m_ExplosionStrength;
		else
			Strength = TuningList()[m_apPlayers[Owner]->m_TuneZone].m_ExplosionStrength;

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
	CClientMask Mask = pChr->TeamMask();

	// deal damage
	CEntity *apDrops[(int)MAX_CLIENTS * 10];
	int Hits = 0;
	int NumDrops = m_World.FindEntities(StartPos, Radius, apDrops, std::size(apDrops), CGameWorld::ENTTYPE_PICKUPDROP);

	for(int i = 0; i < NumDrops; ++i)
	{
		auto *pPickup = static_cast<CPickupDrop *>(apDrops[i]);
		if(!pPickup)
			continue;

		if(pPickup->Team() != ActivatedTeam && ActivatedTeam != TEAM_SUPER)
			continue;

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
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	if(pPlayer->Cosmetics()->m_ConfettiGun)
		return false;
	if(pPlayer->Cosmetics()->m_DamageIndType != INDTYPE_NONE)
		return false;
	if(pPlayer->Cosmetics()->m_GunType != GUNTYPE_NONE)
		return false;
	if(pPlayer->Cosmetics()->m_PhaseGun)
		return false;
	if(pPlayer->Cosmetics()->m_EmoticonGun)
		return false;

	return true;
}