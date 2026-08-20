#include "hidenseek.h"

#include <base/log.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/foxnet/entities/hidenseek_projectile.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <vector>

// ToDo @qxdFox:
// ~~Dont allow weapon drops~~
// ~~give players gun and hammer~~ when hide and seek starts and reset to their old state after

// Add foxnet_accounts_stats to save hide and seek stats and future gamemode data?
// playtest with multiple people
// ~~^ Seekers can see "ghost" skin in scoreboard when hider goes ghost~~
// ~~^ new joining players arent dead if game is running~~

// Feedback:
// add infection
// allow hiders to pass trough eachother?
// ~~pistol should freeze if it touches player~~

// ~~if seeker doesnt move for too long, choose new one (or end game if its a 1v1)~~

constexpr static float MaxAfkSeconds = 30.0f; // seconds
constexpr static int FinishedDelaySeconds = 5; // how long the result screen holds everyone in place

// The minigame uses this many consecutive tune zones, starting at sv_hide_seek_tune_zone
constexpr static int ZoneOffsetSeeker = 0;
constexpr static int ZoneOffsetHider = 1;
constexpr static int ZoneOffsetGhost = 2;
constexpr static int ZoneOffsetFrozen = 3;
constexpr static int NumTuneZones = 4;

static int TimeToTicks(int TenthsOfSeconds, int TickSpeed)
{
	return TenthsOfSeconds * TickSpeed / 10;
}

static std::string MakeHudBar(const char *pLabel, int Current, int Maximum, int Width = 12)
{
	Maximum = std::max(Maximum, 1);
	Current = std::clamp(Current, 0, Maximum);

	const int Filled = std::clamp(Current * Width / Maximum, 0, Width);

	std::string Bar;
	Bar.reserve(str_length(pLabel) + Width + 8);
	Bar.append(pLabel);
	Bar.append(": [");
	Bar.append(Filled, ':');
	Bar.append(Width - Filled, ' ');
	Bar.append("]");

	return Bar;
}

void CHideAndSeekZone::OnTick()
{
	UpdateCandidates();

	const int MaxClients = Server()->MaxClients();
	for(int ClientId = 0; ClientId < MaxClients; ClientId++)
		ClientTick(ClientId);

	// ClientTick lets players enter and leave the area, refresh the list before running the game
	int NumPlayers = UpdateCandidates();

	if(m_State == EState::BadMap)
	{
		for(int ClientId : m_vCandidateIds)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(pPlayer)
				pPlayer->SendBroadcast("Hide and Seek cannot be started on this map.");
		}
		return;
	}

	int NumSeekers = 0;
	for(int ClientId : m_vCandidateIds)
	{
		if(m_aClientData[ClientId].m_IsSeeker)
			NumSeekers++;
	}

	if(NumPlayers < 2 && m_State != EState::WaitingForPlayers)
	{
		EndGame(EWinState::None);
		ReleasePlayers();
		m_State = EState::WaitingForPlayers;
	}

	if(m_State == EState::WaitingForPlayers)
	{
		if(NumPlayers < 2)
		{
			for(int ClientId : m_vCandidateIds)
			{
				CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
				if(pPlayer)
					pPlayer->SendBroadcast("Not enough players to start hide and seek.");
			}
			return;
		}
		else
		{
			m_State = EState::Warmup;
		}
	}
	else if(m_State == EState::Warmup)
	{
		m_WarmUpTime = Server()->Tick() + Server()->TickSpeed() * g_Config.m_SvHideSeekWarmupTime;
		m_State = EState::DoWarmup;
	}
	else if(m_State == EState::DoWarmup)
	{
		if(Server()->Tick() < m_WarmUpTime)
		{
			for(int ClientId : m_vCandidateIds)
			{
				CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
				if(!pPlayer)
					continue;

				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "Hide and Seek starts in %d seconds", (int)((m_WarmUpTime - Server()->Tick()) / Server()->TickSpeed()));
				pPlayer->SendBroadcast(aBuf);
			}
		}
		else
		{
			m_State = EState::Playing;
			StartGame();
		}
	}
	else if(m_State == EState::Playing)
	{
		if(NumSeekers == 0)
		{
			EndGame(EWinState::None);
			ReleasePlayers();
			m_State = EState::WaitingForPlayers;
			return;
		}

		if(m_SeekTimeRemaining > 0)
		{
			m_SeekTimeRemaining--;
		}
		else
		{
			EndGame(EWinState::Hiders); // Seek time is over, end the game
			m_State = EState::Finished;
			m_FinishedDelay = Server()->Tick() + Server()->TickSpeed() * FinishedDelaySeconds;
			HoldPlayers();
			return; // Don't check for win conditions anymore
		}

		int NumHiders = 0;
		for(int ClientId : m_vCandidateIds)
		{
			if(!m_aClientData[ClientId].m_IsSeeker && m_aClientData[ClientId].m_Alive)
				NumHiders++;
		}
		if(NumHiders == 0)
		{
			EndGame(EWinState::Seeker);
			m_State = EState::Finished;
			m_FinishedDelay = Server()->Tick() + Server()->TickSpeed() * FinishedDelaySeconds;
			HoldPlayers();
		}
	}
	else if(m_State == EState::Finished)
	{
		if(Server()->Tick() > m_FinishedDelay)
		{
			ReleasePlayers();
			m_State = EState::WaitingForPlayers;
			for(int ClientId : m_vCandidateIds)
			{
				CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
				if(!pChr)
					continue;

				vec2 SpawnPos = GetRandomSpawnPos();
				pChr->ForceSetPos(SpawnPos);
			}
		}
	}
}

