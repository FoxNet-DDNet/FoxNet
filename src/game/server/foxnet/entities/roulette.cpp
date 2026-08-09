// Made by qxdFox
#include "roulette.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/components/zones/roulette.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <cinttypes>
#include <cmath>
#include <random>
#include <string>
#include <vector>

CRoulette::CRoulette(CGameWorld *pGameWorld, int MultiMapIdx, vec2 Pos) :
	CEntity(pGameWorld, MultiMapIdx, CGameWorld::ENTTYPE_ROULETTE, true, Pos, 54)
{
	m_Pos = Pos;
	m_StartDelay = -1;
	SetState(RStates::IDLE);
	PrepareNextSpin();
	GameWorld()->InsertEntity(this);
}

CRouletteZone *CRoulette::Zone()
{
	return GameServer()->m_ZoneManager.FindMinigame<CRouletteZone>(MultiMapIdx());
}

bool CRoulette::CanBet(int ClientId) const
{
	if(m_State != RStates::IDLE && m_State != RStates::PREPARING)
		return false;
	if(m_aClients[ClientId].m_Active)
		return false;

	return true;
}

void CRoulette::ResetClients()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		m_aClients[ClientId].m_BetAmount = -1;
		m_aClients[ClientId].m_aBetOption[0] = '\0';
		m_aClients[ClientId].m_Active = false;
	}
}

int CRoulette::AmountOfCloseClients()
{
	int Count = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;
		if(pPlayer->IsAfk())
			continue;
		if(pPlayer->MultiMapIdx() != MultiMapIdx())
			continue;
		if(!pPlayer->Acc()->m_LoggedIn)
			continue;
		if(!pPlayer->Acc()->m_Money)
			continue;
		CCharacter *pCharacter = pPlayer->GetCharacter();
		if(!pCharacter)
			continue;
		if(distance(pCharacter->m_Pos, m_Pos) > 32.0f * 13.0f)
			continue;
		if(pCharacter->Team() != TEAM_FLOCK)
			continue;

		Count++;
	}
	return Count;
}

bool CRoulette::AddClient(int ClientId, int64_t BetAmount, const char *pBetOption)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	if(pPlayer->MultiMapIdx() != MultiMapIdx())
		return false;

	if(!Zone() || pPlayer->m_pMinigame != Zone())
		return false;

	if(!g_Config.m_SvAccounts)
	{
		GameServer()->SendChatTarget(ClientId, "Feature is disabled.");
		return false;
	}

	if(!CanBet(ClientId))
	{
		GameServer()->SendChatTarget(ClientId, "Wait until the current round is over.");
		return false;
	}

	if(!pPlayer->Acc()->m_LoggedIn)
	{
		GameServer()->SendChatTarget(ClientId, "You need to be logged in for this");
		return false;
	}

	if(BetAmount <= 0)
	{
		GameServer()->SendChatTarget(ClientId, "You need to set a wager first, use /bet <Amount>");
		return false;
	}

	if(pPlayer->Acc()->m_Money < BetAmount)
	{
		GameServer()->SendChatTarget(ClientId, "You don't have enough money to place that bet anymore.");
		return false;
	}

	bool ValidOption = false;
	for(const char *pOptions : RouletteOptions)
	{
		if(!str_comp(pOptions, pBetOption))
		{
			ValidOption = true;
			break;
		}
	}
	if(!ValidOption)
	{
		GameServer()->SendChatTarget(ClientId, "Something went wrong, please try again!");
		return false;
	}

	SetState(RStates::PREPARING);
	pPlayer->TakeMoney(BetAmount, true);

	m_aClients[ClientId].m_BetAmount = BetAmount;
	str_copy(m_aClients[ClientId].m_aBetOption, pBetOption);
	m_aClients[ClientId].m_Active = true;

	m_Betters++;
	m_TotalWager += BetAmount;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "You placed a bet of %" PRId64 " on %s", BetAmount, pBetOption);
	GameServer()->SendChatTarget(ClientId, aBuf);
	return true;
}

void CRoulette::OnClientReset(int ClientId)
{
	if(m_State == RStates::IDLE)
	{
		ClearClientBet(ClientId, true);
		return;
	}

	if(m_aClients[ClientId].m_Active)
	{
		EvaluateBet(ClientId, true);
		ClearClientBet(ClientId);
	}
}

void CRoulette::SetState(RStates State)
{
	if(m_State == State)
		return;

	m_State = State;

	if(State == RStates::PREPARING)
	{
		int Close = AmountOfCloseClients();

		if(Close > 3)
			m_StartDelay = Server()->TickSpeed() * 7.5; // 7.5 seconds
		else if(Close > 1)
			m_StartDelay = Server()->TickSpeed() * 5; // 5 seconds
		else
			m_StartDelay = Server()->TickSpeed() * 1.5; // 1.5 seconds
	}
}

