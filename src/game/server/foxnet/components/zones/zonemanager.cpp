#include "zonemanager.h"

#include "checktele.h"
#include "collidable.h"
#include "death.h"
#include "freeze.h"
#include "roulette.h"
#include "unfreeze.h"
#include "zone.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/map.h>

#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/gamecontext.h>

#include <algorithm>
#include <iterator>
#include <vector>
#include <game/server/entity.h>
#include <generated/protocol.h>
#include <base/vmath.h>
#include <engine/console.h>
#include <engine/shared/config.h>

IZone *CZoneManager::FindZoneByMapIndex(EZoneType Type, size_t MultiMapIdx)
{
	auto It = std::find_if(m_avpZones[(int)Type].begin(), m_avpZones[(int)Type].end(), [MultiMapIdx](const IZone *pZone) {
		return pZone->MultiMapIndex() == MultiMapIdx;
	});
	return It != m_avpZones[(int)Type].end() ? *It : nullptr;
}

void CZoneManager::OnMapLoad(size_t MultiMapIdx)
{
	OnMapUnload(MultiMapIdx);

	int GroupsStart, LayersStart, GroupsNum, LayersNum;

	IMap *pMap = GameServer()->Map(MultiMapIdx);
	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(pMap->GetItem(GroupsStart + GroupIndex));
		char aGroupName[30];
		IntsToStr(pGroup->m_aName, std::size(pGroup->m_aName), aGroupName, std::size(aGroupName));

		IZone *pGroupZone = nullptr;
		if(!str_comp(aGroupName, "#Roulette"))
		{
			pGroupZone = FindZoneByMapIndex(EZoneType::Roulette, MultiMapIdx);
			if(pGroupZone == nullptr)
			{
				pGroupZone = new CRouletteZone(GameServer(), MultiMapIdx);
				m_avpZones[(int)EZoneType::Roulette].push_back(pGroupZone);
			}
		}

		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(pMap->GetItem(LayersStart + pGroup->m_StartLayer + LayerIndex));

			if(pLayer->m_Type != LAYERTYPE_QUADS)
				continue;

			char aLayerName[30];
			CMapItemLayerQuads *pTilemap = reinterpret_cast<CMapItemLayerQuads *>(pLayer);
			IntsToStr(pTilemap->m_aName, std::size(pTilemap->m_aName), aLayerName, std::size(aLayerName));

			if(pGroupZone != nullptr)
			{
				pGroupZone->Init(pTilemap);
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
			if(!str_comp("QHook", aLayerName))
			{
				CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, true);
				// pZone->Init(pTilemap); we get the pointer from Collision()
				m_avpZones[(int)EZoneType::Hookable].push_back(pZone);
			}
			else if(!str_comp("QUnHook", aLayerName))
			{
				CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, true);
				// pZone->Init(pTilemap); we get the pointer from Collision()
				m_avpZones[(int)EZoneType::Unhookable].push_back(pZone);
			}
			else if(!str_comp("QCfrm", aLayerName))
			{
				CCheckpointFromZone *pZone = new CCheckpointFromZone(GameServer(), MultiMapIdx);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::CFRM].push_back(pZone);
			}
		}
	}

	if(m_DebugSnappingQuads)
	{
		FreeQuadIds();
		SnapQuadIds();
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

	if(m_DebugSnappingQuads)
	{
		FreeQuadIds();
		SnapQuadIds();
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

void CZoneManager::OnSnap(int SnappingClient, bool GlobalSnap, bool RecordingDemo)
{
	if(!m_DebugSnappingQuads)
		return;

	const int ClientVersion = Server()->GetClientVersion(SnappingClient);
	const bool Sixup = Server()->IsSixup(SnappingClient);

	size_t Idx = 0;
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			int SnappMultiMapIndex = GameServer()->GetMultiMapIdx(SnappingClient);

			if(pZone->MultiMapIndex() != SnappMultiMapIndex)
				continue;

			for(const CQuadData &Quad : pZone->Quads())
			{
				if(Idx > m_vIds.size() - 1)
					return;

				const int &Id = m_vIds[Idx];

				vec2 Pos = Quad.m_Pos[0];
				if(NetworkClipped(GameServer(), SnappingClient, Pos))
					continue;

				GameServer()->SnapLaserObject(CSnapContext(ClientVersion, Sixup, SnappingClient), Id, Pos, Pos, Server()->Tick(), -1, -1, -1, -1, LASERFLAG_NO_PREDICT);
				Idx++;
			}
		}
	}

	for(size_t MapIdx = 0; MapIdx < GameServer()->m_vMultiMaps.size(); MapIdx++)
	{
		if(MapIdx != (size_t)GameServer()->GetMultiMapIdx(SnappingClient))
			continue;
		for(const CQuadData &Quad : Collision(MapIdx)->Quads())
		{
			if(Idx > m_vIds.size() - 1)
				return;
			const int &Id = m_vIds[Idx];
			vec2 Pos = Quad.m_Pos[0];
			if(NetworkClipped(GameServer(), SnappingClient, Pos))
				continue;
			GameServer()->SnapLaserObject(CSnapContext(ClientVersion, Sixup, SnappingClient), Id, Pos, Pos, Server()->Tick(), -1, -1, -1, -1, LASERFLAG_NO_PREDICT);
			Idx++;
		}
	}
}

void CZoneManager::SnapQuadIds()
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			for(size_t Quad = 0; Quad < pZone->Quads().size(); Quad++)
		{
				int Id = Server()->SnapNewId();
				m_vIds.emplace_back(Id);
			}
		}
	}
	for(size_t Idx = 0; Idx < GameServer()->m_vMultiMaps.size(); Idx++)
	{
		for(size_t Quad = 0; Quad < Collision(Idx)->Quads().size(); Quad++)
		{
			int Id = Server()->SnapNewId();
			m_vIds.emplace_back(Id);
		}
	}
}

void CZoneManager::FreeQuadIds()
{
	for(int Id : m_vIds)
	{
		Server()->SnapFreeId(Id);
	}
	m_vIds.clear();
}

void CZoneManager::OnConsoleInit()
{
	Console()->Register("debug_snap_quads", "i[snap]", CFGFLAG_SERVER, ConDebugSnapQuads, this, "Toggle snapping of zone quads");
}

void CZoneManager::ConDebugSnapQuads(IConsole::IResult *pResult, void *pUserData)
{
	CZoneManager *pSelf = (CZoneManager *)pUserData;
	if(!pResult->NumArguments())
		return;

	bool Set = pResult->GetInteger(0) != 0;
	if(Set == pSelf->m_DebugSnappingQuads)
		return;

	pSelf->m_DebugSnappingQuads = Set;

	if(pSelf->m_DebugSnappingQuads)
		pSelf->SnapQuadIds();
	else
		pSelf->FreeQuadIds();
}