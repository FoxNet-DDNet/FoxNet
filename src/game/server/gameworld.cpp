/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "gameworld.h"

#include "entities/character.h"
#include "entity.h"
#include "gamecontext.h"
#include "gamecontroller.h"

#include <engine/shared/config.h>

#include <game/collision.h>

#include <algorithm>
#include <iterator>
#include <utility>

//////////////////////////////////////////////////
// game world
//////////////////////////////////////////////////
CGameWorld::CGameWorld()
{
	m_pGameServer = nullptr;
	m_pConfig = nullptr;
	m_pServer = nullptr;

	m_Paused = false;
	m_ResetRequested = false;
	for(auto &pFirstEntityType : m_apFirstEntityTypes)
		pFirstEntityType = nullptr;
}

CGameWorld::~CGameWorld()
{
	// delete all entities
	for(auto &pFirstEntityType : m_apFirstEntityTypes)
		while(pFirstEntityType)
			delete pFirstEntityType; // NOLINT(clang-analyzer-cplusplus.NewDelete)
}

void CGameWorld::SetGameServer(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();
}

void CGameWorld::InitSwitchers(int HighestSwitchNumber, int MultiMapIdx)
{
	m_Core.InitSwitchers(HighestSwitchNumber, MultiMapIdx);
}

CEntity *CGameWorld::FindFirst(int Type)
{
	return Type < 0 || Type >= NUM_ENTTYPES ? nullptr : m_apFirstEntityTypes[Type];
}

int CGameWorld::FindEntities(vec2 Pos, float Radius, CEntity **ppEnts, int Max, int Type, int MultiMapIdx)
{
	if(Type < 0 || Type >= NUM_ENTTYPES)
		return 0;

	const bool CheckMultiMap = !g_Config.m_SvMultimapAllowInteraction;

	int Num = 0;
	for(CEntity *pEnt = m_apFirstEntityTypes[Type]; pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		// Radius first: it is a few flops, while MultiMapIdx() is a virtual call that
		// chases a pointer for characters. Most entities fail here, so this keeps the
		// dispatch off the hot path without changing which entities are selected.
		const float CombinedRadius = Radius + pEnt->m_ProximityRadius;
		if(distance_squared(pEnt->m_Pos, Pos) >= CombinedRadius * CombinedRadius)
			continue;

		if(CheckMultiMap && pEnt->MultiMapIdx() != MultiMapIdx)
			continue;

		if(ppEnts)
			ppEnts[Num] = pEnt;
		Num++;
		if(Num == Max)
			break;
	}

	return Num;
}

void CGameWorld::RebuildCharacterGrid()
{
	m_CharacterGridTick = Server()->Tick();
	m_CharacterGridMaxRadius = 0.0f;

	for(int &Start : m_aCharacterGridStart)
		Start = 0;

	// Count into slot+1 so the prefix sum below lands directly on start offsets.
	int Num = 0;
	for(CEntity *pEnt = m_apFirstEntityTypes[ENTTYPE_CHARACTER]; pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		if(Num >= (int)std::size(m_aCharacterGridEntries))
			break;
		const unsigned Bucket = CharacterGridBucket(CharacterGridCell(pEnt->m_Pos.x), CharacterGridCell(pEnt->m_Pos.y));
		m_aCharacterGridStart[Bucket + 1]++;
		m_CharacterGridMaxRadius = std::max(m_CharacterGridMaxRadius, pEnt->m_ProximityRadius);
		Num++;
	}

	for(int i = 0; i < CHARACTER_GRID_BUCKETS; i++)
		m_aCharacterGridStart[i + 1] += m_aCharacterGridStart[i];

	int aCursor[CHARACTER_GRID_BUCKETS];
	for(int i = 0; i < CHARACTER_GRID_BUCKETS; i++)
		aCursor[i] = m_aCharacterGridStart[i];

	// Walks the same prefix of the same list as the counting pass, so the cursors
	// cannot overrun the space reserved for their bucket.
	int Placed = 0;
	for(CEntity *pEnt = m_apFirstEntityTypes[ENTTYPE_CHARACTER]; pEnt && Placed < Num; pEnt = pEnt->m_pNextTypeEntity)
	{
		const int CellX = CharacterGridCell(pEnt->m_Pos.x);
		const int CellY = CharacterGridCell(pEnt->m_Pos.y);
		SCharacterGridEntry &Entry = m_aCharacterGridEntries[aCursor[CharacterGridBucket(CellX, CellY)]++];
		Entry.m_pEnt = pEnt;
		Entry.m_CellX = CellX;
		Entry.m_CellY = CellY;
		Placed++;
	}
}