void CHideAndSeekZone::OnClientDrop(int ClientId, const char *pReason)
{
	CClientData &Data = m_aClientData[ClientId];
	Data.Reset();
	Data.m_MarkedAfk = false;
	Data.m_NumWins = 0; // Reset wins, wins should get saved in foxnet_accounts_stats when implemented

	// The player is gone, don't keep them around until the list gets rebuilt
	m_vCandidateIds.erase(std::remove(m_vCandidateIds.begin(), m_vCandidateIds.end(), ClientId), m_vCandidateIds.end());
}

void CHideAndSeekZone::OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx)
{
	if(!IsInArea(ClientId))
		return;

	pGameInfoEx->m_Flags &= ~GAMEINFOFLAG_TIMESCORE;

	if(m_State == EState::Playing)
	{
		if(m_aClientData[ClientId].m_Alive)
			pGameInfoEx->m_Flags &= ~GAMEINFOFLAG_ALLOW_ZOOM;

		if(m_aClientData[ClientId].m_IsSeeker || !m_aClientData[ClientId].m_Alive)
		{
			pGameInfoObj->m_TimeLimit = m_GameInfoTimeLimit;
			pGameInfoObj->m_RoundStartTick = m_GameInfoRoundStartTick;
			pGameInfoObj->m_WarmupTimer = 0;
			pGameInfoObj->m_GameStateFlags &= ~GAMESTATEFLAG_RACETIME;
		}
	}
	else if(m_State == EState::Finished && !m_aClientData[ClientId].m_Alive)
	{
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	}
}

void CHideAndSeekZone::ClientTick(int ClientId)
{
	const int MapIdx = (int)MultiMapIndex();

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer || !pPlayer->GetCharacter())
		return;
	if(pPlayer->MultiMapIdx() != MapIdx)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr->IsAlive())
		return;

	// Nothing below here may touch players that arent inside the area, CZoneManager owns that decision
	if(!IsInArea(ClientId))
		return;

	CClientData &Data = m_aClientData[ClientId];

	Data.m_InHiddenZone = false;
	for(const CQuadData &QuadData : Quads())
	{
		if(QuadData.m_SubType != (uint8_t)ESubType::Hidden)
			continue;

		if(InsideQuad(pChr->GetPos(), QuadData, vec2(0, 0)))
		{
			Data.m_InHiddenZone = true;
			break;
		}
	}

	// Only their own input counts as being active, getting pushed around must not clear the afk mark
	if(!pPlayer->IsAfk() && pChr->m_Pos != pChr->m_PrevPos)
	{
		Data.m_LastMovement = Server()->Tick();
		Data.m_MarkedAfk = false;
	}

	// Afk players and players that arent part of the running round are put into solo, otherwise
	// anyone could drag them around, which would also count as them being active again
	const bool KeepOutOfTheWay = pPlayer->IsAfk() || Data.m_MarkedAfk || (m_State == EState::Playing && !Data.m_Alive);
	SetForcedSolo(ClientId, KeepOutOfTheWay);

	if(!Data.m_Alive && (m_State == EState::Playing || m_State == EState::Finished))
	{
		if(pChr->GetWeaponGot(WEAPON_HAMMER) || pChr->GetWeaponGot(WEAPON_HAMMER))
		{
			pChr->GiveWeapon(WEAPON_HAMMER, true);
			pChr->GiveWeapon(WEAPON_GUN, true);
			pChr->SetActiveWeapon(WEAPON_NONE);
		}
	}

	if(m_State == EState::Playing)
	{
		// Afk players are out, a seeker gets replaced instead so the round doesnt just end
		const bool NoMovement = Server()->Tick() - Data.m_LastMovement > MaxAfkSeconds * Server()->TickSpeed();
		if(Data.m_Alive && Data.m_IsSeeker && (pPlayer->IsAfk() || NoMovement))
		{
			TryReplaceAfkSeeker(ClientId);
			return;
		}

		// Players that go afk mid round are out, players that walked in late never were part of it
		if(Data.m_Alive && pPlayer->IsAfk())
			SetDead(ClientId);

		if(!Data.m_Alive)
			return;

		std::vector<std::string> Messages;
		std::string Msg = "";

		if(!Data.m_IsSeeker)
		{
			// If the player is currently a ghost, we don't want to decrease the cooldown until the timeout is over
			if(Data.m_GhostCooldown > 0 && Data.m_GhostDuration <= 0)
				Data.m_GhostCooldown--;

			// Only counts while the hider actually is hidden from the seekers, this is what the xp reward is based on
			if(Data.m_InHiddenZone || Data.m_GhostDuration > 0)
				Data.m_NumHiddenTicks++;

			if(Data.m_GhostDuration > 0)
			{
				Data.m_GhostDuration--;
				if(Data.m_GhostDuration == 0)
					pChr->SetTuneOverride(m_HiderTuneZone);
			}
			else
			{
				Data.m_LastKnownPos = pChr->GetPos();
			}

			const int GhostCooldownTicks = TimeToTicks(g_Config.m_SvHideSeekHidersGhostCooldown, Server()->TickSpeed());
			const int GhostDurationTicks = TimeToTicks(g_Config.m_SvHideSeekHidersGhostDuration, Server()->TickSpeed());

			if(Data.m_GhostDuration > 0)
			{
				Messages.push_back(MakeHudBar("Ghost", Data.m_GhostDuration, GhostDurationTicks));
			}
			else
			{
				const int GhostCharge = GhostCooldownTicks - Data.m_GhostCooldown;
				Msg = MakeHudBar("Ghost", GhostCharge, GhostCooldownTicks);
				//if(Data.m_GhostCooldown <= 0)
				//	Msg += " ✓";
				Messages.push_back(Msg);
			}
		}
		else
		{
			if(Data.m_GunReloadTimer > 0)
				Data.m_GunReloadTimer--;

			const int GunCooldownTicks = g_Config.m_SvHideSeekSeekersGunCooldown * Server()->TickSpeed() / 1000;

			if(Data.m_GunReloadTimer > 0)
			{
				const int ReloadCharge = GunCooldownTicks - Data.m_GunReloadTimer;
				Messages.push_back(MakeHudBar("Reload", ReloadCharge, GunCooldownTicks));
			}
			else
			{
				Msg = MakeHudBar("Reload", GunCooldownTicks, GunCooldownTicks);
				Messages.push_back(Msg);
			}
		}

		if(!Messages.empty())
			pPlayer->SendBroadcastHud(Messages, 0);
	}
}