void CRoulette::PrepareNextSpin()
{
	static std::random_device Rd;
	static std::mt19937 Rng(Rd());
	std::uniform_int_distribution<> DurationDist(MIN_SPIN_DURATION, MAX_SPIN_DURATION);
	std::uniform_real_distribution<> SlowDist(0.35f, 0.7f);

	m_SpinDuration = DurationDist(Rng);
	m_SlowDownFactor = SlowDist(Rng);
	m_EndingField = CalculateEndingField(m_SpinDuration, m_SlowDownFactor);
}

int CRoulette::GetField(float Rotation) const
{
	constexpr float TWO_PI = 2.f * pi;
	constexpr float FIELD_ANGLE = TWO_PI / MAX_FIELDS;
	constexpr float HALF_FIELD = FIELD_ANGLE * 0.5f;
	constexpr float ANGLE_TOP = 3.f * pi / 2.f;

	constexpr float FINE_TUNE = 0.0f;
	constexpr int SECTOR_OFFSET = 0;
	constexpr bool HORIZONTAL_FLIP = true;

	float rot = fmodf(Rotation, TWO_PI);
	if(rot < 0.f)
		rot += TWO_PI;

	float rel = ANGLE_TOP - 1 * rot;
	rel = fmodf(rel, TWO_PI);
	if(rel < 0.f)
		rel += TWO_PI;
	rel += HALF_FIELD + FINE_TUNE;
	if(rel >= TWO_PI)
		rel -= TWO_PI;

	int index = static_cast<int>(rel / FIELD_ANGLE) % MAX_FIELDS;

	index = (index + SECTOR_OFFSET + MAX_FIELDS) % MAX_FIELDS;

	if(HORIZONTAL_FLIP)
		index = (MAX_FIELDS - index) % MAX_FIELDS;

	return index;
}

int CRoulette::GetField() const
{
	return GetField(m_Rotation);
}

int CRoulette::CalculateEndingField(int SpinDuration, float SlowDownFactor) const
{
	float Rotation = m_Rotation;
	float RotationSpeed = m_RotationSpeed;
	RStates State = RStates::SPINNING;
	bool FirstSpinTick = true;

	while(true)
	{
		if((State == RStates::SPINNING || State == RStates::STOPPING) && !FirstSpinTick)
			SpinDuration--;

		if(State == RStates::SPINNING)
		{
			RotationSpeed += 0.005f;
			if(RotationSpeed > 0.5f)
				RotationSpeed = 0.5f;
		}
		else if(State == RStates::STOPPING)
		{
			RotationSpeed -= 0.005f * SlowDownFactor;
			if(RotationSpeed < 0.0f)
				return GetField(Rotation);
		}

		Rotation += RotationSpeed;

		if(SpinDuration <= 0 && State == RStates::SPINNING)
			State = RStates::STOPPING;

		FirstSpinTick = false;
	}
}

void CRoulette::ClearClientBet(int ClientId, bool Refund)
{
	if(m_aClients[ClientId].m_Active)
	{
		if(Refund)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(pPlayer && pPlayer->Acc()->m_LoggedIn)
				pPlayer->GiveMoney(m_aClients[ClientId].m_BetAmount, false, true);
		}

		m_TotalWager -= m_aClients[ClientId].m_BetAmount;
		if(m_TotalWager < 0)
			m_TotalWager = 0;

		if(m_Betters > 0)
			m_Betters--;
	}

	m_aClients[ClientId].m_BetAmount = -1;
	m_aClients[ClientId].m_aBetOption[0] = '\0';
	m_aClients[ClientId].m_Active = false;
}

void CRoulette::EvaluateBet(int ClientId, bool Silent)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(!m_aClients[ClientId].m_Active)
		return;
	if(m_EndingField < 0)
		return;

	const int WinningField = m_EndingField;
	const int Color = m_aFields[WinningField].m_Color;
	const int Number = m_aFields[WinningField].m_Number;
	const int64_t Amount = m_aClients[ClientId].m_BetAmount;

	float PayoutMultiplier = 0;
	if(str_comp(m_aClients[ClientId].m_aBetOption, "Black") == 0 && Color == COLOR_BLACK)
		PayoutMultiplier = 2;
	else if(str_comp(m_aClients[ClientId].m_aBetOption, "Red") == 0 && Color == COLOR_RED)
		PayoutMultiplier = 2;
	else if(str_comp(m_aClients[ClientId].m_aBetOption, "Green") == 0 && Color == COLOR_GREEN)
		PayoutMultiplier = 14;
	else if(str_comp(m_aClients[ClientId].m_aBetOption, "Even") == 0 && Number != 0 && Number % 2 == 0)
		PayoutMultiplier = 2;
	else if(str_comp(m_aClients[ClientId].m_aBetOption, "Odd") == 0 && Number % 2 == 1)
		PayoutMultiplier = 2;
	else if(str_comp(m_aClients[ClientId].m_aBetOption, "1-12") == 0 && Number >= 1 && Number <= 12)
		PayoutMultiplier = 3;
	else if(str_comp(m_aClients[ClientId].m_aBetOption, "13-24") == 0 && Number >= 13 && Number <= 24)
		PayoutMultiplier = 3;
	else if(str_comp(m_aClients[ClientId].m_aBetOption, "25-36") == 0 && Number >= 25 && Number <= 36)
		PayoutMultiplier = 3;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(pPlayer && pPlayer->Acc()->m_LoggedIn)
	{
		if(PayoutMultiplier > 0)
			pPlayer->GiveMoney((int64_t)(Amount * PayoutMultiplier), false, Silent);
	}

	ClearClientBet(ClientId);
}

