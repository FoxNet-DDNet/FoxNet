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

const SFields CRouletteZone::s_aFields[NUM_FIELDS] = {
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
			m_Radius = distance(QuadData.m_aPoints[0], QuadData.m_aPoints[1]) * 0.5f;
			m_HasWheel = true;
			m_SnapId = AllocSnapId();

			CWheelSpin::CConfig Config;
			Config.m_NumFields = NUM_FIELDS;
			// The map art runs the other way round the pointer
			Config.m_Reverse = true;
			m_Spin.Init(Config);

			log_info("roulette-wheel", "Roulette created at %.2f, %.2f, radius %.0f, on map %" PRIzu, m_WheelPos.x, m_WheelPos.y, m_Radius, MultiMapIndex());
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
	if(!m_Spin.Idle())
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
		if(!IsInArea(ClientId))
			continue;
		CCharacter *pCharacter = pPlayer->GetCharacter();
		if(!pCharacter)
			continue;
		if(pCharacter->Team() != TEAM_FLOCK)
			continue;

		Count++;
	}
	return Count;
}

bool CRouletteZone::OnCharacterFire(CCharacter *pChr, int Weapon)
{
	if(pChr->Team() != TEAM_FLOCK)
		return true;
	if(!pChr->IsAlive())
		return true;
	if(Weapon != WEAPON_HAMMER)
		return true;

	const int ClientId = pChr->GetPlayer()->GetCid();
	if(!IsInArea(ClientId))
		return true;

	vec2 CursorPos = pChr->GetCursorPos();

	for(const CBetQuadData &QuadData : BetQuads())
	{
		const vec2 aPoints[4] = {QuadData.m_Pos[0], QuadData.m_Pos[1], QuadData.m_Pos[2], QuadData.m_Pos[3]};
		if(!InsideQuadrilateral(CursorPos, aPoints))
			continue;

		if(AddClient(ClientId, RouletteOptions[QuadData.m_BetOption]))
		{
			GameServer()->CreateDeath(CursorPos, ClientId, pChr->TeamMask());
			break;
		}
	}
	return true;
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

	BeginCountdown();
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

void CRouletteZone::BeginCountdown()
{
	if(m_StartDelay >= 0) // already counting down
		return;
	if(!m_Spin.Idle())
		return;

	const int Close = AmountOfCloseClients();
	if(Close > 3)
		m_StartDelay = Server()->TickSpeed() * 7.5f; // 7.5 seconds
	else if(Close > 1)
		m_StartDelay = Server()->TickSpeed() * 5; // 5 seconds
	else
		m_StartDelay = Server()->TickSpeed() * 1.5f; // 1.5 seconds
}

void CRouletteZone::OnClientReset(int ClientId)
{
	if(m_Spin.Idle())
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
	const int WinningField = m_Spin.EndingField();
	if(WinningField < 0)
		return;

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

void CRouletteZone::OnTick()
{
	if(!m_HasWheel)
		return;

	if(m_StartDelay > 0)
	{
		m_StartDelay--;
	}
	else if(m_StartDelay == 0)
	{
		m_StartDelay = -1;
		m_Spin.Start();
	}

	if(m_Spin.Tick())
	{
		// EndingField() still holds this spin's winner, PrepareNext() rolls the following one
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			EvaluateBet(ClientId);
		m_Spin.PrepareNext();
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

		if(m_Spin.Idle())
		{
			if(pPlayer->m_Wager <= 0)
				str_copy(aBuf, "Wager: Nothing");
			else
				str_format(aBuf, sizeof(aBuf), "Wager: %" PRId64, pPlayer->m_Wager);
			Messages.push_back(aBuf);
		}
	}

	if(!m_Spin.Idle() || m_StartDelay >= 0)
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

	const float RouletteLength = m_Radius;
	const vec2 From = m_WheelPos + direction(m_Spin.Rotation()) * RouletteLength;

	GameServer()->SnapLaserObject(CSnapContext(SnappingClientVersion, SixUp, SnappingClient), m_SnapId, From, m_WheelPos, 0, -1, LASERTYPE_DRAGGER, LASERDRAGGERTYPE_WEAK, -1, LASERFLAG_NO_PREDICT);
}