bool CHideAndSeekZone::ContainsPlayer(const CPlayer *pPlayer) const
{
	const CCharacter *pChr = pPlayer->GetCharacter();
	// Dying takes the player out of the area and the round, unlike the default which keeps them
	if(!pChr || !pChr->IsAlive())
		return false;

	return IMinigame::ContainsPlayer(pPlayer);
}

void CHideAndSeekZone::OnPlayerEnter(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!g_Config.m_SvMinigamesSameIp && HasSameIpInArea(ClientId))
	{
		pPlayer->SendChat("A player with the same ip is already in that zone.");
		// The dead character fails ContainsPlayer(), so the next membership pass hands them back
		pPlayer->KillCharacter();
		return;
	}

	// Walking in never carries state of an older round along, and never joins a running one
	CClientData &Data = m_aClientData[ClientId];
	Data.Reset();
	Data.m_LastMovement = Server()->Tick();
	Data.m_ActiveInRound = false; // Just entered, m_ActiveInRound = true when StartGame()

	CCharacter *pChr = pPlayer->GetCharacter();

	if(pChr)
	{
		pChr->SetTuneOverride(-1);
	}
}

void CHideAndSeekZone::OnPlayerLeave(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	pPlayer->ClearBroadcast();

	ReleaseHold(ClientId);

	CCharacter *pChr = pPlayer->GetCharacter();
	if(pChr)
	{
		pChr->SetTuneOverride(-1);

		pChr->GiveWeapon(WEAPON_HAMMER);
		pChr->GiveWeapon(WEAPON_GUN);
		pChr->SetActiveWeapon(WEAPON_HAMMER);
	}

	SetForcedSolo(ClientId, false);
	m_aClientData[ClientId].Reset(); // has to happen after the solo release, Reset() drops the ownership flag
}

bool CHideAndSeekZone::HasSameIpInArea(int ClientId) const
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == ClientId)
			continue;
		CPlayer *pOtherPlayer = GameServer()->m_apPlayers[i];
		if(!pOtherPlayer || !pOtherPlayer->GetCharacter())
			continue;
		if(pOtherPlayer->MultiMapIdx() != (int)MultiMapIndex())
			continue;
		if(!IsInArea(i))
			continue;

		// compare addresses
		if(net_addr_comp_noport(Server()->ClientAddr(ClientId), Server()->ClientAddr(i)) == 0)
			return true;
	}
	return false;
}

const char *CHideAndSeekZone::Motd() const
{
	return "[Viewable in Server info tab]\n"
	       "\n"
	       "\n"
	       "--  Hɪᴅᴇ ᴀɴᴅ Sᴇᴇᴋ  --\n"
	       "\n"
	       "Sᴇᴇᴋᴇʀ:\n"
	       "Find all seekers and hammer them, shooting your gun will point to the closest hidden player.\n"
	       "\n"
	       "Hɪᴅᴇʀ:\n"
	       "Dark Areas completely hide you from the seeker, hammering will put you in ghost mode for a short time which allows you to run away\n"
	       "\n"
	       "Depending on the map, entities will or will not work\n"
	       "\n"
	       "[Press Tab to hide]";
}

void CHideAndSeekZone::StartGame()
{
	if(m_vSpawnQuads.empty() || !InitTuning())
	{
		m_State = EState::BadMap;
		return;
	}

	// Nobody carries the flag in from an earlier round: a player who stopped being a candidate before
	// the last EndGame never reached its Data.Reset(), so only the players set up below are in this one
	for(CClientData &ClientData : m_aClientData)
		ClientData.m_ActiveInRound = false;

	for(int ClientId : m_vCandidateIds)
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(!pChr)
			continue;

		m_aClientData[ClientId].m_ActiveInRound = true;
		m_aClientData[ClientId].m_Alive = true;
		m_aClientData[ClientId].m_IsSeeker = false;
		m_aClientData[ClientId].m_LastMovement = Server()->Tick();
		pChr->GetPlayer()->ClearBroadcast();
		pChr->GetPlayer()->Pause(CPlayer::PAUSE_NONE, true);
		SetForcedSolo(ClientId, false);
		pChr->SetSolo(false);
		pChr->GiveWeapon(WEAPON_GUN);
		pChr->GiveWeapon(WEAPON_HAMMER);
		pChr->SetActiveWeapon(WEAPON_HAMMER);
	}

	const int NumCandidates = (int)m_vCandidateIds.size();
	const int DesiredSeekers = std::min(NumCandidates, std::max(1, (NumCandidates + 3) / 4));

	std::vector<int> vCandidateIds = m_vCandidateIds;

	std::shuffle(vCandidateIds.begin(), vCandidateIds.end(), Rng());

	for(int i = 0; i < DesiredSeekers; i++)
		m_aClientData[vCandidateIds[i]].m_IsSeeker = true;

	const int NumSeekers = DesiredSeekers;
	const int NumHiders = NumCandidates - NumSeekers;

	// move players to spawn points
	for(int ClientId : m_vCandidateIds)
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(!pChr)
			continue;

		CPlayer *pPlayer = pChr->GetPlayer();

		vec2 SpawnPos = GetRandomSpawnPos();
		pChr->ForceSetPos(SpawnPos);
		pChr->SetVelocity(vec2(0, 0));
		pChr->ResetHook();

		if(m_aClientData[ClientId].m_IsSeeker)
		{
			pChr->FreezeForce(g_Config.m_SvHideSeekFreezeDuration * Server()->TickSpeed());
			pChr->SetTuneOverride(m_SeekerTuneZone);
			pPlayer->SendChat("You are a Seeker");
		}
		else
		{
			pChr->SetTuneOverride(m_HiderTuneZone);
			pPlayer->SendChat("You are a Hider");
		}
	}

	// Set Seeker Time based on number of players
	int BaseTime = (g_Config.m_SvHideSeekSeekersTime + g_Config.m_SvHideSeekFreezeDuration);

	int Ticks = (BaseTime + ((NumHiders / NumSeekers) * 5)) * Server()->TickSpeed();
	m_SeekTimeTotal = Ticks;
	m_SeekTimeRemaining = Ticks;

	UpdateGameInfoTimer();
}

