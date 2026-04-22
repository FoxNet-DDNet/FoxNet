#include "foxnet_entity.h"

#include <base/dbg.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/item_registry.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <base/math.h>

CEntityOwned::CEntityOwned(CGameWorld *pGameWorld, int Owner, int Objtype, vec2 Pos, int ProximityRadius) :
	CEntity(pGameWorld, DefaultMapIndex, Objtype, Pos, ProximityRadius)
{
	m_Owner = Owner;

	if(GetCharacter())
		m_StartTeamMask = TeamMask();
	if(GetPlayer())
		SetMultiMapIdx(GetPlayer()->MultiMapIdx());
}

bool CEntityOwned::CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer)
{
	if(m_MarkedForDestroy)
		return false;
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;

	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pSnapPlayer || !GetCharacter())
		return false;

	if(GetCharacter()->IsPaused())
		return false;

	if(!TeamMask().test(SnappingClient))
		return false;

	if(pSnapPlayer->GetCharacter() && GetCharacter())
		if(!GetCharacter()->CanSnapCharacter(SnappingClient))
			return false;

	if(GetPlayer()->m_Vanish && SnappingClient != GetPlayer()->GetCid() && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return false;

	if(ppSnapPlayer)
		*ppSnapPlayer = pSnapPlayer;

	return true;
}

CPlayer *CEntityOwned::GetPlayer()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id %d", m_Owner);
	// Should be dbg_assert but idk
	if(Server()->ClientSlotEmpty(m_Owner))
		return nullptr;
	return GameServer()->m_apPlayers[m_Owner];
}
CCharacter *CEntityOwned::GetCharacter()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id %d", m_Owner);
	// same
	if(Server()->ClientSlotEmpty(m_Owner))
		return nullptr;
	CPlayer *pPlayer = GetPlayer();
	if(!pPlayer)
		return nullptr;
	if(MultiMapIdx() != pPlayer->MultiMapIdx())
		SetMultiMapIdx(pPlayer->MultiMapIdx());
	return pPlayer->GetCharacter();
}

CCollision *CEntityOwned::GetCollision()
{
	dbg_assert(m_Owner >= 0 && m_Owner < MAX_CLIENTS, "invalid owner id %d", m_Owner);
	// same
	if(Server()->ClientSlotEmpty(m_Owner))
		return Collision();
	CPlayer *pPlayer = GetPlayer();
	if(!pPlayer)
		return Collision();
	return GameServer()->Collision(pPlayer->MultiMapIdx());
}

CClientMask CEntityOwned::CosmeticMask(EItemType ItemType)
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter)
		return CClientMask().set();

	return pCharacter->CosmeticMask(ItemType);
}

CClientMask CEntityOwned::TeamMask()
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter)
		return CClientMask().set();

	return pCharacter->TeamMask();
}

bool CEntityOwned::SnapCosmeticPickupPos(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Pos, int Type, int SubType, int Rotation, int Alpha, int Flags)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		return GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, Pos, Type, SubType, -1, OldFlags);
	}

	CNetObj_CosmeticPickup *pPickup = Server()->SnapNewItem<CNetObj_CosmeticPickup>(SnapId);
	if(!pPickup)
		return false;


	pPickup->m_X = (int)Pos.x;
	pPickup->m_Y = (int)Pos.y;
	pPickup->m_Type = Type;
	pPickup->m_Subtype = SubType;
	pPickup->m_Owner = Owner;
	pPickup->m_Alpha = Alpha;
	pPickup->m_Rotation = Rotation;
	pPickup->m_Flags = Flags;
	return true;
}

bool CEntityOwned::SnapCosmeticPickup(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Offset, int Type, int SubType, int Rotation, int Alpha, int Flags)
{
	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, true);

	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		return GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, Pos + Offset, Type, SubType, -1, OldFlags);
	}

	CNetObj_CosmeticPickup *pPickup = Server()->SnapNewItem<CNetObj_CosmeticPickup>(SnapId);
	if(!pPickup)
		return false;

	if(Flags & COSMETIC_FLAG_ANCHORED)
	{
		pPickup->m_X = (int)Offset.x;
		pPickup->m_Y = (int)Offset.y;
	}
	else
	{
		pPickup->m_X = (int)Offset.x + Pos.x;
		pPickup->m_Y = (int)Offset.y + Pos.y;
	}

	pPickup->m_Type = Type;
	pPickup->m_Subtype = SubType;
	pPickup->m_Owner = Owner;
	pPickup->m_Alpha = Alpha;
	pPickup->m_Rotation = Rotation;
	pPickup->m_Flags = Flags;
	return true;
}