void CRoulette::StartSpin()
{
	if(m_State != RStates::PREPARING)
		return;

	if(m_State == RStates::PREPARING)
	{
		m_StartDelay = -1;
		SetState(RStates::SPINNING);
	}
}

void CRoulette::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("roulette", "Reset");

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		ClearClientBet(ClientId, true);

	if(GetId().has_value())
		Server()->SnapFreeId(GetId().value());
	GameWorld()->RemoveEntity(this);
}

void CRoulette::Tick()
{
	if(m_State == RStates::SPINNING || m_State == RStates::STOPPING)
		m_SpinDuration--;
	if(m_StartDelay > 0)
		m_StartDelay--;
	else if(m_State == RStates::PREPARING && m_StartDelay == 0)
		StartSpin();

	if(m_State == RStates::SPINNING)
	{
		m_RotationSpeed += 0.005f;
		if(m_RotationSpeed > 0.5f)
			m_RotationSpeed = 0.5f;
	}
	else if(m_State == RStates::STOPPING)
	{
		m_RotationSpeed -= 0.005f * m_SlowDownFactor;
		if(m_RotationSpeed < 0.0f)
		{
			m_RotationSpeed = 0.0f;
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
				EvaluateBet(ClientId);
			SetState(RStates::IDLE);
			PrepareNextSpin();
		}
	}
	m_Rotation += m_RotationSpeed;

	if(m_SpinDuration <= 0)
	{
		if(m_State == RStates::SPINNING)
			SetState(RStates::STOPPING);
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		SendBroadcast(ClientId);
}

void CRoulette::SendBroadcast(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!Zone() || pPlayer->m_pMinigame != Zone())
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(pChr->Team() != TEAM_FLOCK)
		return;

	float TimeLeft = (float)m_StartDelay / (float)Server()->TickSpeed();
	char aBuf[32];

	std::vector<std::string> Messages;

	if(pPlayer->Acc()->m_LoggedIn)
	{
		str_format(aBuf, sizeof(aBuf), "%" PRId64 "%s", pPlayer->Acc()->m_Money, g_Config.m_SvCurrencyName);
		Messages.push_back(aBuf);

		if(m_State == RStates::IDLE)
		{
			if(pPlayer->m_BetAmount <= 0)
				str_copy(aBuf, "Wager: Nothing");
			else
				str_format(aBuf, sizeof(aBuf), "Wager: %" PRId64, pPlayer->m_BetAmount);
			Messages.push_back(aBuf);
		}
	}

	if(m_State != RStates::IDLE)
	{
		if(m_aClients[ClientId].m_Active)
		{
			str_format(aBuf, sizeof(aBuf), "You bet on: %s", m_aClients[ClientId].m_aBetOption);
			Messages.push_back(aBuf);
		}

		str_format(aBuf, sizeof(aBuf), "\nPlayers: %d", m_Betters);
		Messages.push_back(aBuf);
		str_format(aBuf, sizeof(aBuf), "Total Bets: %" PRId64 "\n", m_TotalWager);
		Messages.push_back(aBuf);
		if(m_StartDelay > 0)
		{
			str_format(aBuf, sizeof(aBuf), "Starting in: %.1fs", TimeLeft);
			Messages.push_back(aBuf);
		}
	}

	pPlayer->SendBroadcastHud(Messages, 2);
}

void CRoulette::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const int SnappingClientVersion = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	float RouletteLength = g_Config.m_SvRouletteLength;

	vec2 From = m_Pos + direction(m_Rotation) * RouletteLength;

	if(GetId().has_value())
		GameServer()->SnapLaserObject(CSnapContext(SnappingClientVersion, SixUp, SnappingClient), GetId().value(), From, m_Pos, 0, -1, LASERTYPE_DRAGGER, LASERDRAGGERTYPE_WEAK, -1, LASERFLAG_NO_PREDICT);
}