void CHideAndSeekZone::UpdateGameInfoTimer()
{
	// The client counts down from m_TimeLimit (in minutes) since m_RoundStartTick, so the start tick
	// gets backdated to the point where that countdown lines up with the seek time that is left.
	// Only called when the seek time jumps, ticking it down on its own keeps both in sync.
	const int TotalSeconds = std::max(0, m_SeekTimeTotal / Server()->TickSpeed());
	const int RemainingSeconds = std::max(0, m_SeekTimeRemaining / Server()->TickSpeed());

	m_GameInfoTimeLimit = std::max(1, (std::max(TotalSeconds, RemainingSeconds) + 59) / 60);

	const int ElapsedSeconds = m_GameInfoTimeLimit * 60 - RemainingSeconds;
	m_GameInfoRoundStartTick = Server()->Tick() - ElapsedSeconds * Server()->TickSpeed();
}

void CHideAndSeekZone::EndGame(EWinState WinState)
{
	int NumSeekers = 0;
	int NumHiders = 0;
	char aHiderName[MAX_NAME_LENGTH] = "";
	char aSeekerName[MAX_NAME_LENGTH] = "";
	for(int ClientId : m_vCandidateIds)
	{
		if(!m_aClientData[ClientId].m_ActiveInRound)
			continue;

		if(m_aClientData[ClientId].m_IsSeeker)
		{
			NumSeekers++;
			str_copy(aSeekerName, Server()->ClientName(ClientId));
		}
		else
		{
			NumHiders++;
			str_copy(aHiderName, Server()->ClientName(ClientId));
		}
	}

	for(int ClientId : m_vCandidateIds)
	{
		CClientData &Data = m_aClientData[ClientId];

		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;

		if(WinState == EWinState::None)
		{
			pPlayer->SendChat("Game ended prematurely");
		}
		else if(WinState == EWinState::Hiders)
		{
			if(NumHiders != 1)
				pPlayer->SendChat("The Hiders won the game!");
			else
				pPlayer->SendChatFmt("'%s' won the game!", aHiderName);

			// Xp for hiders gets given based on how long they werent hidden for, and only for the
			// ones that were part of this round. Walking in mid round still gets the cleanup below.
			if(Data.m_ActiveInRound && !Data.m_IsSeeker)
			{
				if(g_Config.m_SvHideSeekGiveXp)
				{
					int NumSeekingSeconds = m_SeekTimeTotal / (float)Server()->TickSpeed();
					int HiddenSeconds = Data.m_NumHiddenTicks / (float)Server()->TickSpeed();

					int a = NumSeekingSeconds / std::max(HiddenSeconds, 1);
					int b = std::clamp(a, 1, 10);

					pPlayer->GiveXP(5 * b, "", false);
				}
				Data.m_NumWins++;
			}
		}
		else
		{
			if(NumSeekers != 1)
				pPlayer->SendChat("The Seekers won the game!");
			else
				pPlayer->SendChatFmt("'%s' won the game!", aSeekerName);

			if(Data.m_ActiveInRound && Data.m_IsSeeker)
			{
				if(g_Config.m_SvHideSeekGiveXp)
				{
					if(m_SeekTimeRemaining > m_SeekTimeTotal * 0.5f)
						pPlayer->GiveXP(5 + Data.m_NumKills * 7, "", false);
				}
				Data.m_NumWins++;
			}
		}

		CCharacter *pChr = pPlayer->GetCharacter();
		if(pChr)
		{
			pChr->SetTuneOverride(-1);
			pChr->Unfreeze();
		}
		SetForcedSolo(ClientId, false);
		pPlayer->ClearBroadcast();
		Data.Reset();
	}

	m_GameInfoTimeLimit = 0;
	m_GameInfoRoundStartTick = 0;

	// The tune zones stay reserved, the end of round hold still needs the frozen one
}

// The one place the tune zones are described. It feeds both the servers tuning list and the
// settings that get written into the map, so the two can never drift apart.
struct STuneZoneSetting
{
	int m_ZoneOffset;
	const char *m_pParam;
	float m_Value;
};

static void BuildTuneZoneTable(std::vector<STuneZoneSetting> &vSettings)
{
	// Seekers hammer slower than normal
	vSettings.push_back({ZoneOffsetSeeker, "hammer_fire_delay", (float)g_Config.m_SvHideSeekSeekersHammerDelay});
	vSettings.push_back({ZoneOffsetSeeker, "hammer_hit_fire_delay", (float)(g_Config.m_SvHideSeekSeekersHammerDelay + 150)});

	// Hiders cant grab or hammer each other
	vSettings.push_back({ZoneOffsetHider, "player_hooking", 0.0f});
	vSettings.push_back({ZoneOffsetHider, "player_hammering", 0.0f});

	// A ghost passes through everyone
	vSettings.push_back({ZoneOffsetGhost, "player_hooking", 0.0f});
	vSettings.push_back({ZoneOffsetGhost, "player_hammering", 0.0f});
	vSettings.push_back({ZoneOffsetGhost, "player_collision", 0.0f});

	// Nothing may move a player of a finished round, not even gravity
	vSettings.push_back({ZoneOffsetFrozen, "gravity", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "ground_control_speed", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "ground_control_accel", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "air_control_speed", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "air_control_accel", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "ground_jump_impulse", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "air_jump_impulse", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "hook_length", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "player_collision", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "player_hooking", 0.0f});
	vSettings.push_back({ZoneOffsetFrozen, "player_hammering", 0.0f});
}

