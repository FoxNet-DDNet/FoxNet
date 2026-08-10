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

#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <random>
#include <string>
#include <vector>

// Wheel geometry. Shared by GetField() and FieldCenterRotation() so the two stay each other's inverse:
// the landing correction relies on that, it has to move the pointer without changing the winner.
constexpr static float TwoPi = 2.f * pi;
constexpr static float FieldAngle = TwoPi / MAX_FIELDS;
constexpr static float HalfField = FieldAngle * 0.5f;
constexpr static float AngleTop = 3.f * pi / 2.f;
constexpr static float FineTune = 0.0f;
constexpr static int SectorOffset = 0;
constexpr static bool HorizontalFlip = true;

const SFields CRouletteZone::s_aFields[MAX_FIELDS] = {
	{0, COLOR_GREEN},
	{32, COLOR_RED}, {15, COLOR_BLACK}, {19, COLOR_RED}, {4, COLOR_BLACK},
	{21, COLOR_RED}, {2, COLOR_BLACK}, {25, COLOR_RED}, {17, COLOR_BLACK},
	{34, COLOR_RED}, {6, COLOR_BLACK}, {27, COLOR_RED}, {13, COLOR_BLACK},
	{36, COLOR_RED}, {11, COLOR_BLACK}, {30, COLOR_RED}, {8, COLOR_BLACK},
	{23, COLOR_RED}, {10, COLOR_BLACK}, {5, COLOR_RED}, {24, COLOR_BLACK},
	{16, COLOR_RED}, {33, COLOR_BLACK}, {1, COLOR_RED}, {20, COLOR_BLACK},
	{14, COLOR_RED}, {31, COLOR_BLACK}, {9, COLOR_RED}, {22, COLOR_BLACK},
	{18, COLOR_RED}, {29, COLOR_BLACK}, {7, COLOR_RED}, {28, COLOR_BLACK},
	{12, COLOR_RED}, {35, COLOR_BLACK}, {3, COLOR_RED}, {26, COLOR_BLACK}};

CRouletteZone::~CRouletteZone()
{
	// The table is going away with the map, nobody gets to keep a stake on it
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		ClearClientBet(ClientId, true);
}

void CRouletteZone::Init(CMapItemLayerQuads *pQuadsLayer)
{
	char aLayerName[30];
	IntsToStr(pQuadsLayer->m_aName, std::size(pQuadsLayer->m_aName), aLayerName, std::size(aLayerName));

	CQuad *pQuads = (CQuad *)GameServer()->Map(MultiMapIndex())->GetDataSwapped(pQuadsLayer->m_Data);
	ReserveQuads(pQuadsLayer->m_NumQuads);

	ESubType SubType = ESubType::Area;
	if(!str_comp(aLayerName, "Area"))
		SubType = ESubType::Area;
	else if(!str_comp(aLayerName, "Wheel"))
		SubType = ESubType::Wheel;
	else if(str_startswith(aLayerName, "Bet_"))
	{
		SubType = ESubType::BetOption;
	}
	else
		return;

	for(int NumQuads = 0; NumQuads < pQuadsLayer->m_NumQuads; NumQuads++)
	{
		CQuadData QuadData;
		QuadData.Init(&pQuads[NumQuads]);
		QuadData.m_SubType = (uint8_t)SubType;

		if(SubType == ESubType::Area)
			AddAreaQuad(QuadData); // these decide who the zone owns, see IMinigame::ContainsPlayer
		else
			AddQuad(QuadData);

		if(SubType == ESubType::Wheel)
		{
			if(m_HasWheel)
			{
				log_info("roulette", "Skipping duplicate wheel on map %" PRIzu, MultiMapIndex());
				continue;
			}

			m_WheelPos = QuadData.m_aPoints[4]; // pivot
			m_HasWheel = true;
			m_SnapId = AllocSnapId();
			SetState(EState::Idle);
			PrepareNextSpin();

			log_info("roulette", "Roulette created at %.2f, %.2f on map %" PRIzu, m_WheelPos.x, m_WheelPos.y, MultiMapIndex());
		}

		if(SubType == ESubType::BetOption)
		{
			const char *pBetOptionStr = aLayerName + str_length("Bet_");
			if(!pBetOptionStr[0])
				continue;

			for(size_t Type = 0; Type < std::size(RouletteOptions); Type++)
			{
				if(!str_comp(pBetOptionStr, RouletteOptions[Type]))
				{
					CBetQuadData BetQuadData;
					BetQuadData.m_BetOption = (int)Type;
					BetQuadData.m_MapIndex = MultiMapIndex();
					for(size_t i = 0; i < std::size(BetQuadData.m_Pos); i++)
						BetQuadData.m_Pos[i] = QuadData.m_aPoints[i];

					m_vBetQuads.push_back(BetQuadData);
					break;
				}
			}
		}
	}
}

