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
#include <game/server/entities/projectile.h>
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

constexpr static float MaxSpawnPointOffset = 16.0f;
constexpr static float MaxAfkSeconds = 30.0f; // seconds

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
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		ClientTick(ClientId);

	int NumPlayers = GetNumCandidates();

	if(m_State == EState::BadMap)
	{
		for(CCharacter *pChr : m_vCandidates)
			pChr->GetPlayer()->SendBroadcast("Hide and Seek cannot be started on this map.");
		return;
	}

	int NumSeekers = 0;
	for(CCharacter *pChr : m_vCandidates)
	{
		if(m_aClientData[pChr->GetPlayer()->GetCid()].m_IsSeeker)
			NumSeekers++;
	}

	if(NumPlayers < 2 && m_State != EState::WaitingForPlayers)
	{
		EndGame(EWinState::None);
		m_State = EState::WaitingForPlayers;
	}

	if(m_State == EState::WaitingForPlayers)
	{
		if(NumPlayers < 2)
		{
			for(CCharacter *pChr : m_vCandidates)
				pChr->GetPlayer()->SendBroadcast("Not enough players to start hide and seek.");
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
			for(CCharacter *pChr : m_vCandidates)
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "Hide and Seek starts in %d seconds", (int)((m_WarmUpTime - Server()->Tick()) / Server()->TickSpeed()));
				pChr->GetPlayer()->SendBroadcast(aBuf);
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
			m_FinishedDelay = Server()->Tick() + Server()->TickSpeed() * 5;
			return; // Don't check for win conditions anymore
		}

		int NumHiders = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			if(IsCandidate(ClientId) && !m_aClientData[ClientId].m_IsSeeker && m_aClientData[ClientId].m_Alive)
				NumHiders++;
		}
		if(NumHiders == 0)
		{
			EndGame(EWinState::Seeker);
			m_State = EState::Finished;
			m_FinishedDelay = Server()->Tick() + Server()->TickSpeed() * 5;
		}
	}
	else if(m_State == EState::Finished)
	{
		if(Server()->Tick() > m_FinishedDelay)
		{
			m_State = EState::WaitingForPlayers;
			for(CCharacter *pChr : m_vCandidates)
			{
				if(pChr->GetPlayer()->m_Area != EArea::HideAndSeek)
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
}

void CHideAndSeekZone::OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pPlayer->m_Area != EArea::HideAndSeek)
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
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer || !pPlayer->GetCharacter())
		return;
	if(pPlayer->MultiMapIdx() != (int)MultiMapIndex())
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr->IsAlive())
		return;

	CClientData &Data = m_aClientData[ClientId];

	bool InArea = false;
	Data.m_InHiddenZone = false;
	for(const CQuadData &QuadData : Quads())
	{
		if(QuadData.m_SubType == (uint8_t)ESubType::Area)
		{
			if(InArea)
				continue;

			if(InsideQuad(pChr->GetPos(), QuadData, vec2(0, 0)))
			{
				// check ip
				if(!g_Config.m_SvMinigamesSameIp)
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

						if(pOtherPlayer->m_Area != EArea::HideAndSeek)
							continue;

						// compare addresses
						if(net_addr_comp_noport(Server()->ClientAddr(ClientId), Server()->ClientAddr(i)) == 0)
						{
							pPlayer->SendChat("A player with the same ip is already in that zone.");
							pPlayer->KillCharacter();
							return;
						}
					}
				}
				InArea = true;
			}
		}
		else if(QuadData.m_SubType == (uint8_t)ESubType::Hidden)
		{
			if(Data.m_InHiddenZone)
				continue;

			if(InsideQuad(pChr->GetPos(), QuadData, vec2(0, 0)))
				Data.m_InHiddenZone = true;
		}
	}

	if(InArea)
	{
		if(pPlayer->m_Area != EArea::HideAndSeek)
		{
			pPlayer->SetArea(EArea::HideAndSeek);
			Data.m_JoinedAt = Server()->Tick();
			Data.m_LastMovement = Server()->Tick();
			pChr->SetTuneOverride(-1);
			pChr->SetSolo(false);
		}
	}
	else
	{
		if(pPlayer->m_Area == EArea::HideAndSeek)
		{
			pPlayer->SetArea(EArea::Game);
			pChr->SetTuneOverride(-1);
			pChr->SetSolo(false);
		}
	}

	if(pChr->m_Pos != pChr->m_PrevPos)
	{
		Data.m_LastMovement = Server()->Tick();
		Data.m_MarkedAfk = false;
		if(m_State != EState::Playing && InArea)
			pChr->SetSolo(false);
	}

	if(m_State == EState::Playing)
	{
		if(Data.m_JoinedAt == Server()->Tick())
			SetDead(ClientId, false);
		if(pPlayer->IsAfk())
			SetDead(ClientId, false);

		if(!Data.m_Alive)
		{
			if(!pChr->Core()->m_Solo)
				pChr->SetSolo(true);
			return;
		}

		if(Data.m_IsSeeker && Server()->Tick() - Data.m_LastMovement > MaxAfkSeconds * Server()->TickSpeed())
		{
			TryReplaceAfkSeeker(ClientId);
			return;
		}

		std::vector<std::string> Messages;
		std::string Msg = "";

		if(!Data.m_IsSeeker)
		{
			// If the player is currently a ghost, we don't want to decrease the cooldown until the timeout is over
			if(Data.m_GhostCooldown > 0 && Data.m_GhostDuration <= 0)
				Data.m_GhostCooldown--;

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
	else if(m_State == EState::Finished && IsCandidate(ClientId))
	{
		// Only freeze players who were actually part of the game (joined before the game ended)
		if(!Data.m_Alive)
		{
			pChr->SetVelocity(vec2(0, 0));
			pChr->ResetHook();
			pChr->ForceSetPos(pChr->m_PrevPos);
		}
	}
}

void CHideAndSeekZone::StartGame()
{
	if(!InitTuning())
	{
		m_State = EState::BadMap;
		return;
	}

	for(CCharacter *pChr : m_vCandidates)
	{
		int ClientId = pChr->GetPlayer()->GetCid();
		m_aClientData[ClientId].m_Alive = true;
		m_aClientData[ClientId].m_IsSeeker = false;
		m_aClientData[ClientId].m_LastMovement = Server()->Tick();
		pChr->GetPlayer()->ClearBroadcast();
		pChr->GetPlayer()->Pause(CPlayer::PAUSE_NONE, true);
		pChr->SetSolo(false);
		pChr->GiveWeapon(WEAPON_GUN);
		pChr->GiveWeapon(WEAPON_HAMMER);
	}

	const int NumCandidates = (int)m_vCandidates.size();
	const int DesiredSeekers = std::min(NumCandidates, std::max(1, (NumCandidates + 3) / 4));

	std::vector<int> vCandidateIds;
	vCandidateIds.reserve(m_vCandidates.size());
	for(CCharacter *pChr : m_vCandidates)
		vCandidateIds.push_back(pChr->GetPlayer()->GetCid());

	std::shuffle(vCandidateIds.begin(), vCandidateIds.end(), Rng());

	for(int i = 0; i < DesiredSeekers; i++)
		m_aClientData[vCandidateIds[i]].m_IsSeeker = true;

	const int NumSeekers = DesiredSeekers;
	const int NumHiders = NumCandidates - NumSeekers;

	// move players to spawn points
	for(CCharacter *pChr : m_vCandidates)
	{
		CPlayer *pPlayer = pChr->GetPlayer();

		int ClientId = pPlayer->GetCid();
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

	const int SeekTimeSeconds = maximum(0, m_SeekTimeTotal / Server()->TickSpeed());
	m_GameInfoTimeLimit = maximum(1, (SeekTimeSeconds + 59) / 60);

	const int ElapsedSeconds = m_GameInfoTimeLimit * 60 - SeekTimeSeconds;
	m_GameInfoRoundStartTick = Server()->Tick() - ElapsedSeconds * Server()->TickSpeed();
}

void CHideAndSeekZone::EndGame(EWinState WinState)
{
	int NumSeekers = 0;
	int NumHiders = 0;
	for(CCharacter *pChr : m_vCandidates)
	{
		if(m_aClientData[pChr->GetPlayer()->GetCid()].m_IsSeeker)
			NumSeekers++;
		else
			NumHiders++;
	}

	for(CCharacter *pChr : m_vCandidates)
	{
		CClientData &Data = m_aClientData[pChr->GetPlayer()->GetCid()];

		CPlayer *pPlayer = pChr->GetPlayer();
		if(WinState == EWinState::None)
		{
			pPlayer->SendChat("Game ended prematurely");
		}
		else if(WinState == EWinState::Hiders)
		{
			if(NumHiders != 1)
				pPlayer->SendChat("The Hiders won the game!");
			else
				pPlayer->SendChatFmt("'%s' won the game!", Server()->ClientName(pPlayer->GetCid()));

			// Xp for hiders gets given based on how long they werent hidden for
			if(!Data.m_IsSeeker)
			{
				if(g_Config.m_SvHideSeekGiveXp)
				{
					int NumSeekingSeconds = m_SeekTimeTotal / (float)Server()->TickSpeed();
					int HiddenSeconds = Data.m_NumHiddenTicks / (float)Server()->TickSpeed();

					int a = NumSeekingSeconds / std::max(HiddenSeconds, 1);
					int b = std::clamp(a, 1, 10);

					pPlayer->GiveXP(5 * b, "", false); // Give XP to the hiders
				}
				Data.m_NumWins++;
			}
		}
		else
		{
			if(NumSeekers != 1)
				pPlayer->SendChat("The Seeker won the game!");
			else
				pPlayer->SendChatFmt("'%s' won the game!", Server()->ClientName(pPlayer->GetCid()));
			if(Data.m_IsSeeker)
			{
				if(g_Config.m_SvHideSeekGiveXp)
				{
					if(m_SeekTimeRemaining > m_SeekTimeTotal * 0.5f)
						pPlayer->GiveXP(5 + Data.m_NumKills * 7, "", false);
				}
				Data.m_NumWins++;
			}
		}

		pChr->SetTuneOverride(-1);
		pChr->SetSolo(false);
		if(Data.m_IsSeeker)
			pChr->Unfreeze();
		pPlayer->ClearBroadcast();
		Data.Reset();
	}

	m_GameInfoTimeLimit = 0;
	m_GameInfoRoundStartTick = 0;

	if(m_SeekerTuneZone > 0)
	{
		GameServer()->TuningList(MultiMapIndex())[m_SeekerTuneZone] = CTuningParams::DEFAULT;
		m_SeekerTuneZone = -1;
	}
	if(m_HiderTuneZone > 0)
	{
		GameServer()->TuningList(MultiMapIndex())[m_HiderTuneZone] = CTuningParams::DEFAULT;
		m_HiderTuneZone = -1;
	}
	if(m_GhostTuneZone > 0)
	{
		GameServer()->TuningList(MultiMapIndex())[m_GhostTuneZone] = CTuningParams::DEFAULT;
		m_GhostTuneZone = -1;
	}
}

bool CHideAndSeekZone::InitTuning()
{
	const CTuningParams &BaseTuning = GameServer()->DDNetDefaultTuning();

	// Find 3 free tuning zones. Zone 0 is the global tuning slot.
	for(int i = 1; i < TuneZone::NUM; i++)
	{
		if(mem_comp(&BaseTuning, &GameServer()->TuningList(MultiMapIndex())[i], sizeof(CTuningParams)) == 0)
		{
			if(m_SeekerTuneZone == -1)
				m_SeekerTuneZone = i;
			else if(m_HiderTuneZone == -1)
				m_HiderTuneZone = i;
			else if(m_GhostTuneZone == -1)
				m_GhostTuneZone = i;
			else
				break;
		}
	}
	if(m_SeekerTuneZone == -1 || m_HiderTuneZone == -1 || m_GhostTuneZone == -1)
	{
		log_error("hide-n-seek", "Failed to find 3 free tune zonse for hide and seek, aborting game");
		return false;
	}

	CTuningParams *pTune = GameServer()->TuningList(MultiMapIndex());

	pTune[m_HiderTuneZone].m_PlayerHooking = 0;
	pTune[m_HiderTuneZone].m_PlayerHammering = 0;

	pTune[m_SeekerTuneZone].m_HammerFireDelay = g_Config.m_SvHideSeekSeekersHammerDelay;
	pTune[m_SeekerTuneZone].m_HammerHitFireDelay = pTune[m_SeekerTuneZone].m_HammerFireDelay + 150;

	pTune[m_GhostTuneZone].m_PlayerHooking = 0;
	pTune[m_GhostTuneZone].m_PlayerHammering = 0;
	pTune[m_GhostTuneZone].m_PlayerCollision = 0;

	return true;
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
	if(!IsCandidate(pPlayer->GetCid()))
		return -1;
	if(m_State != EState::Playing)
		return -1;
	return SHOW_OTHERS_ONLY_TEAM;

	return -1;
}

bool CHideAndSeekZone::CanUseCommand(CPlayer *pPlayer, const char *pCommand)
{
	if(!IsCandidate(pPlayer->GetCid()))
		return true;
	if(m_State != EState::Playing)
		return true;

	if(str_startswith_nocase(pCommand, "team") || str_startswith_nocase(pCommand, "spec"))
		return false;

	if(!m_aClientData[pPlayer->GetCid()].m_Alive)
		return true;

	if(str_startswith_nocase(pCommand, "pause") ||
		str_startswith_nocase(pCommand, "team") ||
		str_startswith_nocase(pCommand, "swap"))
	{
		pPlayer->SendChat("You cannot use that command right now.");
		return false;
	}

	return true;
}

bool CHideAndSeekZone::CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget)
{
	const bool PlayerIsAliveSeeker = m_aClientData[pPlayer->GetCid()].m_IsSeeker && m_aClientData[pPlayer->GetCid()].m_Alive;
	if(PlayerIsAliveSeeker && !m_aClientData[pTarget->GetCid()].m_IsSeeker)
	{
		pPlayer->SendChat("You can't spectate hiders!");
		return false; // Don't allow spectators to spectate hiders, they might be invisible
	}

	return true;
}

bool CHideAndSeekZone::CanSnapCharacter(CCharacter *pChr, int SnappingClient)
{
	if(!pChr)
		return true; // ?

	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;

	if(m_State != EState::Playing)
		return true;

	CPlayer *pPlayer = pChr->GetPlayer();
	int ClientId = pPlayer->GetCid();

	if(SnappingClient == ClientId)
		return true;

	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	bool IsSnapClientCandidate = IsCandidate(SnappingClient);

	if(!pSnapPlayer)
		return false;
	CCharacter *pSnapChr = pSnapPlayer->GetCharacter();

	if(!m_aClientData[ClientId].m_Alive && !m_aClientData[SnappingClient].m_Alive)
		return true;

	if(!m_aClientData[ClientId].m_Alive && IsSnapClientCandidate)
		return false;

	const bool ClientIsAliveSeeker = m_aClientData[ClientId].m_IsSeeker && m_aClientData[ClientId].m_Alive;
	const bool SnapClientIsAliveSeeker = m_aClientData[SnappingClient].m_IsSeeker && m_aClientData[SnappingClient].m_Alive;

	if(ClientIsAliveSeeker && IsSnapClientCandidate)
		return true;

	if(SnapClientIsAliveSeeker)
	{
		if(pSnapChr)
		{
			if(pSnapChr->Core()->HookedPlayer() == ClientId)
				return true; // Forcefully show the hider
			if(pSnapChr->m_FreezeTime > 0)
				return false; // warmup for hiders so they can run
		}

		if(m_aClientData[ClientId].m_GhostDuration > 0)
			return false; // if the hider is currently a ghost, they are invisible to seekers

		if(m_aClientData[ClientId].m_InHiddenZone)
			return false; // if the hider is in a hidden zone, they are invisible to seekers
	}

	return true;
}

bool CHideAndSeekZone::CanDropWeapon(CCharacter *pChr, int Weapon)
{
	if(pChr->GetPlayer()->m_Area == EArea::HideAndSeek)
		return false;
	return true;
}

void CHideAndSeekZone::OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];

	if(pPlayer->m_Area == EArea::HideAndSeek)
		pPlayer->SetArea(EArea::Game);
	m_aClientData[ClientId].Reset();
}

