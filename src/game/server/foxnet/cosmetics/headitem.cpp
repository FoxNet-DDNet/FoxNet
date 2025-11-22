// Made by qxdFox
#include "headitem.h"

#include "game/server/entities/character.h"

#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/server/foxnet/shop.h>
#include <game/gamecore.h>

CHeadItem::CHeadItem(CGameWorld *pGameWorld, int Owner, vec2 Pos, int Type, vec2 Offset) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_HEAD_ITEM, Pos)
{
	m_Pos = Pos;
	m_Owner = Owner;

	// Type of Entity
	m_Type = Type;
	m_Offset = Offset;

	GameWorld()->InsertEntity(this);
}

void CHeadItem::Reset()
{
	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CHeadItem::Tick()
{
	CPlayer *pOwnerPl = GameServer()->m_apPlayers[m_Owner];

	if(!pOwnerPl)
	{
		Reset();
		return;
	}

	CCharacter *pOwnerChr = pOwnerPl->GetCharacter();

	switch(m_Type)
	{
	case HEADITEM_SPAWNSOLO:
		if(!pOwnerChr || !pOwnerChr->m_SpawnSolo)
		{
			Reset();
			return;
		}
		break;
	case HEADITEM_COSMETIC:
		if(pOwnerPl->Cosmetics()->m_HatType == HATTYPE_NONE)
		{
			pOwnerPl->Inv()->SetEquippedIndex(HAT_HAMMER + pOwnerPl->Cosmetics()->m_HatType - 1, 0);
			Reset();
			return;
		}
		break;
	default: 
		Reset();
		return;
	}

	if(!pOwnerChr)
		return;

	m_Pos = pOwnerChr->GetPos();
}

void CHeadItem::Snap(int SnappingClient)
{
	if(SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return;

	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	if(!pOwnerChr || !pSnapPlayer)
		return;

	if(m_Type != HEADITEM_SPAWNSOLO)
	{
		if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowHats)
			return;

		if(pOwnerChr->IsPaused())
			return;

		if(pOwnerChr->m_SpawnSolo)
			return;
	}

	if(!pOwnerChr->TeamMask().test(SnappingClient))
		return;

	if(pSnapPlayer->GetCharacter() && pOwnerChr)
		if(!pOwnerChr->CanSnapCharacter(SnappingClient))
			return;

	if(pOwnerChr->GetPlayer()->m_Vanish && SnappingClient != pOwnerChr->GetPlayer()->GetCid() && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return;

	vec2 Pos = m_Pos + pOwnerChr->GetVelocity();
	if(m_Owner == SnappingClient)
		Pos = pOwnerChr->GetPredictedPos(pOwnerChr->m_Pos, pOwnerChr->m_PrevPos);
	Pos += m_Offset;

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	int Type = 0;
	int SubType = 0;
	int Flags = PICKUPFLAG_NO_PREDICT;

	switch(m_Type)
	{
	case HEADITEM_SPAWNSOLO:
		Type = POWERUP_ARMOR;
		SubType = 0;
		break;
	case HEADITEM_COSMETIC:
		Type = POWERUP_WEAPON;
		SubType = pOwnerChr->GetPlayer()->Cosmetics()->m_HatType - 1;
		Flags |= pOwnerChr->Acc()->m_Configs.m_HatItemFlags;
		break;
	default:
		break;
	}

	GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), GetId(), Pos, Type, SubType, -1, Flags);
}