int CGameWorld::FindCharacters(vec2 Pos, float Radius, CEntity **ppEnts, int Max, int MultiMapIdx)
{
	if(m_CharacterGridTick != Server()->Tick())
		RebuildCharacterGrid();

	const bool CheckMultiMap = !g_Config.m_SvMultimapAllowInteraction;

	// Widen by the largest proximity radius in the index, since FindEntities admits
	// an entity when the combined radii overlap, not just the query radius.
	const float Reach = Radius + m_CharacterGridMaxRadius;
	const int MinCellX = CharacterGridCell(Pos.x - Reach);
	const int MaxCellX = CharacterGridCell(Pos.x + Reach);
	const int MinCellY = CharacterGridCell(Pos.y - Reach);
	const int MaxCellY = CharacterGridCell(Pos.y + Reach);

	int Num = 0;
	for(int CellY = MinCellY; CellY <= MaxCellY; CellY++)
	{
		for(int CellX = MinCellX; CellX <= MaxCellX; CellX++)
		{
			const unsigned Bucket = CharacterGridBucket(CellX, CellY);
			for(int i = m_aCharacterGridStart[Bucket]; i < m_aCharacterGridStart[Bucket + 1]; i++)
			{
				const SCharacterGridEntry &Entry = m_aCharacterGridEntries[i];
				// Unrelated cells can share a bucket. Requiring an exact cell match drops
				// those, and since each entity sits in exactly one cell it also
				// guarantees we never emit the same entity twice.
				if(Entry.m_CellX != CellX || Entry.m_CellY != CellY)
					continue;

				CEntity *pEnt = Entry.m_pEnt;
				const float CombinedRadius = Radius + pEnt->m_ProximityRadius;
				if(distance_squared(pEnt->m_Pos, Pos) >= CombinedRadius * CombinedRadius)
					continue;

				if(CheckMultiMap && pEnt->MultiMapIdx() != MultiMapIdx)
					continue;

				if(ppEnts)
					ppEnts[Num] = pEnt;
				Num++;
				if(Num == Max)
					return Num;
			}
		}
	}

#ifdef CONF_DEBUG
	// The index has to agree with the scan it replaces. Only checked when Max did not
	// truncate: past that point the two visit entities in different orders and would
	// legitimately keep different subsets.
	dbg_assert(FindEntities(Pos, Radius, nullptr, Max, ENTTYPE_CHARACTER, MultiMapIdx) == Num,
		"character grid disagrees with linear scan");
#endif

	return Num;
}

void CGameWorld::InsertEntity(CEntity *pEnt)
{
#ifdef CONF_DEBUG
	for(CEntity *pCur = m_apFirstEntityTypes[pEnt->m_ObjType]; pCur; pCur = pCur->m_pNextTypeEntity)
		dbg_assert(pCur != pEnt, "err");
#endif

	if(pEnt->m_ObjType == ENTTYPE_CHARACTER)
		m_CharacterGridTick = -1;

	// insert it
	if(m_apFirstEntityTypes[pEnt->m_ObjType])
		m_apFirstEntityTypes[pEnt->m_ObjType]->m_pPrevTypeEntity = pEnt;
	pEnt->m_pNextTypeEntity = m_apFirstEntityTypes[pEnt->m_ObjType];
	pEnt->m_pPrevTypeEntity = nullptr;
	m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt;
}

