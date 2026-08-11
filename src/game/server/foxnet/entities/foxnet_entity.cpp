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
	CEntity(pGameWorld, DefaultMapIndex, Objtype, true, Pos, ProximityRadius)
{
	m_Owner = Owner;

	if(GetCharacter())
		m_StartTeamMask = TeamMask();
	if(GetPlayer())
		SetMultiMapIdx(GetPlayer()->MultiMapIdx());
}

bool CEntityOwned::CanSnapEntityMask(int SnappingClient, CClientMask Mask, CPlayer **ppSnapPlayer)
{
	if(m_MarkedForDestroy)
		return false;
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;

	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pSnapPlayer)
		return false;

	if(!Mask.test(SnappingClient))
		return false;

	if(ppSnapPlayer)
		*ppSnapPlayer = pSnapPlayer;

	return true;
}

bool CEntityOwned::CanSnapEntityNoChar(int SnappingClient, CPlayer **ppSnapPlayer)
{
	if(m_MarkedForDestroy)
		return false;
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;

	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pSnapPlayer)
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

bool CEntityOwned::CanSnapEntity(int SnappingClient, CPlayer **ppSnapPlayer)
{
	if(!GetCharacter())
		return false;

	if(GetCharacter()->IsPaused())
		return false;

	return CanSnapEntityNoChar(SnappingClient, ppSnapPlayer);
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

bool CEntityOwned::TranslateOwner(int SnappingClient, int *pOwner)
{
	if(*pOwner < 0)
		return true; // ownerless, nothing for the client to look up

	return Server()->Translate(*pOwner, SnappingClient);
}

CClientMask CEntityOwned::TeamMask()
{
	CCharacter *pCharacter = GetCharacter();
	if(!pCharacter)
		return CClientMask().set();

	return pCharacter->TeamMask();
}

void CEntityOwned::SnapCosmeticPickupPos(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Pos, int Type, int SubType, int Rotation, int Alpha, int Flags)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, Pos, Type, SubType, -1, OldFlags);
		return;
	}

	if(!TranslateOwner(SnappingClient, &Owner))
		return;

	CNetObj_CosmeticPickup Pickup = {};

	Pickup.m_X = (int)Pos.x;
	Pickup.m_Y = (int)Pos.y;
	Pickup.m_Type = Type;
	Pickup.m_Subtype = SubType;
	Pickup.m_Owner = Owner;
	Pickup.m_Alpha = Alpha;
	Pickup.m_Rotation = Rotation;
	Pickup.m_Flags = Flags;
	Server()->SnapNewItem(SnapId, Pickup);
}

void CEntityOwned::SnapCosmeticPickup(int SnappingClient, int SnapId, int OldFlags, int Owner, const vec2 &Offset, int Type, int SubType, int Rotation, int Alpha, int Flags)
{
	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, true);

	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, Pos + Offset, Type, SubType, -1, OldFlags);
		return;
	}

	if(!TranslateOwner(SnappingClient, &Owner))
		return;

	CNetObj_CosmeticPickup Pickup = {};

	if(Flags & COSMETIC_FLAG_ANCHORED)
	{
		Pickup.m_X = (int)Offset.x;
		Pickup.m_Y = (int)Offset.y;
	}
	else
	{
		Pickup.m_X = (int)Offset.x + Pos.x;
		Pickup.m_Y = (int)Offset.y + Pos.y;
	}

	Pickup.m_Type = Type;
	Pickup.m_Subtype = SubType;
	Pickup.m_Owner = Owner;
	Pickup.m_Alpha = Alpha;
	Pickup.m_Rotation = Rotation;
	Pickup.m_Flags = Flags;
	Server()->SnapNewItem(SnapId, Pickup);
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

