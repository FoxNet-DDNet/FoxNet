#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ROULETTE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ROULETTE_H

#include "minigame.h"

#include <base/vmath.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include <cstdint>
#include <vector>

class CBetQuadData
{
public:
	int m_MapIndex = 0;
	vec2 m_Pos[4] = {vec2(0, 0)};
	// Based on RouletteOptions in roulette.h
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

	bool m_CreatedWheel = false;
	std::vector<CBetQuadData> m_vBetQuads;

public:
	CRouletteZone(CGameContext *pGameContext, size_t MapIndex) :
		IMinigame(pGameContext, MapIndex) {}
	const std::vector<CBetQuadData> &BetQuads() const { return m_vBetQuads; }

	void Init(CMapItemLayerQuads *pQuadsLayer) override;
	void OnTick() override;

	[[nodiscard]] bool ContainsPlayer(const CPlayer *pPlayer) const override;
	[[nodiscard]] const char *Motd() const override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ROULETTE_H
