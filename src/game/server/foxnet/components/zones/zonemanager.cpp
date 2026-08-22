#include "zonemanager.h"

#include "checktele.h"
#include "collidable.h"
#include "death.h"
#include "freeze.h"
#include "gambling/moneywheel.h"
#include "gambling/roulette.h"
#include "hidenseek.h"
#include "minigame.h"
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

CQuadZone *CZoneManager::FindZoneByMapIndex(EZoneType Type, size_t MultiMapIdx)
{
	auto It = std::find_if(m_avpZones[(int)Type].begin(), m_avpZones[(int)Type].end(), [MultiMapIdx](const CQuadZone *pZone) {
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

		IMinigame *pGroupZone = nullptr;
		if(!str_comp(aGroupName, "#Roulette"))
		{
			pGroupZone = FindMinigame<CRouletteZone>(MultiMapIdx);
			if(pGroupZone == nullptr)
			{
				pGroupZone = new CRouletteZone(GameServer(), MultiMapIdx);
				m_vpMinigames.push_back(pGroupZone);
			}
		}
		else if(!str_comp(aGroupName, "#MoneyWheel"))
		{
			pGroupZone = FindMinigame<CMoneyWheelZone>(MultiMapIdx);
			if(pGroupZone == nullptr)
			{
				pGroupZone = new CMoneyWheelZone(GameServer(), MultiMapIdx);
				m_vpMinigames.push_back(pGroupZone);
			}
		}
		else if(!str_comp(aGroupName, "#HideNSeek"))
		{
			pGroupZone = FindMinigame<CHideAndSeekZone>(MultiMapIdx);
			if(pGroupZone == nullptr)
			{
				pGroupZone = new CHideAndSeekZone(GameServer(), MultiMapIdx);
				m_vpMinigames.push_back(pGroupZone);
				log_info("hide-n-seek", "Hide and Seek created on map %" PRIzu ", tune zones %d-%d", MultiMapIdx, CHideAndSeekZone::TuneZoneBase(), CHideAndSeekZone::TuneZoneBase() + 3);
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
		vZones.erase(std::remove_if(vZones.begin(), vZones.end(), [MapIdx](CQuadZone *pZone) {
			if(pZone->MultiMapIndex() == MapIdx)
			{
				delete pZone;
				return true;
			}
			return false;
		}),
			vZones.end());
	}

	// The membership itself dies with the zone, but its side effects do not: released solos, tune
	// overrides and broadcasts all have to be undone while the minigame is still there to do it
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(pMinigame->MultiMapIndex() != MapIdx)
			continue;

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			if(!pMinigame->IsInArea(ClientId))
				continue;

			DropPlayer(pMinigame, ClientId);
		}
	}

	m_vpMinigames.erase(std::remove_if(m_vpMinigames.begin(), m_vpMinigames.end(), [MapIdx](IMinigame *pMinigame) {
		if(pMinigame->MultiMapIndex() == MapIdx)
		{
			delete pMinigame;
			return true;
		}
		return false;
	}),
		m_vpMinigames.end());

	if(m_DebugSnappingQuads)
	{
		FreeQuadIds();
		SnapQuadIds();
	}
}

void CZoneManager::UpdateMembership()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		if(!pPlayer)
			continue;

		IMinigame *pWanted = nullptr;
		for(IMinigame *pMinigame : m_vpMinigames)
		{
			if(pMinigame->MultiMapIndex() != (size_t)pPlayer->MultiMapIdx())
				continue;
			if(!pMinigame->ContainsPlayer(pPlayer))
				continue;

			pWanted = pMinigame;
			break; // first match wins, overlapping areas are a map error and resolve the same way every tick
		}

		IMinigame *pCurrent = MinigameOf(ClientId);
		if(pWanted == pCurrent)
			continue;

		// Leave before enter, so a minigame never sees its state torn down by the one taking over
		if(pCurrent)
			DropPlayer(pCurrent, ClientId);

		if(pWanted)
		{
			pWanted->SetInArea(ClientId, true);
			pWanted->OnPlayerEnter(ClientId);
			pWanted->SendMotd(ClientId);
		}
	}
}

void CZoneManager::DropPlayer(IMinigame *pMinigame, int ClientId)
{
	pMinigame->SetInArea(ClientId, false);
	pMinigame->OnPlayerLeave(ClientId);

	// A minigame owns the broadcast while it holds a player, it does not get to keep it afterwards
	if(CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId])
		pPlayer->ClearBroadcast();
}