int CHideAndSeekZone::TuneZoneBase()
{
	// Zone 0 is the global tuning slot, and the last zone has to still fit
	return std::clamp(g_Config.m_SvHideSeekTuneZone, 1, TuneZone::NUM - NumTuneZones);
}

static const char *TuneZoneName(int ZoneOffset)
{
	switch(ZoneOffset)
	{
	case ZoneOffsetSeeker: return "seeker";
	case ZoneOffsetHider: return "hider";
	case ZoneOffsetGhost: return "ghost";
	case ZoneOffsetFrozen: return "frozen";
	}
	return "unknown";
}

void CHideAndSeekZone::BuildTuneZoneSettings(std::vector<std::string> &vLines)
{
	std::vector<STuneZoneSetting> vSettings;
	BuildTuneZoneTable(vSettings);

	const int Base = TuneZoneBase();
	char aBuf[128];
	for(int ZoneOffset = 0; ZoneOffset < NumTuneZones; ZoneOffset++)
	{
		if(ZoneOffset != ZoneOffsetSeeker)
			vLines.emplace_back(""); // one blank line between the zones

		str_format(aBuf, sizeof(aBuf), "# %s tune zone", TuneZoneName(ZoneOffset));
		vLines.emplace_back(aBuf);

		for(const STuneZoneSetting &Setting : vSettings)
		{
			if(Setting.m_ZoneOffset != ZoneOffset)
				continue;

			str_format(aBuf, sizeof(aBuf), "tune_zone %d %s %.2f", Base + ZoneOffset, Setting.m_pParam, Setting.m_Value);
			vLines.emplace_back(aBuf);
		}
	}
}

bool CHideAndSeekZone::InitTuning()
{
	const int Base = TuneZoneBase();
	m_SeekerTuneZone = Base + ZoneOffsetSeeker;
	m_HiderTuneZone = Base + ZoneOffsetHider;
	m_GhostTuneZone = Base + ZoneOffsetGhost;
	m_FrozenTuneZone = Base + ZoneOffsetFrozen;

	// The map carries these settings as well (that is how the clients get to know them), the server
	// applies them again here so the game still works on a map that wasnt written with them.
	std::vector<STuneZoneSetting> vSettings;
	BuildTuneZoneTable(vSettings);

	CTuningParams *pTune = GameServer()->TuningList(MultiMapIndex());
	for(const STuneZoneSetting &Setting : vSettings)
	{
		if(!pTune[Base + Setting.m_ZoneOffset].Set(Setting.m_pParam, Setting.m_Value))
		{
			log_error("hide-n-seek", "Unknown tuning '%s', aborting game", Setting.m_pParam);
			return false;
		}
	}

	return true;
}

void CHideAndSeekZone::HoldPlayers()
{
	for(int ClientId : m_vCandidateIds)
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(!pChr)
			continue;

		m_aClientData[ClientId].m_Held = true;

		// Freeze zeroes direction, jump and hook on the client too, the tune zone takes care of the
		// rest. Velocity is only cleared once, it is part of the snapshot the client predicts from.
		pChr->ResetHook();
		pChr->SetVelocity(vec2(0, 0));
		pChr->SetTuneOverride(m_FrozenTuneZone);
	}
}

void CHideAndSeekZone::ReleaseHold(int ClientId)
{
	CClientData &Data = m_aClientData[ClientId];
	if(!Data.m_Held)
		return;

	Data.m_Held = false;

	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr)
		return;

	pChr->Unfreeze();
	pChr->SetTuneOverride(-1);

	pChr->GiveWeapon(WEAPON_HAMMER);
	pChr->GiveWeapon(WEAPON_GUN);
	pChr->SetActiveWeapon(WEAPON_HAMMER);
}

void CHideAndSeekZone::ReleasePlayers()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		ReleaseHold(ClientId);
}

void CHideAndSeekZone::SetGhost(int ClientId, int Duration)
{
	if(!IsCandidate(ClientId))
		return;

	CClientData &Data = m_aClientData[ClientId];

	if(Data.m_GhostCooldown > 0 || Data.m_GhostDuration > 0)
		return;

	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(pChr)
	{
		pChr->SetTuneOverride(m_GhostTuneZone);
		GameServer()->CreatePlayerSpawn(pChr->GetPos(), pChr->TeamMask());
	}

	Data.m_GhostDuration = Duration;
	Data.m_GhostCooldown = TimeToTicks(g_Config.m_SvHideSeekHidersGhostCooldown, Server()->TickSpeed());
}

int CHideAndSeekZone::ShowOthers(CPlayer *pPlayer)
{
	if(m_State != EState::Playing)
		return -1;
	if(!IsInArea(pPlayer->GetCid()))
		return -1;

	// Players that arent part of the running round are solo, they would see nothing at all with only team
	if(!m_aClientData[pPlayer->GetCid()].m_Alive)
		return SHOW_OTHERS_ON;

	return SHOW_OTHERS_ONLY_TEAM;
}

