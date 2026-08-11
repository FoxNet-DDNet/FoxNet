/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "entity.h"

#include "gamecontext.h"
#include "gameworld.h"
#include "player.h"

#include <base/math.h>
#include <base/vmath.h>

#include <engine/server.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/server/gameworld.h>

//////////////////////////////////////////////////
// Entity
//////////////////////////////////////////////////
CEntity::CEntity(CGameWorld *pGameWorld, int MultiMapIdx, int ObjType, bool SnapFreeId, vec2 Pos, int ProximityRadius)
{
	m_pGameWorld = pGameWorld;
	m_MultiMapIndex = MultiMapIdx;
	m_pCCollision = GameServer()->Collision(m_MultiMapIndex);

	m_ObjType = ObjType;
	m_Pos = Pos;
	m_ProximityRadius = ProximityRadius;

	m_MarkedForDestroy = false;
	if(SnapFreeId)
		m_Id = Server()->SnapNewId();

	m_pPrevTypeEntity = nullptr;
	m_pNextTypeEntity = nullptr;
}

CEntity::~CEntity()
{
	GameWorld()->RemoveEntity(this);
	if(m_Id.has_value())
		Server()->SnapFreeId(m_Id.value());
}

CCollision *CEntity::Collision()
{
	return GameServer()->Collision(MultiMapIdx());
}

bool CEntity::NetworkClipped(int SnappingClient) const
{
	if(!CheckMultiMapIdx(SnappingClient, MultiMapIdx()))
		return true;

	return ::NetworkClipped(m_pGameWorld->GameServer(), SnappingClient, m_Pos);
}

bool CEntity::NetworkClipped(int SnappingClient, vec2 CheckPos) const
{
	if(!CheckMultiMapIdx(SnappingClient, MultiMapIdx()))
		return true;

	return ::NetworkClipped(m_pGameWorld->GameServer(), SnappingClient, CheckPos);
}

bool CEntity::NetworkClippedLine(int SnappingClient, vec2 StartPos, vec2 EndPos) const
{
	if(!CheckMultiMapIdx(SnappingClient, MultiMapIdx()))
		return true;

	return ::NetworkClippedLine(m_pGameWorld->GameServer(), SnappingClient, StartPos, EndPos);
}

bool CEntity::GameLayerClipped(vec2 CheckPos)
{
	return round_to_int(CheckPos.x) / 32 < -200 || round_to_int(CheckPos.x) / 32 > Collision()->GetWidth() + 200 ||
	       round_to_int(CheckPos.y) / 32 < -200 || round_to_int(CheckPos.y) / 32 > Collision()->GetHeight() + 200;
}

bool CEntity::GetNearestAirPos(vec2 Pos, vec2 PrevPos, vec2 *pOutPos)
{
	for(int k = 0; k < 16 && Collision()->CheckPoint(Pos); k++)
	{
		Pos -= normalize(PrevPos - Pos);
	}

	vec2 PosInBlock = vec2(round_to_int(Pos.x) % 32, round_to_int(Pos.y) % 32);
	vec2 BlockCenter = vec2(round_to_int(Pos.x), round_to_int(Pos.y)) - PosInBlock + vec2(16.0f, 16.0f);

	*pOutPos = vec2(BlockCenter.x + (PosInBlock.x < 16 ? -2.0f : 1.0f), Pos.y);
	if(!Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2()))
		return true;

	*pOutPos = vec2(Pos.x, BlockCenter.y + (PosInBlock.y < 16 ? -2.0f : 1.0f));
	if(!Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2()))
		return true;

	*pOutPos = vec2(BlockCenter.x + (PosInBlock.x < 16 ? -2.0f : 1.0f),
		BlockCenter.y + (PosInBlock.y < 16 ? -2.0f : 1.0f));
	return !Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2());
}

bool CEntity::GetNearestAirPosPlayer(vec2 PlayerPos, vec2 *pOutPos)
{
	for(int Distance = 5; Distance >= -1; Distance--)
	{
		*pOutPos = vec2(PlayerPos.x, PlayerPos.y - Distance);
		if(!Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2()))
		{
			return true;
		}
	}
	return false;
}

// <FoxNet
bool CheckMultiMapIdx(const CGameContext *pGameServer, int SnappingClient, int MultiMapIndex)
{
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;
	if(g_Config.m_SvMultimapShowOthers || g_Config.m_SvMultimapAllowInteraction)
		return true;

	return MultiMapIndex == pGameServer->m_apPlayers[SnappingClient]->MultiMapIdx();
}
bool CEntity::CheckMultiMapIdx(int SnappingClient, int MultiMapIndex) const
{
	return ::CheckMultiMapIdx(m_pGameWorld->GameServer(), SnappingClient, MultiMapIndex);
}
// FoxNet>

bool NetworkClipped(const CGameContext *pGameServer, int SnappingClient, vec2 CheckPos)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || pGameServer->m_apPlayers[SnappingClient]->m_ShowAll)
		return false;

	const CPlayer *pPlayer = pGameServer->m_apPlayers[SnappingClient];
	vec2 Delta = pPlayer->m_ViewPos - CheckPos;
	return absolute(Delta.x) > pPlayer->m_NetworkClipRadius.x || absolute(Delta.y) > pPlayer->m_NetworkClipRadius.y;
}

bool NetworkClippedLine(const CGameContext *pGameServer, int SnappingClient, vec2 StartPos, vec2 EndPos)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || pGameServer->m_apPlayers[SnappingClient]->m_ShowAll)
		return false;

	const CPlayer *pPlayer = pGameServer->m_apPlayers[SnappingClient];
	const vec2 &ViewPos = pPlayer->m_ViewPos;

	vec2 DistanceToLine, ClosestPoint;
	if(closest_point_on_line(StartPos, EndPos, ViewPos, ClosestPoint))
	{
		DistanceToLine = ViewPos - ClosestPoint;
	}
	else
	{
		// No line section was passed but two equal points
		DistanceToLine = ViewPos - StartPos;
	}

	float ClippDistance = std::max(pPlayer->m_NetworkClipRadius.x, pPlayer->m_NetworkClipRadius.y);
	return (absolute(DistanceToLine.x) > ClippDistance || absolute(DistanceToLine.y) > ClippDistance);
}