bool CRouletteZone::ContainsPlayer(const CPlayer *pPlayer) const
{
	const CCharacter *pChr = pPlayer->GetCharacter();
	// Being frozen never moves a player in or out, they cant act on the table either way
	if(pChr && pChr->IsAlive() && pChr->Core()->m_IsInFreeze)
		return IsInArea(pPlayer->GetCid());

	return IMinigame::ContainsPlayer(pPlayer);
}

const char *CRouletteZone::Motd() const
{
	return "\n"
	       "[Viewable in Server info tab]\n"
	       "\n"
	       "\n"
	       "--  Rᴏᴜʟᴇᴛᴛᴇ  --\n"
	       "\n"
	       "To start, write '/bet <amount>', after that you can select your bet type by hovering your mouse over any of the options below and hammering\n"
	       "\n"
	       "Pᴀʏᴏᴜᴛs:\n"
	       "Black | Red: 2x\n"
	       "3x dozens: 3x\n"
	       "Green [Zero]: 14x\n"
	       "\n"
	       "[Press Tab to hide]";
}

void CRouletteZone::OnPlayerEnter(int ClientId)
{
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr)
		return;
	m_aClients[ClientId].m_PrevPassive = pChr->Core()->m_Passive;
	pChr->SetPassive(true);
}

void CRouletteZone::OnPlayerLeave(int ClientId)
{
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr)
		return;
	pChr->SetPassive(m_aClients[ClientId].m_PrevPassive);
}

void CRouletteZone::OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg)
{
	m_aClients[ClientId].m_PrevPassive = false;
}

bool CRouletteZone::CanJoinRound(int ClientId) const
{
	if(m_State != EState::Idle && m_State != EState::Preparing)
		return false;
	if(m_aClients[ClientId].m_Active)
		return false;

	return true;
}

bool CRouletteZone::CanUseMoney(CPlayer *pPlayer)
{
	// Money on the table cannot be spent anywhere else, even after walking out of the area
	return !ClientBetting(pPlayer->GetCid());
}

int CRouletteZone::AmountOfCloseClients() const
{
	int Count = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;
		if(pPlayer->IsAfk())
			continue;
		if(pPlayer->MultiMapIdx() != (int)MultiMapIndex())
			continue;
		if(!pPlayer->Acc()->m_LoggedIn)
			continue;
		if(!pPlayer->Acc()->m_Money)
			continue;
		CCharacter *pCharacter = pPlayer->GetCharacter();
		if(!pCharacter)
			continue;
		if(distance(pCharacter->m_Pos, m_WheelPos) > 32.0f * 13.0f)
			continue;
		if(pCharacter->Team() != TEAM_FLOCK)
			continue;

		Count++;
	}
	return Count;
}

bool CRouletteZone::AddClient(int ClientId, const char *pBetOption)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	const int64_t BetAmount = pPlayer->m_Wager;

	if(!m_HasWheel)
		return false;

	if(!IsInArea(ClientId))
		return false;

	if(!g_Config.m_SvAccounts)
	{
		GameServer()->SendChatTarget(ClientId, "Feature is disabled.");
		return false;
	}

	if(!CanJoinRound(ClientId))
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

	SetState(EState::Preparing);
	pPlayer->TakeMoney(BetAmount, true);

	m_aClients[ClientId].m_UsedWager = BetAmount;
	str_copy(m_aClients[ClientId].m_aBetOption, pBetOption);
	m_aClients[ClientId].m_Active = true;

	m_Betters++;
	m_TotalWager += BetAmount;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "You placed a bet of %" PRId64 " on %s", BetAmount, pBetOption);
	GameServer()->SendChatTarget(ClientId, aBuf);
	return true;
}

