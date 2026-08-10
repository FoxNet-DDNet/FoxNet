#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_MONEYWHEEL_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_MONEYWHEEL_H

#include "../minigame.h"
#include "betquad.h"
#include "wheelspin.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include <cstdint>
#include <string>
#include <vector>

/*
 * What a field looks like in game. Add one here and give it a case in DrawField(), that is the only
 * place that knows how an icon is actually snapped.
 */
enum class EMoneyWheelIcon
{
	PickupHeart = 0,
	PickupShield,
	LaserFreeze,
	LaserDoor,
};

/*
 * One bet: how much it pays, how many of the wheel's fields carry it, and what those fields look
 * like. Everything else follows from this, including the wheel size and the payout list in the motd.
 */
class CMoneyWheelOption
{
public:
	const char *m_pName; // matches the "Bet_<name>" quad layer, and what the player sees in chat
	int m_Multiplier; // payout including the stake, so 2 doubles the money
	int m_NumFields;
	EMoneyWheelIcon m_Icon;
};

class CMoneyWheelZone : public IMinigame
{
	enum class ESubType : uint8_t
	{
		Area = 0,
		Wheel,
		BetOption,
	};

	class CClientData
	{
	public:
		int64_t m_UsedWager = 0;
		int m_BetOption = -1; // index into the option table, -1 for no bet
		bool m_Active = false;

		bool m_PrevPassive = false;
	};

	CWheelSpin m_Spin;

	bool m_HasWheel = false;
	vec2 m_WheelPos = vec2(0, 0);
	float m_Radius = 0.0f;
	std::vector<int> m_vSnapIds; // one per field, index matches m_vFields

	// Which option sits on each field, built from the table so no field count is hardcoded
	std::vector<int> m_vFields;

	// Counts down to the next spin once somebody has bet, the wheel itself stays Idle meanwhile
	int m_StartDelay = -1;

	int m_Betters = 0;
	int64_t m_TotalWager = 0;

	std::vector<CBetQuadData> m_vBetQuads;
	CClientData m_aClients[MAX_CLIENTS];

	// Built from the option table so the payout list can never disagree with what the wheel pays
	std::string m_Motd;

	void BuildFields();
	void BuildMotd();
	void BeginCountdown();
	void ClearClientBet(int ClientId, bool Refund = false);
	void EvaluateBet(int ClientId, bool Silent = false);

	bool CanJoinRound(int ClientId) const;
	int AmountOfCloseClients() const;
	void SendBroadcast(int ClientId);
	void DrawIcon(int SnappingClient, int SnapId, vec2 Pos, EMoneyWheelIcon Icon) const;
	void DrawField(int SnappingClient, int Field) const;

	[[nodiscard]] int OptionOfField(int Field) const;

public:
	CMoneyWheelZone(CGameContext *pGameContext, size_t MapIndex) :
		IMinigame(pGameContext, MapIndex) {}
	~CMoneyWheelZone() override;

	const std::vector<CBetQuadData> &BetQuads() const { return m_vBetQuads; }

	void Init(CMapItemLayerQuads *pQuadsLayer) override;
	void OnTick() override;
	void OnSnap(int SnappingClient) override;

	[[nodiscard]] bool ContainsPlayer(const CPlayer *pPlayer) const override;
	[[nodiscard]] const char *Motd() const override;

	void OnPlayerEnter(int ClientId) override;
	void OnPlayerLeave(int ClientId) override;
	void OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg) override;

	void OnClientReset(int ClientId) override;
	void OnClientDrop(int ClientId, const char *pReason) override;
	bool CanUseMoney(CPlayer *pPlayer) override;

	// A wheel with no centre takes nothing, that map is broken
	[[nodiscard]] bool TakesWager() const override { return m_HasWheel; }

	bool OnCharacterFire(CCharacter *pChr, int Weapon) override;

	bool ClientBetting(int ClientId) const { return m_aClients[ClientId].m_Active; }
	// Bets whatever /bet set aside on the given option name
	bool AddClient(int ClientId, const char *pBetOption);

	int EndingField() const { return m_Spin.EndingField(); }
	// Name and payout of whatever sits on a field, for the reveal command
	const char *FieldName(int Field) const;
	int FieldMultiplier(int Field) const;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_MONEYWHEEL_H