void CGameWorld::RemoveEntity(CEntity *pEnt)
{
	// not in the list
	if(!pEnt->m_pNextTypeEntity && !pEnt->m_pPrevTypeEntity && m_apFirstEntityTypes[pEnt->m_ObjType] != pEnt)
		return;

	if(pEnt->m_ObjType == ENTTYPE_CHARACTER)
		m_CharacterGridTick = -1;

	// remove
	if(pEnt->m_pPrevTypeEntity)
		pEnt->m_pPrevTypeEntity->m_pNextTypeEntity = pEnt->m_pNextTypeEntity;
	else
		m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt->m_pNextTypeEntity;
	if(pEnt->m_pNextTypeEntity)
		pEnt->m_pNextTypeEntity->m_pPrevTypeEntity = pEnt->m_pPrevTypeEntity;

	// keep list traversing valid
	if(m_pNextTraverseEntity == pEnt)
		m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;

	pEnt->m_pNextTypeEntity = nullptr;
	pEnt->m_pPrevTypeEntity = nullptr;
}

//
void CGameWorld::Snap(int SnappingClient)
{
	for(CEntity *pEnt = m_apFirstEntityTypes[ENTTYPE_CHARACTER]; pEnt;)
	{
		m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
		pEnt->Snap(SnappingClient);
		pEnt = m_pNextTraverseEntity;
	}

	for(int i = 0; i < NUM_ENTTYPES; i++)
	{
		if(i == ENTTYPE_CHARACTER)
			continue;

		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Snap(SnappingClient);
			pEnt = m_pNextTraverseEntity;
		}
	}
}

void CGameWorld::Reset()
{
	// reset all entities
	for(auto *pEnt : m_apFirstEntityTypes)
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Reset();
			pEnt = m_pNextTraverseEntity;
		}
	RemoveEntities();

	GameServer()->m_pController->OnReset();
	RemoveEntities();

	m_ResetRequested = false;

	GameServer()->CreateAllEntities(false, DefaultMapIndex);
}

void CGameWorld::RemoveEntitiesFromPlayer(int PlayerId)
{
	RemoveEntitiesFromPlayers(&PlayerId, 1);
}

void CGameWorld::RemoveEntitiesFromPlayers(int PlayerIds[], int NumPlayers)
{
	for(auto *pEnt : m_apFirstEntityTypes)
	{
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			for(int i = 0; i < NumPlayers; i++)
			{
				if(pEnt->GetOwnerId() == PlayerIds[i])
				{
					RemoveEntity(pEnt);
					pEnt->Destroy();
					break;
				}
			}
			pEnt = m_pNextTraverseEntity;
		}
	}
}

void CGameWorld::RemoveEntities()
{
	// destroy objects marked for destruction
	for(auto *pEnt : m_apFirstEntityTypes)
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			if(pEnt->m_MarkedForDestroy)
			{
				RemoveEntity(pEnt);
				pEnt->Destroy();
			}
			pEnt = m_pNextTraverseEntity;
		}
}

