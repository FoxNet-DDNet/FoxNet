#include "moneywheel.h"

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
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

/*
 * The whole game, in one table.
 *
 * Adding, removing or reweighting a bet is a line in here: the wheel resizes itself, the odds follow,
 * the motd payout list follows, and the icons follow. The only other place that needs touching is
 * DrawIcon(), and only if an icon is used that does not exist yet.
 *
 * The odds: every option's m_NumFields * m_Multiplier is 24 against a 26 field wheel, so all four
 * bets return the same 24/26 = 92%. Equal on purpose, otherwise one bet is simply the correct one to
 * always make.
 *
 * Keep that product equal if you retune this, and be aware it decides the wheel size for you. Flat
 * odds mean N = K * sum(1/multiplier) with K a common multiple of every multiplier, so the payouts
 * and the field count cannot be picked independently. 2/3/6/12 gives K=24 and N=26; the same wheel
 * with a 5x and a 10x on it has no solution below 34 fields.
 */
static const CMoneyWheelOption s_aOptions[] = {
	{"2x", 2, 12, EMoneyWheelIcon::LaserDoor},
	{"3x", 3, 8, EMoneyWheelIcon::LaserFreeze},
	{"6x", 6, 4, EMoneyWheelIcon::PickupShield},
	{"12x", 12, 2, EMoneyWheelIcon::PickupHeart},
};

CMoneyWheelZone::~CMoneyWheelZone()
{
	// The wheel is going away with the map, nobody gets to keep a stake on it
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		ClearClientBet(ClientId, true);
}

void CMoneyWheelZone::BuildFields()
{
	int TotalFields = 0;
	for(const CMoneyWheelOption &Option : s_aOptions)
		TotalFields += Option.m_NumFields;

	m_vFields.clear();
	m_vFields.reserve(TotalFields);

	int aPlaced[std::size(s_aOptions)] = {0};
	for(int Slot = 0; Slot < TotalFields; Slot++)
	{
		// Always place whichever option has fallen furthest behind its share of the wheel so far.
		// That spreads equal options as evenly around the ring as the counts allow, instead of
		// leaving all three 10x fields sitting next to each other.
		int Best = -1;
		float BestScore = 0.0f;
		for(size_t Option = 0; Option < std::size(s_aOptions); Option++)
		{
			if(aPlaced[Option] >= s_aOptions[Option].m_NumFields)
				continue;

			const float Score = (float)(Slot + 1) * s_aOptions[Option].m_NumFields / TotalFields - aPlaced[Option];
			if(Best == -1 || Score > BestScore)
			{
				BestScore = Score;
				Best = (int)Option;
			}
		}

		if(Best < 0)
			break;

		m_vFields.push_back(Best);
		aPlaced[Best]++;
	}
}

void CMoneyWheelZone::BuildMotd()
{
	m_Motd =
		"[Viewable in Server info tab]\n"
		"\n"
		"\n"
		"--  Mᴏɴᴇʏ Wʜᴇᴇʟ  --\n"
		"\n"
		"To start, write '/bet <amount>', after that you can select your bet type by hovering your mouse over any of the options below and hammering\n"
		"\n"
		"Whatever ends up at the top of the wheel wins.\n"
		"\n"
		"Pᴀʏᴏᴜᴛs:\n";

	char aBuf[32];
	for(const CMoneyWheelOption &Option : s_aOptions)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d fields\n", Option.m_pName, Option.m_NumFields);
		m_Motd += aBuf;
	}

	str_format(aBuf, sizeof(aBuf), "\n%d total fields\n", (int)m_vFields.size());
	m_Motd += aBuf;


	m_Motd += "\n[Press Tab to hide]";
}

