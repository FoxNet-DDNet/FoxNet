/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "gamecontext.h"

#include <base/log.h>
#include <base/net.h>
#include <base/time.h>

#include <engine/shared/config.h>

static void SendMuteEvent(IServer *pServer, const char *pAction, const NETADDR *pAddr, int Seconds, const char *pReason, const char *pDisplayName, int ActorClientId)
{
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(pAddr, aAddr, sizeof(aAddr), false);
	IServer::CModerationEvent Event;
	Event.m_pAction = pAction;
	Event.m_pAddress = aAddr;
	Event.m_pTargetName = pDisplayName;
	Event.m_DurationSeconds = Seconds;
	Event.m_pReason = pReason;
	Event.m_ActorClientId = ActorClientId;
	pServer->SendModerationEvent(Event);
}

static void SendUnmuteEvent(IServer *pServer, const char *pAction, const std::pair<NETADDR, CMute> &Removed, int ActorClientId)
{
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Removed.first, aAddr, sizeof(aAddr), false);
	IServer::CModerationEvent Event;
	Event.m_pAction = pAction;
	Event.m_pAddress = aAddr;
	Event.m_pTargetName = Removed.second.m_NameKnown ? Removed.second.m_aClientName : nullptr;
	Event.m_pReason = Removed.second.m_aReason;
	Event.m_pReasonLabel = "Original mute reason";
	Event.m_ActorClientId = ActorClientId;
	pServer->SendModerationEvent(Event);
}

static NETADDR KeyAddress(NETADDR Addr)
{
	if(Addr.type == NETTYPE_WEBSOCKET_IPV4)
	{
		Addr.type = NETTYPE_IPV4;
	}
	else if(Addr.type == NETTYPE_WEBSOCKET_IPV6)
	{
		Addr.type = NETTYPE_IPV6;
	}
	Addr.port = 0;
	return Addr;
}

int CMute::SecondsLeft() const
{
	return m_Expire - time_timestamp();
}

CMutes::CMutes(const char *pSystemName) :
	m_pSystemName(pSystemName)
{
}

bool CMutes::Mute(const NETADDR *pAddr, int Seconds, const char *pReason, const char *pClientName, bool InitialDelay)
{
	if(!in_range(Seconds, 1, 365 * 24 * 60 * 60))
	{
		log_info(m_pSystemName, "Invalid mute duration: %d", Seconds);
		return false;
	}

	// Find a matching mute for this IP, update expiration time if found
	const int64_t Expire = time_timestamp() + Seconds;
	CMute &Mute = m_Mutes[KeyAddress(*pAddr)];
	if(!Mute.m_Initialized)
	{
		Mute.m_Initialized = true;
		Mute.m_Expire = Expire;
		str_copy(Mute.m_aReason, pReason);
		Mute.m_InitialDelay = InitialDelay;
		if(pClientName)
		{
			str_copy(Mute.m_aClientName, pClientName);
			Mute.m_NameKnown = true;
		}
		else
		{
			Mute.m_aClientName[0] = '\0';
			Mute.m_NameKnown = false;
		}

		return true;
	}
	if(Expire > Mute.m_Expire)
	{
		Mute.m_Expire = Expire;
		str_copy(Mute.m_aReason, pReason);
		Mute.m_InitialDelay = InitialDelay;
	}
	return true;
}

std::optional<std::pair<NETADDR, CMute>> CMutes::UnmuteIndex(int Index)
{
	if(Index < 0 || Index >= (int)m_Mutes.size())
	{
		log_info(m_pSystemName, "Invalid index to unmute: %d", Index);
		return std::nullopt;
	}
	auto It = std::next(m_Mutes.begin(), Index);
	char aAddrString[NETADDR_MAXSTRSIZE];
	net_addr_str(&It->first, aAddrString, sizeof(aAddrString), false);
	if(It->second.m_NameKnown)
		log_info(m_pSystemName, "Unmuted: '%s' (<{%s}>)", It->second.m_aClientName, aAddrString);
	else
		log_info(m_pSystemName, "Unmuted: (unknown) (<{%s}>)", aAddrString);
	const std::pair<NETADDR, CMute> Removed = *It;
	m_Mutes.erase(It);
	return Removed;
}