void CGameWorld::Tick()
{
	if(m_ResetRequested)
		Reset();

	if(!m_Paused)
	{
		// update all objects
		for(int i = 0; i < NUM_ENTTYPES; i++)
		{
			// It's important to call PreTick() and Tick() after each other.
			// If we call PreTick() before, and Tick() after other entities have been processed, it causes physics changes such as a stronger shotgun or grenade.
			if(g_Config.m_SvNoWeakHook && i == ENTTYPE_CHARACTER)
			{
				auto *pEnt = m_apFirstEntityTypes[i];
				for(; pEnt;)
				{
					m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
					((CCharacter *)pEnt)->PreTick();
					pEnt = m_pNextTraverseEntity;
				}
			}
			{
				auto *pEnt = m_apFirstEntityTypes[i];
				for(; pEnt;)
				{
					m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
					pEnt->Tick();
					pEnt = m_pNextTraverseEntity;
				}
			}
		}
		for(auto *pEnt : m_apFirstEntityTypes)
		{
			for(; pEnt;)
			{
				m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
				pEnt->TickDeferred();
				pEnt = m_pNextTraverseEntity;
			}
		}
	}
	else
	{
		// update all objects
		for(auto *pEnt : m_apFirstEntityTypes)
		{
			for(; pEnt;)
			{
				m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
				pEnt->TickPaused();
				pEnt = m_pNextTraverseEntity;
			}
		}
	}

	RemoveEntities();

	// find the characters' strong/weak id
	int StrongWeakId = 0;
	for(CCharacter *pChar = (CCharacter *)FindFirst(ENTTYPE_CHARACTER); pChar; pChar = (CCharacter *)pChar->TypeNext())
	{
		pChar->m_StrongWeakId = StrongWeakId;
		StrongWeakId++;
	}
}

ESaveResult CGameWorld::BlocksSave(int ClientId)
{
	// check all objects
	for(auto *pEnt : m_apFirstEntityTypes)
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			ESaveResult Result = pEnt->BlocksSave(ClientId);
			if(Result != ESaveResult::SUCCESS)
				return Result;
			pEnt = m_pNextTraverseEntity;
		}
	return ESaveResult::SUCCESS;
}

void CGameWorld::SwapClients(int Client1, int Client2)
{
	// update all objects
	for(auto *pEnt : m_apFirstEntityTypes)
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->SwapClients(Client1, Client2);
			pEnt = m_pNextTraverseEntity;
		}
}

CCharacter *CGameWorld::IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, const CCharacter *pNotThis, int CollideWith, const CCharacter *pThisOnly)
{
	return (CCharacter *)IntersectEntity(Pos0, Pos1, Radius, ENTTYPE_CHARACTER, NewPos, pNotThis, CollideWith, pThisOnly);
}

CEntity *CGameWorld::IntersectEntity(vec2 Pos0, vec2 Pos1, float Radius, int Type, vec2 &NewPos, const CEntity *pNotThis, int CollideWith, const CEntity *pThisOnly)
{
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CEntity *pClosest = nullptr;

	CEntity *pEntity = FindFirst(Type);
	for(; pEntity; pEntity = pEntity->TypeNext())
	{
		if(pEntity == pNotThis)
			continue;

		if(pThisOnly && pEntity != pThisOnly)
			continue;

		if(CollideWith != -1 && !pEntity->CanCollide(CollideWith))
			continue;

		vec2 IntersectPos;
		if(closest_point_on_line(Pos0, Pos1, pEntity->m_Pos, IntersectPos))
		{
			float Len = distance(pEntity->m_Pos, IntersectPos);
			if(Len < pEntity->m_ProximityRadius + Radius)
			{
				Len = distance(Pos0, IntersectPos);
				if(Len < ClosestLen)
				{
					NewPos = IntersectPos;
					ClosestLen = Len;
					pClosest = pEntity;
				}
			}
		}
	}

	return pClosest;
}

CCharacter *CGameWorld::ClosestCharacter(vec2 Pos, float Radius, const CEntity *pNotThis)
{
	// Find other players
	float ClosestRange = Radius * 2;
	CCharacter *pClosest = nullptr;

	CCharacter *p = (CCharacter *)FindFirst(ENTTYPE_CHARACTER);
	for(; p; p = (CCharacter *)p->TypeNext())
	{
		if(p == pNotThis)
			continue;
		if(pNotThis && pNotThis->MultiMapIdx() != p->MultiMapIdx() && !g_Config.m_SvMultimapAllowInteraction)
			continue;

		float Len = distance(Pos, p->m_Pos);
		if(Len < p->m_ProximityRadius + Radius)
		{
			if(Len < ClosestRange)
			{
				ClosestRange = Len;
				pClosest = p;
			}
		}
	}

	return pClosest;
}