bool CHideAndSeekZone::OnCharacterFire(int ClientId, int Weapon)
{
	if(m_State != EState::Playing)
		return true;

	if(!IsCandidate(ClientId))
		return true;

	if(!m_aClientData[ClientId].m_Alive)
		return false;

	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);

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

				new CProjectile(
					&GameServer()->m_World,
					MultiMapIndex(),
					WEAPON_GUN, // Type
					-1, // Owner
					ProjStartPos, // Pos
					Dir, // Dir
					Lifetime, // Span
					g_Config.m_SvHideSeekSeekersGunFreeze, // Freeze
					false, // Explosive
					-1, // SoundImpact
					vec2(0, 0) // InitDir
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

void CHideAndSeekZone::OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo *pClientInfo, int *pTeam, int *pLatency, int *pScore)
{
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return;
	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
	if(!pSnapPlayer)
		return;

	int ClientId = pPlayer->GetCid();

	CClientData &Data = m_aClientData[ClientId];
	CClientData &SnapData = m_aClientData[SnappingClient];

	bool Candidate = IsCandidate(ClientId);
	bool SnapCandidate = IsCandidate(SnappingClient);

	if(SnapCandidate || SnapData.m_MarkedAfk)
		*pScore = m_aClientData[ClientId].m_NumWins;

	if(m_State != EState::Playing && m_State != EState::Finished)
		return;

	if(!Candidate && ClientId != SnappingClient)
	{
		*pTeam = (int)TEAM_SPECTATORS;
		return;
	}

	if(!Candidate)
		return;

	if(!Data.m_Alive)
	{
		pClientInfo->m_UseCustomColor = false;
		StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), "x_spec");
	}
	else if(!Data.m_IsSeeker)
	{
		if(m_State == EState::Finished)
			return;

		constexpr int ColorHider = 10401598; // Blue
		pClientInfo->m_UseCustomColor = true;
		pClientInfo->m_ColorBody = ColorHider;
		pClientInfo->m_ColorFeet = ColorHider;

		bool CanSeePlayer = !SnapData.m_IsSeeker || CanSnapCharacter(pPlayer->GetCharacter(), SnappingClient);

		if(Data.m_GhostDuration > 0 && CanSeePlayer)
		{
			pClientInfo->m_UseCustomColor = false;
			StrToInts(pClientInfo->m_aSkin, std::size(pClientInfo->m_aSkin), "ghost");
		}
	}
	else
	{
		if(m_State == EState::Finished)
			return;
		constexpr int ColorSeeker = 16758590; // Red
		pClientInfo->m_UseCustomColor = true;
		pClientInfo->m_ColorBody = ColorSeeker;
		pClientInfo->m_ColorFeet = ColorSeeker;
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
			CCharacter *pTargetChr = GameServer()->m_apPlayers[Target]->GetCharacter();

			if(pTargetChr)
			{
				m_aClientData[ClientId].m_NumKills++;
				SetDead(Target, true);
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
	float Dist = std::numeric_limits<float>::max();
	int ClosestId = -1;
	for(CCharacter *pChr : m_vCandidates)
	{
		int ClientId = pChr->GetPlayer()->GetCid();
		if(ClientId == SeekerId)
			continue;
		if(m_aClientData[ClientId].m_IsSeeker)
			continue;
		if(!m_aClientData[ClientId].m_Alive)
			continue;
		if(!pChr->IsAlive())
			continue;

		float NewDist = distance(pChr->GetPos(), GameServer()->m_apPlayers[SeekerId]->GetCharacter()->GetPos());
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
	for(CCharacter *pCandChar : m_vCandidates)
	{
		const int CandidateId = pCandChar->GetPlayer()->GetCid();
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
	SetDead(ClientId, false);

	for(CCharacter *pCandChar : m_vCandidates)
		pCandChar->GetPlayer()->SendChat("A seeker went AFK and was eliminated.");

	std::vector<int> vAliveHiders;
	vAliveHiders.reserve(m_vCandidates.size());
	int AlivePlayers = 0;
	for(CCharacter *pCandChar : m_vCandidates)
	{
		const int CandidateId = pCandChar->GetPlayer()->GetCid();
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
		pNewSeekerChr->SetTuneOverride(m_SeekerTuneZone);

	CPlayer *pNewSeeker = GameServer()->m_apPlayers[NewSeekerId];
	if(pNewSeeker)
		pNewSeeker->SendChat("You are the new seeker!");

	for(CCharacter *pCandChar : m_vCandidates)
	{
		if(pCandChar->GetPlayer()->GetCid() != NewSeekerId)
		{
			pCandChar->GetPlayer()->SendChatFmt("'%s' is the new seeker!", Server()->ClientName(NewSeekerId));
		}
		else
		{
			pCandChar->FreezeForce(g_Config.m_SvHideSeekFreezeDuration * Server()->TickSpeed());
			pCandChar->ForceSetPos(GetRandomSpawnPos());
			m_SeekTimeRemaining += MaxAfkSeconds * Server()->TickSpeed();
		}
	}

	return true;
}

void CHideAndSeekZone::SetDead(int ClientId, bool SendKillMsg)
{
	m_aClientData[ClientId].m_Alive = false;
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(pChr)
	{
		pChr->SetVelocity(vec2(0, 0));
		pChr->ResetHook();
		pChr->Unfreeze();
		pChr->SetTuneOverride(-1);
		pChr->SetSolo(true);
	}
	if(SendKillMsg)
	{
		for(CCharacter *pCandChar : m_vCandidates)
		{
			if(pCandChar->GetPlayer()->GetCid() == ClientId)
			{
				pCandChar->GetPlayer()->SendChat("You got found!");
			}
			else
			{
				pCandChar->GetPlayer()->SendChatFmt("'%s' got found!", Server()->ClientName(ClientId));
			}
		}
	}
}

int CHideAndSeekZone::GetNumCandidates()
{
	m_vCandidates.clear();
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;
		CCharacter *pChr = pPlayer->GetCharacter();
		if(!pChr)
			continue;
		if(!pChr->IsAlive())
			continue;
		if(pChr->Team() != TEAM_FLOCK)
			return false;
		if(pPlayer->IsAfk())
			continue;
		if(m_aClientData[pPlayer->GetCid()].m_MarkedAfk)
			continue;
		if(pPlayer->MultiMapIdx() != (int)MultiMapIndex())
			continue;

		if(pPlayer->m_Area == EArea::HideAndSeek)
			m_vCandidates.push_back(pPlayer->GetCharacter());
	}

	return m_vCandidates.size();
}

bool CHideAndSeekZone::IsCandidate(int ClientId) const
{
	for(CCharacter *pCharacter : m_vCandidates)
	{
		if(!pCharacter || !pCharacter->GetPlayer())
			continue; // instead of return false

		if(pCharacter->GetPlayer()->GetCid() == ClientId)
			return true;
	}

	return false;
}

vec2 CHideAndSeekZone::GetRandomSpawnPos()
{
	if(m_vSpawnPoints.empty())
		return vec2(0, 0);

	std::uniform_int_distribution<size_t> Range(0, m_vSpawnPoints.size() - 1);
	vec2 Pos = m_vSpawnPoints[Range(Rng())];
	Pos += vec2(random_float() - 0.5f, random_float() - 0.5f) * MaxSpawnPointOffset; // add some random offset to prevent players from spawning on top of each other
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
		QuadData.Init(&pQuads[NumQuads]);
		QuadData.m_SubType = (uint8_t)SubType;
		AddQuad(QuadData);

		if(SubType == ESubType::Spawn)
		{
			for(int j = 0; j < 4; j++)
			{
				vec2 Pos;
				if(!GameServer()->GetNearestAirPos(QuadData.m_aPoints[j], &Pos, MaxSpawnPointOffset))
					continue;

				m_vSpawnPoints.push_back(Pos);
			}
		}
	}
}