void CEntityOwned::SnapCosmeticLaserPos(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha, int Flags)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		if(Alpha == 0)
			return;
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, From, To, Server()->Tick() - TickOffset, Owner, Type, -1, -1, LASERFLAG_NO_PREDICT);
		return;
	}

	if(!TranslateOwner(SnappingClient, &Owner))
		return;

	CNetObj_CosmeticLaser Laser = {};

	Laser.m_FromX = (int)From.x;
	Laser.m_FromY = (int)From.y;
	Laser.m_ToX = (int)To.x;
	Laser.m_ToY = (int)To.y;
	Laser.m_TickOffset = OffsetToHeight(TickOffset);
	Laser.m_Type = Type;
	Laser.m_Owner = Owner;
	Laser.m_Alpha = Alpha;
	Laser.m_Flags = Flags;
	Server()->SnapNewItem(SnapId, Laser);
}

void CEntityOwned::SnapCosmeticLaser(int SnappingClient, int SnapId, int Owner, const vec2 &From, const vec2 &To, int TickOffset, int Type, int Alpha, int Flags)
{
	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, false);
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		if(Alpha == 0)
			return;
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, Pos + From, Pos + To, Server()->Tick() - TickOffset, Owner, Type, -1, -1, LASERFLAG_NO_PREDICT);
		return;
	}

	if(!TranslateOwner(SnappingClient, &Owner))
		return;

	CNetObj_CosmeticLaser Laser = {};

	if(Flags & COSMETIC_FLAG_ANCHORED)
	{
		Laser.m_FromX = (int)From.x;
		Laser.m_FromY = (int)From.y;
		Laser.m_ToX = (int)To.x;
		Laser.m_ToY = (int)To.y;
	}
	else
	{
		Laser.m_FromX = (int)From.x + Pos.x;
		Laser.m_FromY = (int)From.y + Pos.y;
		Laser.m_ToX = (int)To.x + Pos.x;
		Laser.m_ToY = (int)To.y + Pos.y;
	}

	Laser.m_TickOffset = OffsetToHeight(TickOffset);
	Laser.m_Type = Type;
	Laser.m_Owner = Owner;
	Laser.m_Alpha = Alpha;
	Laser.m_Flags = Flags;
	Server()->SnapNewItem(SnapId, Laser);
}

void CEntityOwned::SnapCosmeticProjectile(int SnappingClient, int SnapId, int Owner, const vec2 &Offset, const vec2 &Target, int StartTick, int Type, int Alpha, int Flags)
{
	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, false);

	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		int ProjOwner = Owner;
		if(!TranslateOwner(SnappingClient, &ProjOwner))
			ProjOwner = -1;

		CNetObj_DDNetProjectile Proj = {};

		Proj.m_X = round_to_int((Pos.x + Offset.x) * 100.0f);
		Proj.m_Y = round_to_int((Pos.y + Offset.y) * 100.0f);
		Proj.m_Type = Type;
		Proj.m_Owner = ProjOwner;
		Proj.m_StartTick = StartTick;
		Proj.m_VelX = 0;
		Proj.m_VelY = 0;
		Server()->SnapNewItem(SnapId, Proj);
		return;
	}

	if(!TranslateOwner(SnappingClient, &Owner))
		return;

	int Rot = 0;
	if(length(Target) > 0.00001f)
		Rot = round_to_int(angle(Target) * 180.0f / pi);

	CNetObj_CosmeticProjectile Projectile = {};

	if(Flags & COSMETIC_FLAG_ANCHORED)
	{
		Projectile.m_X = (int)Offset.x;
		Projectile.m_Y = (int)Offset.y;
	}
	else
	{
		Projectile.m_X = (int)(Pos.x + Offset.x);
		Projectile.m_Y = (int)(Pos.y + Offset.y);
	}

	Projectile.m_X = (int)Offset.x;
	Projectile.m_Y = (int)Offset.y;
	Projectile.m_Type = Type;
	Projectile.m_Owner = Owner;
	Projectile.m_Alpha = Alpha;
	Projectile.m_Rotation = Rot;
	Projectile.m_Flags = Flags;
	Server()->SnapNewItem(SnapId, Projectile);
}

