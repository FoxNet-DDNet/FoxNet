// Made by qxdFox
#include "headitem.h"

#include <base/log.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>

CHeadItem::CHeadItem(CGameWorld *pGameWorld, int Owner, vec2 Pos, int Type, vec2 Offset) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_HEAD_ITEM, Pos)
{
	m_Pos = Pos;

	// Type of Entity
	m_Type = Type;
	m_Offset = Offset;

	for(size_t i = 0; i < std::size(m_aIds); i++)
		m_aIds[i] = Server()->SnapNewId();
	std::sort(std::begin(m_aIds), std::end(m_aIds));

	GameWorld()->InsertEntity(this);
}

void CHeadItem::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("headitem", "Reset");

	for(size_t i = 0; i < std::size(m_aIds); i++)
		Server()->SnapFreeId(m_aIds[i]);

	m_MarkedForDestroy = true;
}

void CHeadItem::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(!GetPlayer())
	{
		Reset();
		return;
	}

	switch(m_Type)
	{
	case HEADITEM_SPAWNSOLO:
		if(!GetCharacter() || !GetCharacter()->m_SpawnSolo)
		{
			Reset();
			return;
		}
		break;
	case HEADITEM_COSMETIC:
		if(GetPlayer()->Cosmetics()->m_HatType == EHatType::None)
		{
			Reset();
			return;
		}
		break;
	default:
		Reset();
		return;
	}

	if(GetCharacter())
		m_Pos = GetCharacter()->GetPos();
}

void CHeadItem::Snap(int SnappingClient)
{
	if(SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return;

	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	EHatType PlHatType = GetPlayer()->Cosmetics()->m_HatType;

	if(m_Type != HEADITEM_SPAWNSOLO)
	{
		if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowHats)
			return;

		if(m_Type == HEADITEM_COSMETIC)
		{
			if(PlHatType == EHatType::Party)
			{
				SnapPartyHat(SnappingClient);
				return;
			}
			else if(PlHatType == EHatType::Tophat)
			{
				SnapTopHat(SnappingClient);
				return;
			}
		}

		if(GetCharacter()->m_SpawnSolo)
			return;
	}

	int Type = 0;
	int SubType = 0;
	int Flags = 0;

	switch(m_Type)
	{
	case HEADITEM_SPAWNSOLO:
		Type = POWERUP_ARMOR;
		SubType = 0;
		break;
	case HEADITEM_COSMETIC:
		Type = POWERUP_WEAPON;
		SubType = (int)PlHatType - 1;
		Flags |= GetCharacter()->Acc()->m_Configs.m_HatItemFlags;
		break;
	default:
		break;
	}

	int Rotation = 0;
	if(Flags == PICKUPFLAG_ROTATE + PICKUPFLAG_XFLIP + PICKUPFLAG_YFLIP)
		Rotation = 270;
	else if(Flags == PICKUPFLAG_XFLIP + PICKUPFLAG_YFLIP)
		Rotation = 180;
	else if(Flags == PICKUPFLAG_ROTATE)
		Rotation = 90;

	Flags |= PICKUPFLAG_NO_PREDICT;

	SnapCosmeticPickup(SnappingClient, GetId(), Flags, m_Owner, m_Offset, Type, SubType, Rotation, -1, COSMETIC_FLAG_ANCHORED);
}

void CHeadItem::SnapPartyHat(int SnappingClient)
{
	const int NumPoints = 2;
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	vec2 HatFrom[NumPoints] = {vec2(19.0f, -48.0f), vec2(19.0f, -48.0f)};
	vec2 HatTo[NumPoints] = {vec2(-13.5f, -14.0f), vec2(17.0f, -9.0f)};
	int Flags[NumPoints] = {COSMETIC_FLAG_ANCHORED, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD};

	bool Still = abs(pOwnerChr->GetVelocity().x) < 0.01f && abs(pOwnerChr->GetVelocity().y) < 0.01f && pOwnerChr->IsGrounded();

	if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
	{
		for(int i = 0; i < NumPoints; i++)
		{
			vec2 Center = vec2(0, 0);
			Rotate(Center, &HatFrom[i], 0.2f);
			Rotate(Center, &HatTo[i], 0.2f);
			HatFrom[i] += vec2(-1.5f, 3.5f);
			HatTo[i] += vec2(-1.5f, 3.5f);
		}
	}

	bool Turn = normalize(vec2(pOwnerChr->Input()->m_TargetX, pOwnerChr->Input()->m_TargetY)).x > 0;

	for(size_t i = 0; i < NumPoints; i++)
	{
		if(Turn)
		{
			HatFrom[i].x = -HatFrom[i].x;
			HatTo[i].x = -HatTo[i].x;
		}

		SnapCosmeticLaser(SnappingClient, m_aIds[i], m_Owner, HatFrom[i], HatTo[i], 4, LASERTYPE_GUN, -1, Flags[i]);
	}
}

void CHeadItem::SnapTopHat(int SnappingClient)
{
	const vec2 Center = vec2(0, 0);
	const int NumPoints = 5;
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	vec2 HatFrom[NumPoints] = {
		vec2(15.0f, -43.0f), // top line
		vec2(-12.5f, -44.0f), // right line down
		vec2(15.0f, -43.0f), // left line down
		vec2(26.0f, -19.0f), // bottom line
		vec2(-26.5f, -21.0f), // bottom dot
	};

	vec2 HatTo[NumPoints] = {
		vec2(-12.5f, -44.0f), // top line
		vec2(-12.5f, -20.0f), // right line down
		vec2(14.5f, -20.0f), // left line down
		vec2(-26.5f, -21.0f), // bottom line
		vec2(-26.5f, -21.0f), // bottom dot
	};

	int HatTickOffset[NumPoints] = {
		5, // top line
		5, // right line down
		5, // left line down
		4, // Long Bottom Line
		4 // bottom dot
	};

	int Flags[NumPoints] = {
		COSMETIC_FLAG_ANCHORED,
		COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD,
		COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD,
		COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD | COSMETIC_LASER_FLAG_TO_HEAD,
		COSMETIC_FLAG_ANCHORED};

	bool Still = abs(pOwnerChr->GetVelocity().x) < 0.01f && abs(pOwnerChr->GetVelocity().y) < 0.01f && pOwnerChr->IsGrounded();
	for(int i = 0; i < NumPoints; i++)
	{
		Rotate(Center, &HatFrom[i], 0.2f);
		Rotate(Center, &HatTo[i], 0.2f);

		if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
		{
			Rotate(Center, &HatFrom[i], 0.2f);
			Rotate(Center, &HatTo[i], 0.2f);
			HatFrom[i] += vec2(-1.5f, 3.5f);
			HatTo[i] += vec2(-1.5f, 3.5f);
		}
	}
	bool Turn = normalize(vec2(pOwnerChr->Input()->m_TargetX, pOwnerChr->Input()->m_TargetY)).x > 0;

	for(size_t i = 0; i < NumPoints; i++)
	{
		if(Turn)
		{
			HatFrom[i].x = -HatFrom[i].x;
			HatTo[i].x = -HatTo[i].x;
		}
		SnapCosmeticLaser(SnappingClient, m_aIds[i], m_Owner, HatFrom[i], HatTo[i], HatTickOffset[i], LASERTYPE_GUN, -1, Flags[i]);
	}
}