void CZoneManager::TickSharedQuadZones()
{
	// QHook / QUnHook zones hand their quads to the per-map collision list and keep
	// their own list empty, so each of them used to update and collide against the very
	// same global data -- one full pass over every drop and every quad per quad layer,
	// all producing the same result. Run it once per map instead.
	//
	// Doing it here also settles quad positions before any zone acts on them, rather
	// than midway through the zone loop where the outcome depended on zone ordering.
	const EZoneType aSharedTypes[] = {EZoneType::Hookable, EZoneType::Unhookable};

	std::vector<size_t> vHandledMaps;
	for(EZoneType Type : aSharedTypes)
	{
		for(CQuadZone *pZone : m_avpZones[(int)Type])
		{
			const size_t MapIdx = pZone->MultiMapIndex();
			if(std::find(vHandledMaps.begin(), vHandledMaps.end(), MapIdx) != vHandledMaps.end())
				continue;
			vHandledMaps.push_back(MapIdx);

			// Only CCollidableZone is ever stored under these two types.
			static_cast<CCollidableZone *>(pZone)->TickSharedQuads();
		}
	}
}

void CZoneManager::OnPostTick()
{
	for(auto &vZones : m_avpZones)
		for(CQuadZone *pZone : vZones)
			pZone->OnPostTick();
}

void CZoneManager::OnTick()
{
	TickSharedQuadZones();

	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		auto &vZones = m_avpZones[i];
		for(CQuadZone *pZone : vZones)
		{
			pZone->UpdateCache();
			pZone->OnTick();
		}
	}

	// Membership is resolved against this tick's quad positions, before any minigame acts on it
	for(IMinigame *pMinigame : m_vpMinigames)
		pMinigame->UpdateCache();

	UpdateMembership();

	for(IMinigame *pMinigame : m_vpMinigames)
		pMinigame->OnTick();
}

void CZoneManager::OnSnap(int SnappingClient, bool GlobalSnap, bool RecordingDemo)
{
	const size_t SnappMultiMapIndex = GameServer()->GetMultiMapIdx(SnappingClient);

	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(pMinigame->MultiMapIndex() != SnappMultiMapIndex)
			continue;

		pMinigame->OnSnap(SnappingClient);
	}

	if(!m_DebugSnappingQuads)
		return;

	const int ClientVersion = Server()->GetClientVersion(SnappingClient);
	const bool Sixup = Server()->IsSixup(SnappingClient);

	size_t Idx = 0;
	auto SnapZoneQuads = [&](const CQuadZone *pZone) {
		if(pZone->MultiMapIndex() != SnappMultiMapIndex)
			return true;

		for(const CQuadData &Quad : pZone->Quads())
		{
			if(Idx > m_vIds.size() - 1)
				return false;

			const int &Id = m_vIds[Idx];

			vec2 Pos = Quad.m_aPoints[0];
			if(NetworkClipped(GameServer(), SnappingClient, Pos))
				continue;

			GameServer()->SnapLaserObject(CSnapContext(ClientVersion, Sixup, SnappingClient), Id, Pos, Pos, Server()->Tick(), -1, -1, -1, -1, LASERFLAG_NO_PREDICT);
			Idx++;
		}
		return true;
	};

	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		for(const CQuadZone *pZone : m_avpZones[i])
		{
			if(!SnapZoneQuads(pZone))
				return;
		}
	}
	for(const IMinigame *pMinigame : m_vpMinigames)
	{
		if(!SnapZoneQuads(pMinigame))
			return;
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
	size_t NumQuads = 0;
	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		for(const CQuadZone *pZone : m_avpZones[i])
			NumQuads += pZone->Quads().size();
	}
	for(const IMinigame *pMinigame : m_vpMinigames)
		NumQuads += pMinigame->Quads().size();

	for(size_t Quad = 0; Quad < NumQuads; Quad++)
	{
		std::optional<int> Id = Server()->SnapNewId();
		if(Id.has_value())
			m_vIds.emplace_back(Id.value());
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
	if(IMinigame *pCurrent = MinigameOf(ClientId))
		DropPlayer(pCurrent, ClientId);

	for(IMinigame *pMinigame : m_vpMinigames)
	{
		// The slot is about to be handed to somebody else, who has seen none of this
		pMinigame->ResetClientMotd(ClientId);
		pMinigame->OnClientDrop(ClientId, pReason);
	}
}

IMinigame *CZoneManager::MinigameOf(int ClientId) const
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(pMinigame->IsInArea(ClientId))
			return pMinigame;
	}
	return nullptr;
}