void CMoneyWheelZone::Init(CMapItemLayerQuads *pQuadsLayer)
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
		SubType = ESubType::BetOption;
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
				log_info("money-wheel", "Skipping duplicate wheel on map %" PRIzu, MultiMapIndex());
				continue;
			}

			m_WheelPos = QuadData.m_aPoints[4]; // pivot
			// The mapper sizes the ring by how wide they draw the quad, no config var needed
			m_Radius = distance(QuadData.m_aPoints[0], QuadData.m_aPoints[1]) * 0.5f;
			m_HasWheel = true;

			BuildFields();
			BuildMotd();

			CWheelSpin::CConfig Config;
			Config.m_NumFields = (int)m_vFields.size();

			Config.m_Acceleration = 0.007f;
			Config.m_MinSpinTicks = 1.5 * SERVER_TICK_SPEED;
			Config.m_MaxSpinTicks = 2.5 * SERVER_TICK_SPEED;

			m_Spin.Init(Config);

			m_vSnapIds.reserve(m_vFields.size());
			for(size_t Field = 0; Field < m_vFields.size(); Field++)
				m_vSnapIds.push_back(AllocSnapId());

			log_info("money-wheel", "Money wheel created at %.2f, %.2f with %" PRIzu " fields, radius %.0f, on map %" PRIzu,
				m_WheelPos.x, m_WheelPos.y, m_vFields.size(), m_Radius, MultiMapIndex());
		}

		if(SubType == ESubType::BetOption)
		{
			const char *pBetOptionStr = aLayerName + str_length("Bet_");
			if(!pBetOptionStr[0])
				continue;

			for(size_t Option = 0; Option < std::size(s_aOptions); Option++)
			{
				if(str_comp(pBetOptionStr, s_aOptions[Option].m_pName))
					continue;

				CBetQuadData BetQuadData;
				BetQuadData.m_BetOption = (int)Option;
				BetQuadData.m_MapIndex = MultiMapIndex();
				for(size_t i = 0; i < std::size(BetQuadData.m_Pos); i++)
					BetQuadData.m_Pos[i] = QuadData.m_aPoints[i];
				// Point 4 is the pivot, OnSnap draws this option's icon on it
				BetQuadData.m_Pivot = QuadData.m_aPoints[4];
				BetQuadData.m_SnapId = AllocSnapId();

				m_vBetQuads.push_back(BetQuadData);
				break;
			}
		}
	}
}

int CMoneyWheelZone::OptionOfField(int Field) const
{
	if(Field < 0 || Field >= (int)m_vFields.size())
		return -1;

	return m_vFields[Field];
}

const char *CMoneyWheelZone::FieldName(int Field) const
{
	const int Option = OptionOfField(Field);
	return Option < 0 ? "" : s_aOptions[Option].m_pName;
}

int CMoneyWheelZone::FieldMultiplier(int Field) const
{
	const int Option = OptionOfField(Field);
	return Option < 0 ? 0 : s_aOptions[Option].m_Multiplier;
}

bool CMoneyWheelZone::ContainsPlayer(const CPlayer *pPlayer) const
{
	const CCharacter *pChr = pPlayer->GetCharacter();
	// Being frozen never moves a player in or out, they cant act on the wheel either way
	if(pChr && pChr->IsAlive() && pChr->Core()->m_IsInFreeze)
		return IsInArea(pPlayer->GetCid());

	return IMinigame::ContainsPlayer(pPlayer);
}

const char *CMoneyWheelZone::Motd() const
{
	return m_Motd.c_str();
}

void CMoneyWheelZone::OnPlayerEnter(int ClientId)
{
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr)
		return;

	// Nobody gets to hook players around while they are standing at the wheel
	m_aClients[ClientId].m_PrevPassive = pChr->Core()->m_Passive;
	pChr->SetPassive(true);
}

void CMoneyWheelZone::OnPlayerLeave(int ClientId)
{
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientId);
	if(!pChr)
		return;

	pChr->SetPassive(m_aClients[ClientId].m_PrevPassive);
}

void CMoneyWheelZone::OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg)
{
	// Passive does not survive a death, so there is nothing left to hand back
	m_aClients[ClientId].m_PrevPassive = false;
}

void CMoneyWheelZone::OnClientDrop(int ClientId, const char *pReason)
{
	m_aClients[ClientId].m_PrevPassive = false;
}

bool CMoneyWheelZone::CanJoinRound(int ClientId) const
{
	if(!m_Spin.Idle())
		return false;
	if(m_aClients[ClientId].m_Active)
		return false;

	return true;
}

bool CMoneyWheelZone::CanUseMoney(CPlayer *pPlayer)
{
	// Money on the wheel cannot be spent anywhere else, even after walking out of the area
	return !ClientBetting(pPlayer->GetCid());
}

int CMoneyWheelZone::AmountOfCloseClients() const
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

void CMoneyWheelZone::BeginCountdown()
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

bool CMoneyWheelZone::OnCharacterFire(CCharacter *pChr, int Weapon)
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

		if(AddClient(ClientId, s_aOptions[QuadData.m_BetOption].m_pName))
		{
			GameServer()->CreateDeath(CursorPos, ClientId, pChr->TeamMask());
			break;
		}
	}
	return true;
}

bool CMoneyWheelZone::AddClient(int ClientId, const char *pBetOption)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	if(!m_HasWheel)
		return false;

	if(!IsInArea(ClientId))
		return false;

	const int64_t BetAmount = pPlayer->m_Wager;

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

	int Option = -1;
	for(size_t i = 0; i < std::size(s_aOptions); i++)
	{
		if(!str_comp(s_aOptions[i].m_pName, pBetOption))
		{
			Option = (int)i;
			break;
		}
	}
	if(Option < 0)
	{
		GameServer()->SendChatTarget(ClientId, "Something went wrong, please try again!");
		return false;
	}

	BeginCountdown();
	pPlayer->TakeMoney(BetAmount, true);

	m_aClients[ClientId].m_UsedWager = BetAmount;
	m_aClients[ClientId].m_BetOption = Option;
	m_aClients[ClientId].m_Active = true;

	m_Betters++;
	m_TotalWager += BetAmount;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "You placed a bet of %" PRId64 " on %s", BetAmount, s_aOptions[Option].m_pName);
	GameServer()->SendChatTarget(ClientId, aBuf);
	return true;
}