bool CHideAndSeekZone::CanUseCommand(CPlayer *pPlayer, const char *pCommand)
{
	if(m_State != EState::Playing)
		return true;
	if(!IsInArea(pPlayer->GetCid()))
		return true;

	if(str_startswith_nocase(pCommand, "team") || str_startswith_nocase(pCommand, "spec"))
		return false;

	if(!m_aClientData[pPlayer->GetCid()].m_Alive)
		return true;

	if(str_startswith_nocase(pCommand, "pause") ||
		str_startswith_nocase(pCommand, "swap"))
	{
		pPlayer->SendChat("You cannot use that command right now.");
		return false;
	}

	return true;
}

bool CHideAndSeekZone::CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget)
{
	if(m_State != EState::Playing)
		return true;
	if(!pPlayer || !pTarget)
		return true;
	if(!IsInArea(pPlayer->GetCid()))
		return true;

	const CClientData &Data = m_aClientData[pPlayer->GetCid()];
	const bool PlayerIsAliveSeeker = Data.m_IsSeeker && Data.m_Alive;
	if(PlayerIsAliveSeeker && !m_aClientData[pTarget->GetCid()].m_IsSeeker)
	{
		pPlayer->SendChat("You can't spectate hiders!");
		return false; // Don't allow spectators to spectate hiders, they might be invisible
	}

	return true;
}

bool CHideAndSeekZone::CanSnapCharacter(CCharacter *pChr, int SnappingClient)
{
	if(!pChr || !pChr->GetPlayer())
		return true; // ?

	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;

	if(m_State != EState::Playing)
		return true;

	const int ClientId = pChr->GetPlayer()->GetCid();

	if(SnappingClient == ClientId)
		return true;

	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
	if(!pSnapPlayer)
		return true;

	// Players outside of the area see the game like any other part of the map,
	// players inside of it only get to see the players that share the area with them
	if(!IsInArea(SnappingClient))
		return true;
	if(!IsInArea(ClientId))
		return false;

	const CClientData &Data = m_aClientData[ClientId];
	const CClientData &SnapData = m_aClientData[SnappingClient];

	// Players that arent part of the round watch it, but are only visible to each other
	if(!Data.m_Alive)
		return !SnapData.m_Alive;
	if(!SnapData.m_Alive)
		return true;

	if(Data.m_IsSeeker || !SnapData.m_IsSeeker)
		return true; // Seekers are always visible and hiders can always see eachother

	CCharacter *pSnapChr = pSnapPlayer->GetCharacter();
	if(pSnapChr)
	{
		if(pSnapChr->Core()->HookedPlayer() == ClientId)
			return true; // Forcefully show the hider
		if(pSnapChr->m_FreezeTime > 0)
			return false; // warmup for hiders so they can run
	}

	if(Data.m_GhostDuration > 0)
		return false; // if the hider is currently a ghost, they are invisible to seekers

	if(Data.m_InHiddenZone)
		return false; // if the hider is in a hidden zone, they are invisible to seekers

	return true;
}

bool CHideAndSeekZone::CanDropWeapon(CCharacter *pChr, int Weapon)
{
	if(!pChr || !pChr->GetPlayer())
		return true;
	if(IsInArea(pChr->GetPlayer()->GetCid()))
		return false;
	return true;
}

void CHideAndSeekZone::OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg)
{
	if(!IsInArea(ClientId))
		return;

	// Dying takes the player out of the round right away. The handover itself runs on the next
	// membership pass, once ContainsPlayer() sees the dead character, and repeats this harmlessly.
	OnPlayerLeave(ClientId);
}

bool CHideAndSeekZone::OnCharacterFire(CCharacter *pChr, int Weapon)
{
	if(m_State != EState::Playing)
		return true;

	const int ClientId = pChr->GetPlayer()->GetCid();

	if(!IsCandidate(ClientId))
		return true;

	if(!m_aClientData[ClientId].m_Alive)
		return false;

	if(m_aClientData[ClientId].m_IsSeeker)
	{
		if(Weapon == WEAPON_GUN)
		{
			int ClosestId = GetClosestHiderId(ClientId);
			if(ClosestId != -1 && m_aClientData[ClientId].m_GunReloadTimer <= 0)
			{
				vec2 Dir = normalize(m_aClientData[ClosestId].m_LastKnownPos - pChr->GetPos());

				vec2 ProjStartPos = pChr->GetPos() + Dir * pChr->GetProximityRadius() * 1.35f;

				int Lifetime = (int)(Server()->TickSpeed() * pChr->GetCurrentTuning()->m_GunLifetime);

				new CHideAndSeekProjectile(
					&GameServer()->m_World,
					ClientId, // Owner (the seeker that shot)
					ProjStartPos, // Pos
					Dir, // Dir
					Lifetime, // Span
					g_Config.m_SvHideSeekSeekersGunFreeze // Freeze in ticks
				);
				GameServer()->CreateSound(pChr->GetPos(), SOUND_GUN_FIRE, pChr->TeamMask());

				m_aClientData[ClientId].m_GunReloadTimer = g_Config.m_SvHideSeekSeekersGunCooldown * Server()->TickSpeed() / 1000;
			}
			return false;
		}
	}
	else
	{
		if(Weapon == WEAPON_HAMMER)
			SetGhost(ClientId, TimeToTicks(g_Config.m_SvHideSeekHidersGhostDuration, Server()->TickSpeed()));
	}
	return true;
}

