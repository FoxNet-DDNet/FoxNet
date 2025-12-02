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

	for(size_t i = 0; i < std::size(m_aIds); i++)
		m_aIds[i] = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

void CHeadItem::Reset()
{
	for(size_t i = 0; i < std::size(m_aIds); i++)
		Server()->SnapFreeId(m_aIds[i]);

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
		if(pOwnerPl->Cosmetics()->m_HatType == HatType::None)
		{
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

	if(!pOwnerChr->TeamMask().test(SnappingClient))
		return;

	if(pSnapPlayer->GetCharacter() && pOwnerChr)
		if(!pOwnerChr->CanSnapCharacter(SnappingClient))
			return;

	if(pOwnerChr->GetPlayer()->m_Vanish && SnappingClient != pOwnerChr->GetPlayer()->GetCid() && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return;

	HatType PlHatType = pOwnerChr->GetPlayer()->Cosmetics()->m_HatType;

	if(m_Type != HEADITEM_SPAWNSOLO)
	{
		if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowHats)
			return;

		if(pOwnerChr->IsPaused())
			return;

		if(m_Type == HEADITEM_COSMETIC)
		{
			if(PlHatType == HatType::Party)
			{
				SnapPartyHat(SnappingClient);
				return;
			}
		}

		if(pOwnerChr->m_SpawnSolo)
			return;
	}

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
		SubType = (int)PlHatType - 1;
		Flags |= pOwnerChr->Acc()->m_Configs.m_HatItemFlags;
		break;
	default:
		break;
	}

	vec2 Pos = pOwnerChr->GetPredictedPos(SnappingClient) + m_Offset;

	GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), GetId(), Pos, Type, SubType, -1, Flags);
}

void CHeadItem::SnapPartyHat(int SnappingClient)
{
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	vec2 HatFrom[2] = {vec2(19.0f, -48.0f), vec2(19.0f, -48.0f)};
	vec2 HatTo[2] = {vec2(-13.5f, -14.0f), vec2(17.0f, -9.0f)};

	bool Still = abs(pOwnerChr->GetVelocity().x) < 0.01f && abs(pOwnerChr->GetVelocity().y) < 0.01f;

	 
	if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
	{
		for(int i = 0; i < 2; i++)
		{
			vec2 Center = vec2(0, 0);
			Collision()->Rotate(Center, &HatFrom[i], 0.2f);
			Collision()->Rotate(Center, &HatTo[i], 0.2f);
			HatFrom[i] += vec2(-1.5f, 3.5f);
			HatTo[i] += vec2(-1.5f, 3.5f);
		}
	}

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	bool Turn = normalize(vec2(pOwnerChr->Input()->m_TargetX, pOwnerChr->Input()->m_TargetY)).x > 0;

	for(size_t i = 0; i < std::size(m_aIds); i++)
	{
		if(Turn)
		{
			HatFrom[i].x = -HatFrom[i].x;
			HatTo[i].x = -HatTo[i].x;
		}

		vec2 Pos = pOwnerChr->GetPredictedPos(SnappingClient, false);
		vec2 From = Pos + HatFrom[i];
		vec2 To = Pos + HatTo[i];

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_aIds[i], From, To, Server()->Tick() - 4, m_Owner, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
