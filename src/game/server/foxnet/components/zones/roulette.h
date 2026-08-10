#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ROULETTE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ROULETTE_H

#include "minigame.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include <cstdint>
#include <vector>

constexpr int MAX_FIELDS = 37; // 0-36
constexpr int MIN_SPIN_DURATION = 2 * SERVER_TICK_SPEED; // seconds
constexpr int MAX_SPIN_DURATION = 3 * SERVER_TICK_SPEED; // seconds

enum Colors
{
	COLOR_BLACK = 0,
	COLOR_RED = 1,
	COLOR_GREEN = 2
};

struct SFields
{
	int m_Number;
	int m_Color; // 0 = black, 1 = red, 2 = green
};

constexpr const char *RouletteOptions[] = {
	"Black",
	"Red",
	"Green",
	"Even",
	"Odd",
	"1-12",
	"13-24",
	"25-36"};

class CBetQuadData
{
public:
	int m_MapIndex = 0;
	vec2 m_Pos[4] = {vec2(0, 0)};
	// Based on RouletteOptions
	int m_BetOption = -1;
};

class CRouletteZone : public IMinigame
{
	enum class ESubType : uint8_t
	{
		Area = 0,
		Wheel,
		BetOption,
	};

	enum class EState
	{
		Idle = 0,
		Preparing,
		Spinning,
		Stopping,
	};

	class CClientData
	{
	public:
		int64_t m_UsedWager = 0;
		char m_aBetOption[24] = "";
		bool m_Active = false;

		bool m_PrevPassive = false;
	};

	// The wheel is drawn by this zone, it never needed to be an entity: it does not move through the
	// world, collides with nothing, and its whole appearance is the one laser below
	bool m_HasWheel = false;
	vec2 m_WheelPos = vec2(0, 0);
	int m_SnapId = -1;

	int m_SpinDuration = 0;
	float m_SlowDownFactor = 1.0f;
	float m_RotationSpeed = 0.0f;
	float m_Rotation = 0.0f;
	int m_EndingField = -1;
	EState m_State = EState::Idle;
	int m_StartDelay = -1;

	int m_Betters = 0;
	int64_t m_TotalWager = 0;

	std::vector<CBetQuadData> m_vBetQuads;

	CClientData m_aClients[MAX_CLIENTS];
	static const SFields s_aFields[MAX_FIELDS];

	void SetState(EState State);
	void PrepareNextSpin();
	int CalculateEndingField(int SpinDuration, float SlowDownFactor) const;
	void ClearClientBet(int ClientId, bool Refund = false);

	int GetField(float Rotation) const;
	int GetField() const;

	void SendBroadcast(int ClientId);
	int AmountOfCloseClients() const;

	bool CanJoinRound(int ClientId) const;
	void StartSpin();
	void EvaluateBet(int ClientId, bool Silent = false);

public:
	CRouletteZone(CGameContext *pGameContext, size_t MapIndex) :
		IMinigame(pGameContext, MapIndex) {}
	~CRouletteZone() override;

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
	bool CanUseMoney(CPlayer *pPlayer) override;

	// A table with no wheel takes nothing, that map is broken
	[[nodiscard]] bool TakesWager() const override { return m_HasWheel; }

	bool ClientBetting(int ClientId) const { return m_aClients[ClientId].m_Active; }
	// Bets whatever /bet set aside
	bool AddClient(int ClientId, const char *pBetOption);

	int EndingField() const { return m_EndingField; }
	int FieldNumber(int Field) const { return (Field >= 0 && Field < MAX_FIELDS) ? s_aFields[Field].m_Number : -1; }
	int FieldColor(int Field) const { return (Field >= 0 && Field < MAX_FIELDS) ? s_aFields[Field].m_Color : -1; }
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ROULETTE_H