void CEntityOwned::SnapCosmeticProjectilePos(int SnappingClient, int SnapId,  int Type, vec2 Pos, vec2 Dir)
{
	const int Id = SnapId;

	const int StartTick = Server()->Tick();

	if(length(Dir) > 0.00001f)
		Dir = normalize(Dir);

	bool Supports = SnappingClient != SERVER_DEMO_CLIENT && GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps;
	if(Supports)
	{
		int Owner = m_Owner;
		if(!TranslateOwner(SnappingClient, &Owner))
			return;

		int Rotation = round_to_int(angle(Dir) * 180.0f / pi);
		Rotation = ((Rotation % 360) + 360) % 360;

		CNetObj_CosmeticProjectile Projectile = {};
		Projectile.m_X = (int)Pos.x;
		Projectile.m_Y = (int)Pos.y;
		Projectile.m_Type = Type;
		Projectile.m_Owner = Owner;
		Projectile.m_Alpha = -1;
		Projectile.m_Rotation = Rotation;
		Projectile.m_Flags = 0;
		Server()->SnapNewItem(Id, Projectile);
		return;
	}

	const int Ver = GameServer()->GetClientVersion(SnappingClient);
	const float Factor = 0;
	const int MaxPos = 0x7fffffff / 100;
	bool LegacyCompatible = !(absolute((int)Pos.y) + 1 >= MaxPos || absolute((int)Pos.x) + 1 >= MaxPos);

	if(Ver >= VERSION_DDNET_ENTITY_NETOBJS)
	{
		CNetObj_DDNetProjectile Proj = {};
		Proj.m_X = round_to_int(Pos.x * 100.0f);
		Proj.m_Y = round_to_int(Pos.y * 100.0f);
		Proj.m_VelX = round_to_int(Dir.x * Factor * 1e6f);
		Proj.m_VelY = round_to_int(Dir.y * Factor * 1e6f);
		Proj.m_Type = Type;
		Proj.m_StartTick = StartTick;
		Proj.m_Owner = -1;
		Proj.m_SwitchNumber = 0;
		Proj.m_TuneZone = 0;
		Proj.m_Flags = 0;
		Server()->SnapNewItem(Id, Proj);
	}
	else if(Ver >= VERSION_DDNET_ANTIPING_PROJECTILE && LegacyCompatible)
	{
		float Angle = -std::atan2(Dir.x, Dir.y);
		int Data = LEGACYPROJECTILEFLAG_IS_DDNET | LEGACYPROJECTILEFLAG_NO_OWNER;

		CNetObj_DDRaceProjectile Proj = {};
		Proj.m_X = (int)(Pos.x * 100.0f);
		Proj.m_Y = (int)(Pos.y * 100.0f);
		Proj.m_Angle = (int)(Angle * 1000000.0f);
		Proj.m_Data = Data;
		Proj.m_StartTick = StartTick;
		Proj.m_Type = Type;

		if(Ver >= VERSION_DDNET_MSG_LEGACY)
		{
			Server()->SnapNewItem(Id, Proj);
		}
		else
		{
			CNetObj_Projectile Projectile = {};
			static_assert(sizeof(Proj) == sizeof(Projectile));
			mem_copy(&Projectile, &Proj, sizeof(Projectile));
			Server()->SnapNewItem(Id, Projectile);
		}
	}
	else
	{
		CNetObj_Projectile Proj = {};
		Proj.m_X = (int)Pos.x;
		Proj.m_Y = (int)Pos.y;
		Proj.m_VelX = (int)(Dir.x * Factor * 100.0f);
		Proj.m_VelY = (int)(Dir.y * Factor * 100.0f);
		Proj.m_StartTick = StartTick;
		Proj.m_Type = Type;
		Server()->SnapNewItem(Id, Proj);
	}
}