bool CZoneManager::HidesFinishTime(int ClientId, int SnappingClient) const
{
	for(const IMinigame *pMinigame : m_vpMinigames)
	{
		if(!pMinigame->HidesFinishTime())
			continue;
		if(pMinigame->IsInArea(ClientId) && pMinigame->IsInArea(SnappingClient))
			return true;
	}
	return false;
}

void CZoneManager::OnClientReset(int ClientId, size_t MultiMapIdx)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(pMinigame->MultiMapIndex() != MultiMapIdx)
			continue;

		pMinigame->OnClientReset(ClientId);
	}
}

bool CZoneManager::CanUseMoney(CPlayer *pPlayer)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(pMinigame->MultiMapIndex() != (size_t)pPlayer->MultiMapIdx())
			continue;
		if(!pMinigame->CanUseMoney(pPlayer))
			return false;
	}
	return true;
}

void CZoneManager::OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		pMinigame->OnGameInfoSnap(ClientId, pGameInfoObj, pGameInfoEx);
	}
}

int CZoneManager::ShowOthers(CPlayer *pPlayer)
{
	int ShowOthers = -1;
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		int ZoneShowOthers = pMinigame->ShowOthers(pPlayer);
		if(ZoneShowOthers != -1)
			ShowOthers = ZoneShowOthers;
	}
	return ShowOthers;
}

bool CZoneManager::CanUseCommand(CPlayer *pPlayer, const char *pCommand)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(!pMinigame->CanUseCommand(pPlayer, pCommand))
			return false;
	}
	return true;
}

bool CZoneManager::CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(!pMinigame->CanSpectateId(pPlayer, pTarget))
			return false;
	}
	return true;
}

bool CZoneManager::CanSnapCharacter(CCharacter *pChr, int SnappingClient)
{
	bool Allowed = true;
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(!pMinigame->CanSnapCharacter(pChr, SnappingClient))
			Allowed = false;
	}
	return Allowed;
}
bool CZoneManager::CanDropWeapon(CCharacter *pChr, int Weapon)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(!pMinigame->CanDropWeapon(pChr, Weapon))
			return false;
	}
	return true;
}

void CZoneManager::OnCharacterSpawn(int ClientId, vec2 Pos)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		pMinigame->OnCharacterSpawn(ClientId, Pos);
	}
}
void CZoneManager::OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		pMinigame->OnCharacterDie(ClientId, Killer, Weapon, SendKillMsg);
	}
}
bool CZoneManager::OnCharacterFire(CCharacter *pChr, int Weapon)
{
	bool Allowed = true;
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(!pMinigame->OnCharacterFire(pChr, Weapon))
			Allowed = false;
	}
	return Allowed;
}
void CZoneManager::OnCharacterHammerHit(int ClientId, int Target)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		pMinigame->OnCharacterHammerHit(ClientId, Target);
	}
}
bool CZoneManager::SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags)
{
	// Doesn't need bool Allowed, if SetMask returns false the ClientId wont be snapped
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		if(!pMinigame->SetMask(ClientId, MultiMapIdx, Team, ExceptId, Asker, VersionFlags, Flags))
			return false;
	}
	return true;
}

void CZoneManager::OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo &ClientInfo, int *pTeam, int *pLatency, int *pScore)
{
	for(IMinigame *pMinigame : m_vpMinigames)
	{
		pMinigame->OnPlayerSnap(pPlayer, SnappingClient, ClientInfo, pTeam, pLatency, pScore);
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
	auto CountZone = [&](const CQuadZone *pZone) {
		if(MultiMapIndex != -1 && pZone->MultiMapIndex() != (size_t)MultiMapIndex)
			return;
		Num += pZone->Quads().size();
	};

	for(int i = 0; i < (int)EZoneType::Num; i++)
	{
		for(const CQuadZone *pZone : pSelf->m_avpZones[i])
			CountZone(pZone);
	}
	for(const IMinigame *pMinigame : pSelf->m_vpMinigames)
		CountZone(pMinigame);

	if(MultiMapIndex == -1)
	{
		for(size_t Idx = 0; Idx < pSelf->GameServer()->m_vMultiMaps.size(); Idx++)
		{
			Num += pSelf->Collision(Idx)->Quads().size();
		}
	}
	else
	{
		Num += pSelf->Collision(MultiMapIndex)->Quads().size();
	}
	log_info("zones", "%" PRIzu " total zones", Num);
}