void CMoneyWheelZone::ClearClientBet(int ClientId, bool Refund)
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
	m_aClients[ClientId].m_BetOption = -1;
	m_aClients[ClientId].m_Active = false;
}

void CMoneyWheelZone::EvaluateBet(int ClientId, bool Silent)
{
	if(!CheckClientId(ClientId))
		return;
	if(!m_aClients[ClientId].m_Active)
		return;

	const int WinningOption = OptionOfField(m_Spin.EndingField());
	if(WinningOption < 0)
		return;

	if(m_aClients[ClientId].m_BetOption == WinningOption)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(pPlayer && pPlayer->Acc()->m_LoggedIn)
			pPlayer->GiveMoney(m_aClients[ClientId].m_UsedWager * s_aOptions[WinningOption].m_Multiplier, false, Silent);
	}

	ClearClientBet(ClientId);
}

void CMoneyWheelZone::OnClientReset(int ClientId)
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

void CMoneyWheelZone::OnTick()
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

void CMoneyWheelZone::SendBroadcast(int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(!IsInArea(ClientId))
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || pChr->Team() != TEAM_FLOCK)
		return;

	char aBuf[64];
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
			str_format(aBuf, sizeof(aBuf), "You bet on: %s", s_aOptions[m_aClients[ClientId].m_BetOption].m_pName);
			Messages.push_back(aBuf);
		}

		str_format(aBuf, sizeof(aBuf), "\nPlayers: %d", m_Betters);
		Messages.push_back(aBuf);
		str_format(aBuf, sizeof(aBuf), "Total Bets: %" PRId64 "\n", m_TotalWager);
		Messages.push_back(aBuf);
		if(m_StartDelay > 0)
		{
			str_format(aBuf, sizeof(aBuf), "Starting in: %.1fs", (float)m_StartDelay / (float)Server()->TickSpeed());
			Messages.push_back(aBuf);
		}
	}

	pPlayer->SendBroadcastHud(Messages, 2);
}

void CMoneyWheelZone::DrawIcon(int SnappingClient, int SnapId, vec2 Pos, EMoneyWheelIcon Icon) const
{
	if(SnapId < 0)
		return;

	if(NetworkClipped(GameServer(), SnappingClient, Pos))
		return;

	const CSnapContext Context(Server()->GetClientVersion(SnappingClient), Server()->IsSixup(SnappingClient), SnappingClient);

	switch(Icon)
	{
	case EMoneyWheelIcon::PickupHeart:
		GameServer()->SnapPickup(Context, SnapId, Pos, POWERUP_HEALTH, 0, -1, PICKUPFLAG_NO_PREDICT);
		break;
	case EMoneyWheelIcon::PickupShield:
		GameServer()->SnapPickup(Context, SnapId, Pos, POWERUP_ARMOR, 0, -1, PICKUPFLAG_NO_PREDICT);
		break;
	case EMoneyWheelIcon::LaserFreeze:
		GameServer()->SnapLaserObject(Context, SnapId, Pos, Pos, Server()->Tick(), -1, LASERTYPE_FREEZE, -1, -1, LASERFLAG_NO_PREDICT);
		break;
	case EMoneyWheelIcon::LaserDoor:
		GameServer()->SnapLaserObject(Context, SnapId, Pos, Pos, Server()->Tick(), -1, LASERTYPE_DOOR, -1, -1, LASERFLAG_NO_PREDICT);
		break;
	}
}

void CMoneyWheelZone::DrawField(int SnappingClient, int Field) const
{
	const int Option = OptionOfField(Field);
	if(Option < 0)
		return;

	const float Angle = m_Spin.FieldDrawAngle(Field);
	DrawIcon(SnappingClient, m_vSnapIds[Field], m_WheelPos + direction(Angle) * m_Radius, s_aOptions[Option].m_Icon);
}

void CMoneyWheelZone::OnSnap(int SnappingClient)
{
	if(!m_HasWheel)
		return;

	for(size_t Field = 0; Field < m_vFields.size() && Field < m_vSnapIds.size(); Field++)
		DrawField(SnappingClient, (int)Field);

	// The same icon on the patch you hammer, so it is obvious which one on the wheel it is betting on
	for(const CBetQuadData &BetQuad : m_vBetQuads)
	{
		if(BetQuad.m_BetOption < 0)
			continue;

		DrawIcon(SnappingClient, BetQuad.m_SnapId, BetQuad.m_Pivot, s_aOptions[BetQuad.m_BetOption].m_Icon);
	}
}