std::vector<CCharacter *> CGameWorld::IntersectedCharacters(vec2 Pos0, vec2 Pos1, float Radius, const CEntity *pNotThis)
{
	std::vector<CCharacter *> vpCharacters;
	CCharacter *pChr = (CCharacter *)FindFirst(CGameWorld::ENTTYPE_CHARACTER);
	for(; pChr; pChr = (CCharacter *)pChr->TypeNext())
	{
		if(pChr == pNotThis)
			continue;

		vec2 IntersectPos;
		if(closest_point_on_line(Pos0, Pos1, pChr->m_Pos, IntersectPos))
		{
			float Len = distance(pChr->m_Pos, IntersectPos);
			if(Len < pChr->m_ProximityRadius + Radius)
			{
				vpCharacters.push_back(pChr);
			}
		}
	}
	return vpCharacters;
}

void CGameWorld::ReleaseHooked(int ClientId)
{
	CCharacter *pChr = (CCharacter *)FindFirst(CGameWorld::ENTTYPE_CHARACTER);
	for(; pChr; pChr = (CCharacter *)pChr->TypeNext())
	{
		if(pChr->Core()->HookedPlayer() == ClientId && !pChr->IsSuper())
		{
			pChr->ReleaseHook();
		}
	}
}

// <FoxNet
CTuningParams *CGameWorld::TuningList(size_t MultiMapIdx)
{
	return GameServer()->TuningList(MultiMapIdx);
}
CTuningParams *CGameWorld::GetTuning(size_t MultiMapIdx, int i)
{
	return &TuningList(MultiMapIdx)[i];
}
const CTuningParams *CGameWorld::TuningList(size_t MultiMapIdx) const
{
	return const_cast<CGameContext *>(GameServer())->TuningList(MultiMapIdx);
}
const CTuningParams *CGameWorld::GetTuning(size_t MultiMapIdx, int i) const
{
	return &TuningList(MultiMapIdx)[i];
}

void CGameWorld::RemoveEntities(int Type)
{
	for(auto *pEnt : m_apFirstEntityTypes)
	{
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			if(pEnt->m_ObjType == Type)
			{
				RemoveEntity(pEnt);
				pEnt->Destroy();
			}
			pEnt = m_pNextTraverseEntity;
		}
	}
}

std::vector<CEntity *> CGameWorld::FindEntitiesWithOwner(int Type, int Owner) const
{
	std::vector<CEntity *> vEntities;
	CEntity *pEnt = m_apFirstEntityTypes[Type];
	for(; pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		if(pEnt->GetOwnerId() == Owner)
			vEntities.push_back(pEnt);
	}
	return vEntities;
}

std::vector<CEntity *> CGameWorld::EntitiesOfType(int Type, const CEntity *pNotThis) const
{
	std::vector<CEntity *> vEntities;
	CEntity *pEnt = m_apFirstEntityTypes[Type];
	for(; pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		if(pEnt == pNotThis)
			continue;
		vEntities.push_back(pEnt);
	}
	return vEntities;
}

CEntity *CGameWorld::FindEntityOnMap(int Type, int MapIdx, const CEntity *pNotThis)
{
	CEntity *pEnt = m_apFirstEntityTypes[Type];
	for(; pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		if(pEnt == pNotThis)
			continue;
		if(pEnt->Collision() == GameServer()->Collision(MapIdx))
			return pEnt;
	}
	return nullptr;
}

void CGameWorld::DestroyEntitiesOfMap(int MultiMapIdx)
{
	for(auto *pEnt : m_apFirstEntityTypes)
	{
		for(; pEnt;)
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			if(pEnt->MultiMapIdx() == MultiMapIdx)
			{
				RemoveEntity(pEnt);
				pEnt->Destroy();
			}
			pEnt = m_pNextTraverseEntity;
		}
	}
}