std::optional<std::pair<NETADDR, CMute>> CMutes::UnmuteAddr(const NETADDR *pAddr)
{
	NETADDR KeyAddr = KeyAddress(*pAddr);
	char aAddrString[NETADDR_MAXSTRSIZE];
	net_addr_str(&KeyAddr, aAddrString, sizeof(aAddrString), false);
	auto It = m_Mutes.find(KeyAddr);
	if(It == m_Mutes.end())
	{
		log_info(m_pSystemName, "No mutes for this IP address found: %s", aAddrString);
		return std::nullopt;
	}
	if(It->second.m_NameKnown)
		log_info(m_pSystemName, "Unmuted: '%s' (<{%s}>)", It->second.m_aClientName, aAddrString);
	else
		log_info(m_pSystemName, "Unmuted: (unknown) (<{%s}>)", aAddrString);
	const std::pair<NETADDR, CMute> Removed = *It;
	m_Mutes.erase(It);
	return Removed;
}

std::optional<CMute> CMutes::IsMuted(const NETADDR *pAddr, bool RespectInitialDelay) const
{
	const auto It = m_Mutes.find(KeyAddress(*pAddr));
	if(It == m_Mutes.end())
	{
		return std::nullopt;
	}
	if(!RespectInitialDelay && It->second.m_InitialDelay)
	{
		return std::nullopt;
	}
	return It->second;
}

void CMutes::UnmuteExpired()
{
	const int64_t Now = time_timestamp();
	for(auto It = m_Mutes.begin(); It != m_Mutes.end();)
	{
		if(It->second.m_Expire <= Now)
		{
			It = m_Mutes.erase(It);
		}
		else
		{
			++It;
		}
	}
}

void CMutes::Print(int Page) const
{
	if(m_Mutes.empty())
	{
		log_info(m_pSystemName, "The mute list is empty.");
		return;
	}

	static constexpr int ENTRIES_PER_PAGE = 20;
	const int NumPages = std::ceil(m_Mutes.size() / (float)ENTRIES_PER_PAGE);
	if(!in_range(Page, 1, NumPages))
	{
		log_info(m_pSystemName, "Invalid page number. There %s %d %s available.",
			NumPages == 1 ? "is" : "are", NumPages, NumPages == 1 ? "page" : "pages");
		return;
	}

	const int Start = (Page - 1) * ENTRIES_PER_PAGE;
	const int End = Page * ENTRIES_PER_PAGE;

	int Count = 0;
	for(const auto &[Addr, MuteEntry] : m_Mutes)
	{
		if(Count < Start)
		{
			Count++;
			continue;
		}
		else if(Count >= End)
		{
			break;
		}

		char aAddrString[NETADDR_MAXSTRSIZE];
		net_addr_str(&Addr, aAddrString, sizeof(aAddrString), false);

		if(MuteEntry.m_NameKnown)
		{
			log_info(
				m_pSystemName,
				"#%d '%s' (<{%s}>) muted for %d seconds (%s)",
				Count,
				MuteEntry.m_aClientName,
				aAddrString,
				MuteEntry.SecondsLeft(),
				MuteEntry.m_aReason[0] == '\0' ? "No reason given" : MuteEntry.m_aReason);
		}
		else
		{
			log_info(
				m_pSystemName,
				"#%d (unknown) (<{%s}>) muted for %d seconds (%s)",
				Count,
				aAddrString,
				MuteEntry.SecondsLeft(),
				MuteEntry.m_aReason[0] == '\0' ? "No reason given" : MuteEntry.m_aReason);
		}

		Count++;
	}
	log_info(m_pSystemName, "%d %s, showing entries %d - %d (page %d/%d)",
		(int)m_Mutes.size(), m_Mutes.size() == 1 ? "mute" : "mutes", Start, Count - 1, Page, NumPages);
}