int OffsetToHeight(int TickOffset)
{
	switch(TickOffset)
	{
	case 0: return 0;
	case 1: return 8;
	case 2: return 7;
	case 3: return 6;
	case 4: return 4;
	case 5: return 5;
	case 6: return 4;
	case 7: return 3;
	case 8: return 2;
	default: return 0;
	}
}

bool CEntityOwned::SnapCosmeticLaserPos(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha, int Flags)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		if(Alpha == 0)
			return false;
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		return GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, From, To, Server()->Tick() - TickOffset, Owner, Type, -1, -1, LASERFLAG_NO_PREDICT);
	}

	CNetObj_CosmeticLaser *pLaser = Server()->SnapNewItem<CNetObj_CosmeticLaser>(SnapId);
	if(!pLaser)
		return false;

	pLaser->m_FromX = (int)From.x;
	pLaser->m_FromY = (int)From.y;
	pLaser->m_ToX = (int)To.x;
	pLaser->m_ToY = (int)To.y;
	pLaser->m_TickOffset = OffsetToHeight(TickOffset);
	pLaser->m_Type = Type;
	pLaser->m_Owner = Owner;
	pLaser->m_Alpha = Alpha;
	pLaser->m_Flags = Flags;

	return true;
}

bool CEntityOwned::SnapCosmeticLaser(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha, int Flags)
{
	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, false);
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		if(Alpha == 0)
			return false;
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		return GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, Pos + From, Pos + To, Server()->Tick() - TickOffset, Owner, Type, -1, -1, LASERFLAG_NO_PREDICT);
	}

	CNetObj_CosmeticLaser *pLaser = Server()->SnapNewItem<CNetObj_CosmeticLaser>(SnapId);
	if(!pLaser)
		return false;

	if(Flags & COSMETIC_FLAG_ANCHORED)
	{
		pLaser->m_FromX = (int)From.x;
		pLaser->m_FromY = (int)From.y;
		pLaser->m_ToX = (int)To.x;
		pLaser->m_ToY = (int)To.y;
	}
	else
	{
		pLaser->m_FromX = (int)From.x + Pos.x;
		pLaser->m_FromY = (int)From.y + Pos.y;
		pLaser->m_ToX = (int)To.x + Pos.x;
		pLaser->m_ToY = (int)To.y + Pos.y;
	}

	pLaser->m_TickOffset = OffsetToHeight(TickOffset);
	pLaser->m_Type = Type;
	pLaser->m_Owner = Owner;
	pLaser->m_Alpha = Alpha;
	pLaser->m_Flags = Flags;

	return true;
}

bool CEntityOwned::SnapCosmeticProjectile(int SnappingClient, int SnapId, int Owner, const vec2 &Offset, const vec2 &Target, int StartTick, int Type, int Alpha, int Flags)
{
	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, false);

	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(SnapId);
		if(!pProj)
			return false;

		pProj->m_X = round_to_int((Pos.x + Offset.x) * 100.0f);
		pProj->m_Y = round_to_int((Pos.y + Offset.y) * 100.0f);
		pProj->m_Type = Type;
		pProj->m_Owner = Owner;
		pProj->m_StartTick = StartTick;
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		return true;
	}

	int Rot = 0;
	if(length(Target) > 0.00001f)
		Rot = round_to_int(angle(Target) * 180.0f / pi);

	CNetObj_CosmeticProjectile *pProjectile = Server()->SnapNewItem<CNetObj_CosmeticProjectile>(SnapId);
	if(!pProjectile)
		return false;

	if(Flags & COSMETIC_FLAG_ANCHORED)
	{
		pProjectile->m_X = (int)Offset.x;
		pProjectile->m_Y = (int)Offset.y;
	}
	else
	{
		pProjectile->m_X = (int)(Pos.x + Offset.x);
		pProjectile->m_Y = (int)(Pos.y + Offset.y);
	}

	pProjectile->m_X = (int)Offset.x;
	pProjectile->m_Y = (int)Offset.y;
	pProjectile->m_Type = Type;
	pProjectile->m_Owner = Owner;
	pProjectile->m_Alpha = Alpha;
	pProjectile->m_Rotation = Rot;
	pProjectile->m_Flags = Flags;
	return true;
}
