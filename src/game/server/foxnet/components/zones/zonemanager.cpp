#include "zonemanager.h"

#include "checktele.h"
#include "collidable.h"
#include "death.h"
#include "freeze.h"
#include "hidenseek.h"
#include "roulette.h"
#include "unfreeze.h"
#include "zone.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/map.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>

#include <algorithm>
#include <iterator>
#include <vector>

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
		else if(!str_comp(aGroupName, "#HideNSeek"))
		{
			pGroupZone = FindZoneByMapIndex(EZoneType::HideNSeek, MultiMapIdx);
			if(pGroupZone == nullptr)
			{
				pGroupZone = new CHideAndSeekZone(GameServer(), MultiMapIdx);
				m_avpZones[(int)EZoneType::HideNSeek].push_back(pGroupZone);
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
				CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, COLLZONE_STOPA);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::StopA].push_back(pZone);
			}
			if(!str_comp("QHook", aLayerName))
			{
				CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, COLLZONE_HOOK);
				pZone->Init(pTilemap);
				m_avpZones[(int)EZoneType::Hookable].push_back(pZone);
			}
			else if(!str_comp("QUnHook", aLayerName))
			{
				CCollidableZone *pZone = new CCollidableZone(GameServer(), MultiMapIdx, COLLZONE_UNHOOK);
				pZone->Init(pTilemap);
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
			size_t SnappMultiMapIndex = GameServer()->GetMultiMapIdx(SnappingClient);

			if(pZone->MultiMapIndex() != SnappMultiMapIndex)
				continue;

			for(const CQuadData &Quad : pZone->Quads())
			{
				if(Idx > m_vIds.size() - 1)
					return;

				const int &Id = m_vIds[Idx];

				vec2 Pos = Quad.m_aPoints[0];
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
			vec2 Pos = Quad.m_aPoints[0];
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
				std::optional<int> Id = Server()->SnapNewId();
				if(Id.has_value())
					m_vIds.emplace_back(Id.value());
			}
		}
	}
	for(size_t Idx = 0; Idx < GameServer()->m_vMultiMaps.size(); Idx++)
	{
		for(size_t Quad = 0; Quad < Collision(Idx)->Quads().size(); Quad++)
		{
			std::optional<int> Id = Server()->SnapNewId();
			if(Id.has_value())
				m_vIds.emplace_back(Id.value());
		}
	}
}

void CZoneManager::OnClientDrop(int ClientId, const char *pReason)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			pZone->OnClientDrop(ClientId, pReason);
		}
	}
}

void CZoneManager::OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			pZone->OnGameInfoSnap(ClientId, pGameInfoObj, pGameInfoEx);
		}
	}
}

int CZoneManager::ShowOthers(CPlayer *pPlayer)
{
	int ShowOthers = -1;
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			int ZoneShowOthers = pZone->ShowOthers(pPlayer);
			if(ZoneShowOthers != -1)
				ShowOthers = ZoneShowOthers;
		}
	}
	return ShowOthers;
}

bool CZoneManager::CanUseCommand(CPlayer *pPlayer, const char *pCommand)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			if(!pZone->CanUseCommand(pPlayer, pCommand))
				return false;
		}
	}
	return true;
}

bool CZoneManager::CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			if(!pZone->CanSpectateId(pPlayer, pTarget))
				return false;
		}
	}
	return true;
}

bool CZoneManager::CanSnapCharacter(CCharacter *pChr, int SnappingClient)
{
	bool Allowed = true;
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			if(!pZone->CanSnapCharacter(pChr, SnappingClient))
				Allowed = false;
		}
	}
	return Allowed;
}
bool CZoneManager::CanDropWeapon(CCharacter *pChr, int Weapon)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			if(!pZone->CanDropWeapon(pChr, Weapon))
				return false;
		}
	}
	return true;
}

void CZoneManager::OnCharacterSpawn(int ClientId, vec2 Pos)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			pZone->OnCharacterSpawn(ClientId, Pos);
		}
	}
}
void CZoneManager::OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			pZone->OnCharacterDie(ClientId, Killer, Weapon, SendKillMsg);
		}
	}
}
bool CZoneManager::OnCharacterFire(int ClientId, int Weapon)
{
	bool Allowed = true;
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			if(!pZone->OnCharacterFire(ClientId, Weapon))
				Allowed = false;
		}
	}
	return Allowed;
}
void CZoneManager::OnCharacterHammerHit(int ClientId, int Target)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			pZone->OnCharacterHammerHit(ClientId, Target);
		}
	}
}
bool CZoneManager::SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags)
{
	// Doesn't need bool Allowed, if SetMask returns false the ClientId wont be snapped
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			if(!pZone->SetMask(ClientId, MultiMapIdx, Team, ExceptId, Asker, VersionFlags, Flags))
				return false;
		}
	}
	return true;
}

void CZoneManager::OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo &ClientInfo, int *pTeam, int *pLatency, int *pScore)
{
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			pZone->OnPlayerSnap(pPlayer, SnappingClient, ClientInfo, pTeam, pLatency, pScore);
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
	Console()->Register("zones_debug_snap", "i[snap]", CFGFLAG_SERVER, ConDebugSnapQuads, this, "Toggle snapping of zone quads");
	Console()->Register("zones_num", "?i[multimap]", CFGFLAG_SERVER, ConNumQuads, this, "Print the amount of quads loaded on the server");
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

void CZoneManager::ConNumQuads(IConsole::IResult *pResult, void *pUserData)
{
	CZoneManager *pSelf = (CZoneManager *)pUserData;

	int MultiMapIndex = pResult->NumArguments() ? pResult->GetInteger(0) : -1;
	if(MultiMapIndex != -1 && (size_t)MultiMapIndex >= pSelf->GameServer()->m_vMultiMaps.size())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "zones", "invalid multimap index");
		return;
	}

	size_t Num = 0;
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = pSelf->m_avpZones[i];
		for(IZone *pZone : vZones)
		{
			if(MultiMapIndex != -1 && (size_t)pZone->MultiMapIndex() != (size_t)MultiMapIndex)
				continue;

			for(size_t Quad = 0; Quad < pZone->Quads().size(); Quad++)
			{
				Num++;
			}
		}
	}
	if(MultiMapIndex == -1)
	{
		for(size_t Idx = 0; Idx < pSelf->GameServer()->m_vMultiMaps.size(); Idx++)
		{
			for(size_t Quad = 0; Quad < pSelf->Collision(Idx)->Quads().size(); Quad++)
			{
				Num++;
			}
		}
	}
	else
	{
		for(size_t Quad = 0; Quad < pSelf->Collision(MultiMapIndex)->Quads().size(); Quad++)
		{
			Num++;
		}
	}
	log_info("zones", "%" PRIzu " total zones", Num);
}