#include "roulette.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/foxnet/entities/roulette.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <cstdint>
#include <iterator>

void CRouletteZone::OnTick()
{
	if(!m_CreatedWheel)
	{
		for(const CQuadData &QuadData : Quads())
		{
			if(QuadData.m_SubType == (uint8_t)ESubType::Wheel)
			{
				if(!GameServer()->m_World.FindEntityOnMap(CGameWorld::ENTTYPE_ROULETTE, MultiMapIndex()))
				{
					log_info("roulette", "Roulette created at %.2f, %.2f on map %" PRIzu, QuadData.m_Pos[4].x, QuadData.m_Pos[4].y, MultiMapIndex());

					new CRoulette(&GameServer()->m_World, MultiMapIndex(), QuadData.m_Pos[4]);
					m_CreatedWheel = true;
				}
				else
					log_info("roulette", "Skipping duplicate wheel on map %" PRIzu, MultiMapIndex());
				break;
			}
		}
	}

	for(const CQuadData &QuadData : Quads())
	{
		if(QuadData.m_SubType != (uint8_t)ESubType::Area)
			continue;

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(!pPlayer || !pPlayer->GetCharacter())
				continue;
			if(pPlayer->MultiMapIdx() != (int)MultiMapIndex())
				continue;
			CCharacter *pChr = pPlayer->GetCharacter();
			if(!pChr->IsAlive())
				continue;

			pChr->m_InsideQuadFreeze = false;
			if(pChr->Core()->m_IsInFreeze)
				continue;

			vec2 Points[4] = {QuadData.m_Pos[0], QuadData.m_Pos[1], QuadData.m_Pos[3], QuadData.m_Pos[2]};
			if(InsideQuad(pChr->GetPos(), Points, vec2(0, 0)))
			{
				pPlayer->SetArea(EArea::Roulette);
			}
			else
			{
				if(pPlayer->m_Area == EArea::Roulette)
					pPlayer->SetArea(EArea::Game);
			}
		}
	}
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
		InitQuadData(QuadData, pQuadsLayer, &pQuads[NumQuads]);
		QuadData.m_Type = EZoneType::Roulette;
		QuadData.m_SubType = (uint8_t)SubType;
		AddQuad(QuadData);

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
						BetQuadData.m_Pos[i] = QuadData.m_Pos[i];

					m_vBetQuads.push_back(BetQuadData);
					break;
				}
			}
		}
	}
}