void CHideAndSeekZone::OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo &ClientInfo, int *pTeam, int *pLatency, int *pScore)
{
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return;
	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
	if(!pSnapPlayer)
		return;

	int ClientId = pPlayer->GetCid();

	CClientData &Data = m_aClientData[ClientId];
	CClientData &SnapData = m_aClientData[SnappingClient];

	*pScore = Data.m_NumWins;

	if(m_State != EState::Playing && m_State != EState::Finished)
		return;

	const bool InArea = IsInArea(ClientId);

	if(!InArea)
	{
		if(ClientId != SnappingClient)
			*pTeam = (int)TEAM_SPECTATORS;
		return;
	}

	if(!Data.m_Alive)
	{
		ClientInfo.m_UseCustomColor = false;
		StrToInts(ClientInfo.m_aSkin, std::size(ClientInfo.m_aSkin), "x_spec");
	}
	else if(!Data.m_IsSeeker)
	{
		if(m_State == EState::Finished)
			return;

		constexpr int ColorHider = 10401598; // Blue
		ClientInfo.m_UseCustomColor = true;
		ClientInfo.m_ColorBody = ColorHider;
		ClientInfo.m_ColorFeet = ColorHider;

		bool CanSeePlayer = !SnapData.m_IsSeeker || CanSnapCharacter(pPlayer->GetCharacter(), SnappingClient);

		if(Data.m_GhostDuration > 0 && CanSeePlayer)
		{
			ClientInfo.m_UseCustomColor = false;
			StrToInts(ClientInfo.m_aSkin, std::size(ClientInfo.m_aSkin), "ghost");
		}
	}
	else
	{
		if(m_State == EState::Finished)
			return;
		constexpr int ColorSeeker = 16758590; // Red
		ClientInfo.m_UseCustomColor = true;
		ClientInfo.m_ColorBody = ColorSeeker;
		ClientInfo.m_ColorFeet = ColorSeeker;
	}
}

void CHideAndSeekZone::OnCharacterHammerHit(int ClientId, int Target)
{
	if(!IsCandidate(ClientId) || !IsCandidate(Target))
		return;
	if(!m_aClientData[ClientId].m_Alive)
		return;
	if(m_aClientData[Target].m_Alive)
	{
		if(m_aClientData[ClientId].m_IsSeeker && !m_aClientData[Target].m_IsSeeker)
		{
			// m_aClientData[Target].m_IsSeeker = true;
			CCharacter *pTargetChr = GameServer()->GetPlayerChar(Target);

			if(pTargetChr)
			{
				m_aClientData[ClientId].m_NumKills++;
				SetDead(Target, ClientId);
			}
		}
	}
}

bool CHideAndSeekZone::SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags)
{
	if(Asker == SERVER_DEMO_CLIENT)
		return true;

	if(!CheckClientId(Asker) || !CheckClientId(ClientId))
		return true; // Ignore

	CCharacter *pChr = GameServer()->GetPlayerChar(Asker);
	if(!pChr)
		return true; // Ignore
	CCharacter *pTargetChr = GameServer()->GetPlayerChar(ClientId);
	if(!pTargetChr)
		return true; // Ignore

	return CanSnapCharacter(pChr, ClientId);
}

int CHideAndSeekZone::GetClosestHiderId(int SeekerId)
{
	CCharacter *pSeekerChr = GameServer()->GetPlayerChar(SeekerId);
	if(!pSeekerChr)
		return -1;

	float Dist = std::numeric_limits<float>::max();
	int ClosestId = -1;
	for(int ClientId : m_vCandidateIds)
	{
		if(ClientId == SeekerId)
			continue;
		if(m_aClientData[ClientId].m_IsSeeker)
			continue;
		if(!m_aClientData[ClientId].m_Alive)
			continue;

		CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
		if(!pChr)
			continue;

		float NewDist = distance(pChr->GetPos(), pSeekerChr->GetPos());
		if(NewDist < Dist)
		{
			Dist = NewDist;
			ClosestId = ClientId;
		}
	}
	return ClosestId;
}

bool CHideAndSeekZone::TryReplaceAfkSeeker(int ClientId)
{
	if(!m_aClientData[ClientId].m_IsSeeker || !m_aClientData[ClientId].m_Alive)
		return false;

	int AliveSeekers = 0;
	for(int CandidateId : m_vCandidateIds)
	{
		if(!m_aClientData[CandidateId].m_Alive)
			continue;
		if(m_aClientData[CandidateId].m_IsSeeker)
			AliveSeekers++;
	}

	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(pChr)
	{
		pChr->Unfreeze();
		pChr->SetTuneOverride(-1);
	}

	m_aClientData[ClientId].m_IsSeeker = false;
	m_aClientData[ClientId].m_MarkedAfk = true;
	SetDead(ClientId);

	SendChatCandidates("A seeker went AFK and was eliminated.");

	std::vector<int> vAliveHiders;
	vAliveHiders.reserve(m_vCandidateIds.size());
	int AlivePlayers = 0;
	for(int CandidateId : m_vCandidateIds)
	{
		if(!m_aClientData[CandidateId].m_Alive)
			continue;

		AlivePlayers++;
		if(!m_aClientData[CandidateId].m_IsSeeker)
			vAliveHiders.push_back(CandidateId);
	}

	if(AliveSeekers > 1 || AlivePlayers <= 2 || vAliveHiders.empty())
		return false;

	std::uniform_int_distribution<size_t> Range(0, vAliveHiders.size() - 1);
	const int NewSeekerId = vAliveHiders[Range(Rng())];
	CClientData &NewSeekerData = m_aClientData[NewSeekerId];
	NewSeekerData.m_IsSeeker = true;
	NewSeekerData.m_LastMovement = Server()->Tick();

	CCharacter *pNewSeekerChr = GameServer()->GetPlayerChar(NewSeekerId);
	if(pNewSeekerChr)
	{
		pNewSeekerChr->SetTuneOverride(m_SeekerTuneZone);
		pNewSeekerChr->FreezeForce(g_Config.m_SvHideSeekFreezeDuration * Server()->TickSpeed());
		pNewSeekerChr->ForceSetPos(GetRandomSpawnPos());
	}
	m_SeekTimeRemaining += MaxAfkSeconds * Server()->TickSpeed();
	UpdateGameInfoTimer();

	for(int CandidateId : m_vCandidateIds)
	{
		CPlayer *pCandidate = GameServer()->m_apPlayers[CandidateId];
		if(!pCandidate)
			continue;

		if(CandidateId == NewSeekerId)
			pCandidate->SendChat("You are the new seeker!");
		else
			pCandidate->SendChatFmt("'%s' is the new seeker!", Server()->ClientName(NewSeekerId));
	}

	return true;
}

