// Made by qxdFox
#include "headitem.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

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
	if(g_Config.m_SvLogExtra >= 2)
		log_info("headitem", "Reset");

	for(size_t i = 0; i < std::size(m_aIds); i++)
		Server()->SnapFreeId(m_aIds[i]);

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CHeadItem::Tick()
{
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
		Flags |= GetCharacter()->Acc()->m_Configs.m_HatItemFlags;
		break;
	default:
		break;
	}

	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient) + m_Offset;

	GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), GetId(), Pos, Type, SubType, -1, Flags);
}

void CHeadItem::SnapPartyHat(int SnappingClient)
{
	const int NumPoints = 2;
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	vec2 HatFrom[NumPoints] = {vec2(19.0f, -48.0f), vec2(19.0f, -48.0f)};
	vec2 HatTo[NumPoints] = {vec2(-13.5f, -14.0f), vec2(17.0f, -9.0f)};

	bool Still = abs(pOwnerChr->GetVelocity().x) < 0.01f && abs(pOwnerChr->GetVelocity().y) < 0.01f && pOwnerChr->IsGrounded();

	if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
	{
		for(int i = 0; i < NumPoints; i++)
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

	for(size_t i = 0; i < NumPoints; i++)
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

void CHeadItem::SnapTopHat(int SnappingClient)
{
	const vec2 Center = vec2(0, 0);
	const int NumPoints = 5;
	const int Now = Server()->Tick();
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	vec2 HatFrom[NumPoints] = {
		vec2(15.0f, -43.0f), // top line
		vec2(-12.5f, -44.0f), // right line down
		vec2(15.0f, -43.0f), // left line down
		vec2(26.0f, -19.0f), // bottom line
		vec2(-26.5f, -21.0f), // bottom dot
	};

	vec2 HatTo[NumPoints] = {
		vec2(-12.5f, -44.0f),// top line
		vec2(-12.5f, -20.0f), // right line down
		vec2(14.5f, -20.0f), // left line down
		vec2(-26.5f, -21.0f), // bottom line
		vec2(-26.5f, -21.0f), // bottom dot
	};

	int HatSnapTick[NumPoints] = {
		Now - 5, // top line
		Now - 5,// right line down
		Now - 5,// left line down
		Now - 4, // Long Bottom Line
		Now - 4 // bottom dot
	};

	bool Still = abs(pOwnerChr->GetVelocity().x) < 0.01f && abs(pOwnerChr->GetVelocity().y) < 0.01f && pOwnerChr->IsGrounded();
	for(int i = 0; i < NumPoints; i++)
	{
		Collision()->Rotate(Center, &HatFrom[i], 0.2f);
		Collision()->Rotate(Center, &HatTo[i], 0.2f);

		if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
		{
			Collision()->Rotate(Center, &HatFrom[i], 0.2f);
			Collision()->Rotate(Center, &HatTo[i], 0.2f);
			HatFrom[i] += vec2(-1.5f, 3.5f);
			HatTo[i] += vec2(-1.5f, 3.5f);
		}
	}

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	bool Turn = normalize(vec2(pOwnerChr->Input()->m_TargetX, pOwnerChr->Input()->m_TargetY)).x > 0;

	for(size_t i = 0; i < NumPoints; i++)
	{
		if(Turn)
		{
			HatFrom[i].x = -HatFrom[i].x;
			HatTo[i].x = -HatTo[i].x;
		}

		vec2 Pos = pOwnerChr->GetPredictedPos(SnappingClient, false);
		vec2 From = Pos + HatFrom[i];
		vec2 To = Pos + HatTo[i];
		int StartTick = HatSnapTick[i];

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_aIds[i], From, To, StartTick, m_Owner, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
