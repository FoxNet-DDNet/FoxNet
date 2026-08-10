#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_ROULETTE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_ROULETTE_H

#include "../minigame.h"
#include "betquad.h"
#include "wheelspin.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include <cstdint>
#include <vector>
#include <game/server/entities/character.h>
#include <game/server/player.h>

constexpr int NUM_FIELDS = 37; // 0-36

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

class CRouletteZone : public IMinigame
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
		char m_aBetOption[24] = "";
		bool m_Active = false;

		bool m_PrevPassive = false;
	};

	// The wheel is drawn by this zone, it never needed to be an entity: it does not move through the
	// world, collides with nothing, and its whole appearance is the one laser below
	bool m_HasWheel = false;
	vec2 m_WheelPos = vec2(0, 0);
	float m_Radius = 0.0f;
	int m_SnapId = -1;

	CWheelSpin m_Spin;

	// Counts down to the next spin once somebody has bet, the wheel itself stays Idle meanwhile
	int m_StartDelay = -1;

	int m_Betters = 0;
	int64_t m_TotalWager = 0;

	std::vector<CBetQuadData> m_vBetQuads;

	CClientData m_aClients[MAX_CLIENTS];
	static const SFields s_aFields[NUM_FIELDS];

	void BeginCountdown();
	void ClearClientBet(int ClientId, bool Refund = false);

	void SendBroadcast(int ClientId);
	int AmountOfCloseClients() const;

	bool CanJoinRound(int ClientId) const;
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

	bool OnCharacterFire(CCharacter *pChr, int Weapon) override;

	bool ClientBetting(int ClientId) const { return m_aClients[ClientId].m_Active; }
	// Bets whatever /bet set aside
	bool AddClient(int ClientId, const char *pBetOption);

	int EndingField() const { return m_Spin.EndingField(); }
	int FieldNumber(int Field) const { return (Field >= 0 && Field < NUM_FIELDS) ? s_aFields[Field].m_Number : -1; }
	int FieldColor(int Field) const { return (Field >= 0 && Field < NUM_FIELDS) ? s_aFields[Field].m_Color : -1; }
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_ROULETTE_H