void CGameContext::MuteWithMessage(const NETADDR *pAddr, int Seconds, const char *pReason, const char *pDisplayName, int ActorClientId)
{
	if(!m_Mutes.Mute(pAddr, Seconds, pReason, pDisplayName, false))
	{
		return;
	}
	SendMuteEvent(Server(), "Chat mute", pAddr, Seconds, pReason, pDisplayName, ActorClientId);

	if(g_Config.m_SvScriptPlayerMutes[0])
	{
		char aAddrString[NETADDR_MAXSTRSIZE];
		net_addr_str(pAddr, aAddrString, sizeof(aAddrString), false);

		char aScriptArgs[128];
		str_format(aScriptArgs, sizeof(aScriptArgs), "%s %d \"%s\"", aAddrString, Seconds, pReason[0] != '\0' ? pReason : "No reason given");

		char aScriptingBuf[256];
		str_format(aScriptingBuf, sizeof(aScriptingBuf), "chai %s %s", g_Config.m_SvScriptPlayerMutes, aScriptArgs);
		Console()->ExecuteLine(aScriptingBuf, IConsole::CLIENT_ID_UNSPECIFIED, false);
	}

	char aChatMessage[256];
	if(pReason[0] != '\0')
	{
		str_format(aChatMessage, sizeof(aChatMessage), "'%s' has been muted for %d seconds (%s)", pDisplayName, Seconds, pReason);
	}
	else
	{
		str_format(aChatMessage, sizeof(aChatMessage), "'%s' has been muted for %d seconds", pDisplayName, Seconds);
	}
	SendChat(-1, TEAM_ALL, aChatMessage);
}

void CGameContext::VoteMuteWithMessage(const NETADDR *pAddr, int Seconds, const char *pReason, const char *pDisplayName, int ActorClientId)
{
	if(!m_VoteMutes.Mute(pAddr, Seconds, pReason, pDisplayName, false))
	{
		return;
	}
	SendMuteEvent(Server(), "Vote mute", pAddr, Seconds, pReason, pDisplayName, ActorClientId);

	if(g_Config.m_SvScriptPlayerMutes[0])
	{
		char aAddrString[NETADDR_MAXSTRSIZE];
		net_addr_str(pAddr, aAddrString, sizeof(aAddrString), false);

		char aScriptArgs[128];
		str_format(aScriptArgs, sizeof(aScriptArgs), "%s %d \"%s\"", aAddrString, Seconds, pReason[0] != '\0' ? pReason : "No reason given");

		char aScriptingBuf[256];
		str_format(aScriptingBuf, sizeof(aScriptingBuf), "chai %s %s", g_Config.m_SvScriptPlayerMutes, aScriptArgs);
		Console()->ExecuteLine(aScriptingBuf, IConsole::CLIENT_ID_UNSPECIFIED, false);
	}

	char aChatMessage[256];
	if(pReason[0] != '\0')
	{
		str_format(aChatMessage, sizeof(aChatMessage), "'%s' has been banned from voting for %d seconds (%s)", pDisplayName, Seconds, pReason);
	}
	else
	{
		str_format(aChatMessage, sizeof(aChatMessage), "'%s' has been banned from voting for %d seconds", pDisplayName, Seconds);
	}
	SendChat(-1, TEAM_ALL, aChatMessage);
}

void CGameContext::ConMute(IConsole::IResult *pResult, void *pUserData)
{
	log_info("mutes", "This command is deprecated. Use either 'muteid <client_id> <seconds> <reason>' or 'muteip <ip> <seconds> <reason>'");
}

void CGameContext::ConMuteId(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const int Victim = pResult->GetVictim(0);
	if(!CheckClientId(Victim) || !pSelf->m_apPlayers[Victim])
	{
		log_info("mutes", "Client ID not found: %d", Victim);
		return;
	}

	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "";
	pSelf->MuteWithMessage(pSelf->Server()->ClientAddr(Victim), pResult->GetInteger(1), pReason, pSelf->Server()->ClientName(Victim), pResult->m_ClientId);
}

void CGameContext::ConMuteIp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pResult->GetString(0)) != 0)
	{
		log_info("mutes", "Invalid IP address to mute: %s", pResult->GetString(0));
		return;
	}

	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "";

	if(pSelf->m_Mutes.Mute(&Addr, pResult->GetInteger(1), pReason, nullptr, false))
		SendMuteEvent(pSelf->Server(), "Chat mute", &Addr, pResult->GetInteger(1), pReason, nullptr, pResult->m_ClientId);
}

