#include "zonemanager.h"

#include "checktele.h"
#include "collidable.h"
#include "death.h"
#include "freeze.h"
#include "unfreeze.h"
#include "zone.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/map.h>

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

#include <algorithm>
#include <iterator>
#include <vector>
#include <game/quad_data.h>
#include "roulette.h"

void CZoneManager::OnMapLoad(size_t MultiMapIdx)
{
	int GroupsStart, LayersStart, GroupsNum, LayersNum;

	IMap *pMap = GameServer()->Map(MultiMapIdx);
	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(pMap->GetItem(GroupsStart + GroupIndex));
		char aGroupName[30];
		IntsToStr(pGroup->m_aName, std::size(pGroup->m_aName), aGroupName, std::size(aGroupName));

		// Only create one casino zone per map
		if(m_avpZones[(int)EZoneType::Roulette].empty() && !str_comp(aGroupName, "#Roulette"))
		{
			CRouletteZone *pZone = new CRouletteZone(GameServer(), MultiMapIdx);
			m_avpZones[(int)EZoneType::Roulette].push_back(pZone);
		}

		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(pMap->GetItem(LayersStart + pGroup->m_StartLayer + LayerIndex));

			if(pLayer->m_Type != LAYERTYPE_QUADS)
				continue;

			char aLayerName[30];
			CMapItemLayerQuads *pTilemap = reinterpret_cast<CMapItemLayerQuads *>(pLayer);
			IntsToStr(pTilemap->m_aName, std::size(pTilemap->m_aName), aLayerName, std::size(aLayerName));

			if(!str_comp(aGroupName, "#Roulette"))
			{
				m_avpZones[(int)EZoneType::Roulette].at(0)->Init(pTilemap);
			}
			else if(!str_comp("QFr", aLayerName))
			{
				CFreezeZone *pZone = new CFreezeZone(GameServer(), MultiMapIdx);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::Freeze].push_back(pZone);
			}
			else if(!str_comp("QUnFr", aLayerName))
			{
				CUnfreezeZone *pZone = new CUnfreezeZone(GameServer(), MultiMapIdx);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::Unfreeze].push_back(pZone);
			}
			else if(!str_comp("QDeath", aLayerName))
			{
				CDeathZone *pZone = new CDeathZone(GameServer(), MultiMapIdx);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::Death].push_back(pZone);
			}
			else if(!str_comp("QStopa", aLayerName))
			{
				CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, false);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::StopA].push_back(pZone);
			}
			// Solid tiles are handled in the collision code, so we don't need to create zones for them

			//else if(!str_comp("QHook", aLayerName))
			//{
			//	CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, QUADTYPE_HOOKABLE, true);
			//	pZone->Init(pTilemap);
			//	m_avpZones[(int)EZoneType::Hookable].push_back(pZone);
			//}
			//else if(!str_comp("QUnHook", aLayerName))
			//{
			//	CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, QUADTYPE_UNHOOKABLE, true);
			//	pZone->Init(pTilemap);
			//	m_avpZones[(int)EZoneType::Unhookable].push_back(pZone);
			//}
			else if(!str_comp("QCfrm", aLayerName))
			{
				CCheckpointFromZone *pZone = new CCheckpointFromZone(GameServer(), MultiMapIdx);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::CFRM].push_back(pZone);
			}
		}
	}
}

void CZoneManager::OnMapUnload(size_t MapIdx)
{
	// remove all zones that belong to the map that is being unloaded
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		vZones.erase(std::remove_if(vZones.begin(), vZones.end(), [MapIdx](IZone *pZone) {
			if(pZone->MultiMapIndex() == MapIdx)
			{
				delete pZone;
				return true;
			}
			return false;
		}),
			vZones.end());
	}
}

void CZoneManager::OnTick()
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			pZone->UpdateCache();
			pZone->OnTick();
		}
	}
}