void CRouletteZone::OnClientReset(int ClientId)
{
	if(m_State == EState::Idle)
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

void CRouletteZone::SetState(EState State)
{
	if(m_State == State)
		return;

	m_State = State;

	if(State == EState::Preparing)
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

void CRouletteZone::PrepareNextSpin()
{
	static std::random_device Rd;
	static std::mt19937 Rng(Rd());
	std::uniform_int_distribution<> DurationDist(MIN_SPIN_DURATION, MAX_SPIN_DURATION);
	std::uniform_real_distribution<> SlowDist(0.35f, 0.7f);

	m_SpinDuration = DurationDist(Rng);
	m_SlowDownFactor = SlowDist(Rng);

	float EndRotation = 0.0f;
	int StoppingTicks = 0;
	m_EndingField = CalculateEndingField(m_SpinDuration, m_SlowDownFactor, &EndRotation, &StoppingTicks);

	// The physics comes to rest wherever it happens to, which is often right on the seam between two
	// tiles. Spread the gap to the middle of the winning tile across the slowdown instead, so the
	// pointer eases onto the centre rather than snapping there once it has already stopped.
	float Offset = fmodf(FieldCenterRotation(m_EndingField) - EndRotation, TwoPi);
	if(Offset > pi)
		Offset -= TwoPi;
	else if(Offset < -pi)
		Offset += TwoPi;

	// Never more than half a tile: whatever happens, the correction must not walk off the winner
	Offset = std::clamp(Offset, -HalfField, HalfField);

	m_LandingCorrection = StoppingTicks > 0 ? Offset / StoppingTicks : 0.0f;
}

int CRouletteZone::GetField(float Rotation) const
{
	float rot = fmodf(Rotation, TwoPi);
	if(rot < 0.f)
		rot += TwoPi;

	float rel = AngleTop - 1 * rot;
	rel = fmodf(rel, TwoPi);
	if(rel < 0.f)
		rel += TwoPi;
	rel += HalfField + FineTune;
	if(rel >= TwoPi)
		rel -= TwoPi;

	int index = static_cast<int>(rel / FieldAngle) % MAX_FIELDS;

	index = (index + SectorOffset + MAX_FIELDS) % MAX_FIELDS;

	if(HorizontalFlip)
		index = (MAX_FIELDS - index) % MAX_FIELDS;

	return index;
}

float CRouletteZone::FieldCenterRotation(int Field) const
{
	// Undo GetField() step by step: back through the flip and the offset to the raw sector, then to the
	// rotation that puts the middle of that sector under the pointer
	int Index = HorizontalFlip ? (MAX_FIELDS - Field) % MAX_FIELDS : Field;
	Index = ((Index - SectorOffset) % MAX_FIELDS + MAX_FIELDS) % MAX_FIELDS;

	float Rotation = fmodf(AngleTop + FineTune - Index * FieldAngle, TwoPi);
	if(Rotation < 0.f)
		Rotation += TwoPi;

	return Rotation;
}

int CRouletteZone::GetField() const
{
	return GetField(m_Rotation);
}

int CRouletteZone::CalculateEndingField(int SpinDuration, float SlowDownFactor, float *pEndRotation, int *pStoppingTicks) const
{
	float Rotation = m_Rotation;
	float RotationSpeed = m_RotationSpeed;
	EState State = EState::Spinning;
	bool FirstSpinTick = true;
	int StoppingTicks = 0;

	while(true)
	{
		if((State == EState::Spinning || State == EState::Stopping) && !FirstSpinTick)
			SpinDuration--;

		if(State == EState::Spinning)
		{
			RotationSpeed += 0.005f;
			if(RotationSpeed > 0.5f)
				RotationSpeed = 0.5f;
		}
		else if(State == EState::Stopping)
		{
			RotationSpeed -= 0.005f * SlowDownFactor;
			if(RotationSpeed < 0.0f)
			{
				if(pEndRotation)
					*pEndRotation = Rotation;
				if(pStoppingTicks)
					*pStoppingTicks = StoppingTicks;
				return GetField(Rotation);
			}
			// Counts only the ticks that still turn the wheel, the same ones OnTick corrects on
			StoppingTicks++;
		}

		Rotation += RotationSpeed;

		if(SpinDuration <= 0 && State == EState::Spinning)
			State = EState::Stopping;

		FirstSpinTick = false;
	}
}

void CRouletteZone::ClearClientBet(int ClientId, bool Refund)
{
	if(m_aClients[ClientId].m_Active)
	{
		if(Refund)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(pPlayer && pPlayer->Acc()->m_LoggedIn)
				pPlayer->GiveMoney(m_aClients[ClientId].m_UsedWager, false, true);
		}

		m_TotalWager -= m_aClients[ClientId].m_UsedWager;
		if(m_TotalWager < 0)
			m_TotalWager = 0;

		if(m_Betters > 0)
			m_Betters--;
	}

	m_aClients[ClientId].m_UsedWager = -1;
	m_aClients[ClientId].m_aBetOption[0] = '\0';
	m_aClients[ClientId].m_Active = false;
}

void CRouletteZone::EvaluateBet(int ClientId, bool Silent)
{
	if(!CheckClientId(ClientId))
		return;
	if(!m_aClients[ClientId].m_Active)
		return;
	if(m_EndingField < 0)
		return;

	const int WinningField = m_EndingField;
	const int Color = s_aFields[WinningField].m_Color;
	const int Number = s_aFields[WinningField].m_Number;
	const int64_t Amount = m_aClients[ClientId].m_UsedWager;

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

void CRouletteZone::StartSpin()
{
	if(m_State != EState::Preparing)
		return;

	m_StartDelay = -1;
	SetState(EState::Spinning);
}

void CRouletteZone::OnTick()
{
	if(!m_HasWheel)
		return;

	if(m_State == EState::Spinning || m_State == EState::Stopping)
		m_SpinDuration--;
	if(m_StartDelay > 0)
		m_StartDelay--;
	else if(m_State == EState::Preparing && m_StartDelay == 0)
		StartSpin();

	if(m_State == EState::Spinning)
	{
		m_RotationSpeed += 0.005f;
		if(m_RotationSpeed > 0.5f)
			m_RotationSpeed = 0.5f;
	}
	else if(m_State == EState::Stopping)
	{
		m_RotationSpeed -= 0.005f * m_SlowDownFactor;
		if(m_RotationSpeed < 0.0f)
		{
			m_RotationSpeed = 0.0f;
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
				EvaluateBet(ClientId);
			SetState(EState::Idle);
			PrepareNextSpin(); // sets m_LandingCorrection for the next spin, nothing may clear it after
		}
		else
		{
			// Eases the pointer onto the middle of the winning tile instead of resting on a seam
			m_Rotation += m_LandingCorrection;
		}
	}
	m_Rotation += m_RotationSpeed;

	if(m_SpinDuration <= 0)
	{
		if(m_State == EState::Spinning)
			SetState(EState::Stopping);
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		SendBroadcast(ClientId);
}

void CRouletteZone::SendBroadcast(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!IsInArea(ClientId))
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || pChr->Team() != TEAM_FLOCK)
		return;

	float TimeLeft = (float)m_StartDelay / (float)Server()->TickSpeed();
	char aBuf[32];

	std::vector<std::string> Messages;

	if(pPlayer->Acc()->m_LoggedIn)
	{
		str_format(aBuf, sizeof(aBuf), "%" PRId64 "%s", pPlayer->Acc()->m_Money, g_Config.m_SvCurrencyName);
		Messages.push_back(aBuf);

		if(m_State == EState::Idle)
		{
			if(pPlayer->m_Wager <= 0)
				str_copy(aBuf, "Wager: Nothing");
			else
				str_format(aBuf, sizeof(aBuf), "Wager: %" PRId64, pPlayer->m_Wager);
			Messages.push_back(aBuf);
		}
	}

	if(m_State != EState::Idle)
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

void CRouletteZone::OnSnap(int SnappingClient)
{
	if(!m_HasWheel || m_SnapId < 0)
		return;

	if(NetworkClipped(GameServer(), SnappingClient, m_WheelPos))
		return;

	const int SnappingClientVersion = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	const float RouletteLength = g_Config.m_SvRouletteLength;
	const vec2 From = m_WheelPos + direction(m_Rotation) * RouletteLength;

	GameServer()->SnapLaserObject(CSnapContext(SnappingClientVersion, SixUp, SnappingClient), m_SnapId, From, m_WheelPos, 0, -1, LASERTYPE_DRAGGER, LASERDRAGGERTYPE_WEAK, -1, LASERFLAG_NO_PREDICT);
}