void CHideAndSeekZone::SetForcedSolo(int ClientId, bool Solo)
{
	CClientData &Data = m_aClientData[ClientId];
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr)
	{
		// Solo doesn't survive a death, so there is nothing left to release
		Data.m_ForcedSolo = false;
		return;
	}

	if(Solo)
	{
		if(pChr->Core()->m_Solo)
			return; // never claim a solo the player got from somewhere else

		pChr->UnSpawnSolo(false);
		pChr->SetSolo(true);
		Data.m_ForcedSolo = true;
	}
	else
	{
		if(!Data.m_ForcedSolo)
			return;

		pChr->SetSolo(false);
		Data.m_ForcedSolo = false;
	}
}

void CHideAndSeekZone::SetDead(int ClientId, std::optional<int> Killer)
{
	m_aClientData[ClientId].m_Alive = false;
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(pChr)
	{
		pChr->ResetHook();
		pChr->Unfreeze();
		pChr->SetTuneOverride(-1);
	}
	SetForcedSolo(ClientId, true);

	if(Killer.has_value())
	{
		for(int CandidateId : m_vCandidateIds)
		{
			CPlayer *pCandidate = GameServer()->m_apPlayers[CandidateId];
			if(!pCandidate)
				continue;

			if(CandidateId == ClientId)
				pCandidate->SendChat("You got found!");
			else
				pCandidate->SendChatFmt("'%s' got found!", Server()->ClientName(ClientId));
		}

		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = Killer.value();
		Msg.m_Victim = ClientId;
		Msg.m_Weapon = WEAPON_HAMMER;
		Msg.m_ModeSpecial = 0;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
}

void CHideAndSeekZone::SendChatCandidates(const char *pMessage)
{
	for(int ClientId : m_vCandidateIds)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(pPlayer)
			pPlayer->SendChat(pMessage);
	}
}

int CHideAndSeekZone::UpdateCandidates()
{
	m_vCandidateIds.clear();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;
		CCharacter *pChr = pPlayer->GetCharacter();
		if(!pChr)
			continue;
		if(!pChr->IsAlive())
			continue;
		if(pChr->Team() != TEAM_FLOCK)
			continue;
		if(pPlayer->IsAfk())
			continue;
		if(m_aClientData[ClientId].m_MarkedAfk)
			continue;
		if(!IsInArea(ClientId))
			continue;

		m_vCandidateIds.push_back(ClientId);
	}

	return (int)m_vCandidateIds.size();
}

bool CHideAndSeekZone::IsCandidate(int ClientId) const
{
	return std::find(m_vCandidateIds.begin(), m_vCandidateIds.end(), ClientId) != m_vCandidateIds.end();
}

bool CHideAndSeekZone::IsAliveHider(int ClientId) const
{
	if(m_State != EState::Playing)
		return false;
	if(!CheckClientId(ClientId))
		return false;

	const CClientData &Data = m_aClientData[ClientId];
	if(!Data.m_Alive || Data.m_IsSeeker)
		return false;

	return IsInArea(ClientId);
}

vec2 CHideAndSeekZone::GetRandomSpawnPos()
{
	if(m_vSpawnQuads.empty())
		return vec2(0, 0);

	std::uniform_int_distribution<int> Rand(0, (int)m_vSpawnQuads.size() - 1);

	// Spawn quads can be (partly) covered by solid tiles, don't get stuck here if none of them is free
	constexpr int MaxTries = 100;
	vec2 Pos = vec2(0, 0);
	for(int Try = 0; Try < MaxTries; Try++)
	{
		Pos = RandomPointInQuad(m_vSpawnQuads[Rand(Rng())]);
		if(!Collision()->CheckPoint(Pos))
			return Pos;
	}

	log_error("hide-n-seek", "Failed to find a free spawn position, the spawn quads seem to be inside solid");
	return Pos;
}

void CHideAndSeekZone::Init(CMapItemLayerQuads *pQuadsLayer)
{
	char aLayerName[30];
	IntsToStr(pQuadsLayer->m_aName, std::size(pQuadsLayer->m_aName), aLayerName, std::size(aLayerName));

	CQuad *pQuads = (CQuad *)GameServer()->Map(MultiMapIndex())->GetDataSwapped(pQuadsLayer->m_Data);
	ReserveQuads(pQuadsLayer->m_NumQuads);

	ESubType SubType = ESubType::Area;
	if(!str_comp(aLayerName, "Area"))
		SubType = ESubType::Area;
	else if(!str_comp(aLayerName, "Spawn"))
		SubType = ESubType::Spawn;
	else if(!str_comp(aLayerName, "Hidden"))
		SubType = ESubType::Hidden;
	else
		return;
	for(int NumQuads = 0; NumQuads < pQuadsLayer->m_NumQuads; NumQuads++)
	{
		CQuadData QuadData;
		QuadData.Init(&pQuads[NumQuads], GameServer()->Map(MultiMapIndex()));
		QuadData.m_SubType = (uint8_t)SubType;

		if(SubType == ESubType::Area)
			AddAreaQuad(QuadData); // these decide who the zone owns, see IMinigame::ContainsPlayer
		else
			AddQuad(QuadData);

		if(SubType == ESubType::Spawn)
			m_vSpawnQuads.push_back(QuadData);
	}
}