void CGameContext::ConUnmute(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(const auto Removed = pSelf->m_Mutes.UnmuteIndex(pResult->GetInteger(0)))
		SendUnmuteEvent(pSelf->Server(), "Chat unmute", *Removed, pResult->m_ClientId);
}

void CGameContext::ConUnmuteId(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const int Victim = pResult->GetVictim(0);
	if(!CheckClientId(Victim) || !pSelf->m_apPlayers[Victim])
	{
		log_info("mutes", "Client ID not found: %d", Victim);
		return;
	}

	if(const auto Removed = pSelf->m_Mutes.UnmuteAddr(pSelf->Server()->ClientAddr(Victim)))
		SendUnmuteEvent(pSelf->Server(), "Chat unmute", *Removed, pResult->m_ClientId);
}

void CGameContext::ConUnmuteIp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pResult->GetString(0)) != 0)
	{
		log_info("mutes", "Invalid IP address to unmute: %s", pResult->GetString(0));
		return;
	}

	if(const auto Removed = pSelf->m_Mutes.UnmuteAddr(&Addr))
		SendUnmuteEvent(pSelf->Server(), "Chat unmute", *Removed, pResult->m_ClientId);
}

void CGameContext::ConMutes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->m_Mutes.Print(pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 1);
}

void CGameContext::ConVoteMute(IConsole::IResult *pResult, void *pUserData)
{
	log_info("votemutes", "Use either 'vote_muteid <client_id> <seconds> <reason>' or 'vote_muteip <ip> <seconds> <reason>'");
}

void CGameContext::ConVoteMuteId(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const int Victim = pResult->GetVictim(0);
	if(!CheckClientId(Victim) || !pSelf->m_apPlayers[Victim])
	{
		log_info("votemutes", "Client ID not found: %d", Victim);
		return;
	}

	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "";
	pSelf->VoteMuteWithMessage(pSelf->Server()->ClientAddr(Victim), pResult->GetInteger(1), pReason, pSelf->Server()->ClientName(Victim), pResult->m_ClientId);
}

void CGameContext::ConVoteMuteIp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pResult->GetString(0)) != 0)
	{
		log_info("votemutes", "Invalid IP address to mute: %s", pResult->GetString(0));
		return;
	}

	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "";

	if(pSelf->m_VoteMutes.Mute(&Addr, pResult->GetInteger(1), pReason, nullptr, false))
		SendMuteEvent(pSelf->Server(), "Vote mute", &Addr, pResult->GetInteger(1), pReason, nullptr, pResult->m_ClientId);
}

void CGameContext::ConVoteUnmute(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(const auto Removed = pSelf->m_VoteMutes.UnmuteIndex(pResult->GetInteger(0)))
		SendUnmuteEvent(pSelf->Server(), "Vote unmute", *Removed, pResult->m_ClientId);
}

void CGameContext::ConVoteUnmuteId(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const int Victim = pResult->GetVictim(0);
	if(!CheckClientId(Victim) || !pSelf->m_apPlayers[Victim])
	{
		log_info("votemutes", "Client ID not found: %d", Victim);
		return;
	}

	if(const auto Removed = pSelf->m_VoteMutes.UnmuteAddr(pSelf->Server()->ClientAddr(Victim)))
		SendUnmuteEvent(pSelf->Server(), "Vote unmute", *Removed, pResult->m_ClientId);
}

void CGameContext::ConVoteUnmuteIp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pResult->GetString(0)) != 0)
	{
		log_info("votemutes", "Invalid IP address to unmute: %s", pResult->GetString(0));
		return;
	}

	if(const auto Removed = pSelf->m_VoteMutes.UnmuteAddr(&Addr))
		SendUnmuteEvent(pSelf->Server(), "Vote unmute", *Removed, pResult->m_ClientId);
}

void CGameContext::ConVoteMutes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->m_VoteMutes.Print(pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 1);
}
