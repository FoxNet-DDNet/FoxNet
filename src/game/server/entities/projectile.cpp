/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "projectile.h"

#include "character.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/entity.h>
#include <game/server/foxnet/item_registry.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/ddnet.h>

CProjectile::CProjectile(
	CGameWorld *pGameWorld,
	int MultiMapIdx, // FoxNet
	int Type,
	int Owner,
	vec2 Pos,
	vec2 Dir,
	int Span,
	int FreezeTicks,
	bool Explosive,
	int SoundImpact,
	vec2 InitDir,
	int Layer,
	int Number) :
	CEntity(pGameWorld, MultiMapIdx, CGameWorld::ENTTYPE_PROJECTILE, true)
{
	m_Type = Type;
	m_Pos = Pos;
	m_Direction = Dir;
	m_LifeSpan = Span;
	m_Owner = Owner;
	m_SoundImpact = SoundImpact;
	m_StartTick = Server()->Tick();
	m_Explosive = Explosive;

	m_Layer = Layer;
	m_Number = Number;
	m_Bouncing = 0;
	m_FreezeTicks = FreezeTicks;

	m_InitDir = InitDir;
	// m_TuneZone = Collision()->IsTune(Collision()->GetMapIndex(m_Pos));

	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	m_TuneZone = pOwnerChar ? pOwnerChar->GetOverriddenTuneZone() : Collision()->IsTune(Collision()->GetMapIndex(m_Pos));
	m_BelongsToPracticeTeam = pOwnerChar && pOwnerChar->Teams()->IsPractice(pOwnerChar->Team());
	m_DDRaceTeam = m_Owner == -1 ? 0 : GameServer()->GetDDRaceTeam(m_Owner);
	m_IsSolo = pOwnerChar && pOwnerChar->GetCore().m_Solo;

	// <FoxNet
	m_CosmeticMaskGun = m_CosmeticMaskGunHit = CClientMask().set();
	m_OppCosmeticMaskGun = m_OppCosmeticMaskGunHit = CClientMask().set().reset();

	CPlayer *pOwner = m_Owner >= 0 ? GameServer()->m_apPlayers[m_Owner] : nullptr;

	if(pOwner)
	{
		m_EmoteGun = pOwner->Cosmetics()->m_EmoticonGun;
		m_ConfettiGun = pOwner->Cosmetics()->m_ConfettiGun;
		m_PhaseGun = pOwner->Cosmetics()->m_PhaseGun;
		m_DamageIndEffect = pOwner->Cosmetics()->m_DamageIndType;

		CCosmetics *pCosmetics = pOwner->Cosmetics();
		m_GunType = pCosmetics->m_GunType;
		if(pCosmetics->m_GunType != EGunType::None)
			m_LifeSpan *= 1.25;
		if(m_GunType == EGunType::Laser)
			m_ExtraId = Server()->SnapNewId();
	}

	if(pOwnerChar)
	{
		m_CosmeticMaskGun = pOwnerChar->CosmeticMask(EItemType::Gun);
		m_OppCosmeticMaskGun = pOwnerChar->OppositeCosmeticMask(EItemType::Gun);

		m_CosmeticMaskGunHit = pOwnerChar->CosmeticMask(EItemType::Indicator);
		m_OppCosmeticMaskGunHit = pOwnerChar->OppositeCosmeticMask(EItemType::Indicator);

		m_MixedShield = pOwnerChar->m_MixedShield;
		pOwnerChar->m_MixedShield = !pOwnerChar->m_MixedShield;
	}
	// FoxNet>

	GameWorld()->InsertEntity(this);
}

void CProjectile::Reset()
{
	m_MarkedForDestroy = true;
	if(m_ExtraId.has_value())
	{
		if(g_Config.m_SvLogExtra >= 2)
			log_info("projectile", "Extra Id Reset");
		Server()->SnapFreeId(m_ExtraId.value());
		m_ExtraId.reset();
	}
}

vec2 CProjectile::GetPos(float Time, int ClientId)
{
	float Curvature = 0;
	float Speed = 0;
	CTuningParams *pTuning = GetTuning(m_TuneZone);

	switch(m_Type)
	{
	case WEAPON_GRENADE:
		Curvature = pTuning->m_GrenadeCurvature;
		Speed = pTuning->m_GrenadeSpeed;
		break;

	case WEAPON_SHOTGUN:
		Curvature = pTuning->m_ShotgunCurvature;
		Speed = pTuning->m_ShotgunSpeed;
		break;

	case WEAPON_GUN:
		Curvature = pTuning->m_GunCurvature;
		Speed = pTuning->m_GunSpeed;

		CPlayer *pSnapPl = ClientId >= 0 ? GameServer()->m_apPlayers[ClientId] : nullptr;
		if(pSnapPl && (pSnapPl->Acc()->m_Configs.m_Cosmetics.m_ShowGuns || ClientId == m_Owner))
		{
			if(m_GunType != EGunType::None)
				Speed = 1100.0f;
		}
		break;
	}

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CProjectile::Tick()
{
	float Pt = (Server()->Tick() - m_StartTick - 1) / (float)Server()->TickSpeed();
	float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	vec2 ColPos;
	vec2 NewPos;
	int Collide = Collision()->IntersectLine(PrevPos, CurPos, &ColPos, &NewPos);
	CCharacter *pOwnerChar = nullptr;
	CPlayer *pOwner = nullptr;

	if(m_Owner >= 0)
	{
		pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
		pOwner = GameServer()->m_apPlayers[m_Owner];
	}

	CCharacter *pTargetChr = nullptr;

	if(pOwnerChar ? !pOwnerChar->GrenadeHitDisabled() : g_Config.m_SvHit)
		pTargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, ColPos, m_FreezeTicks ? 1.0f : 6.0f, ColPos, pOwnerChar, m_Owner);

	// <FoxNet
	if((pTargetChr && pTargetChr->Core()->m_Passive) || (pOwnerChar && pOwnerChar->Core()->m_Passive))
		pTargetChr = nullptr;

	if(pTargetChr && !pTargetChr->Core()->m_Hittable)
		pTargetChr = nullptr;

	bool GLClipped = GameLayerClipped(CurPos);
	if(pOwner)
	{
		GLClipped = GameLayerClipped(CurPos) && !pOwner->m_IgnoreGamelayer;
	}
	// FoxNet>

	if(m_LifeSpan > -1)
		m_LifeSpan--;

	CClientMask TeamMask = CClientMask().set();
	bool IsWeaponCollide = false;
	if(
		pOwnerChar &&
		pTargetChr &&
		pOwnerChar->IsAlive() &&
		pTargetChr->IsAlive() &&
		!pTargetChr->CanCollide(m_Owner))
	{
		IsWeaponCollide = true;
	}
	if(pOwnerChar && pOwnerChar->IsAlive())
	{
		TeamMask = pOwnerChar->TeamMask();
	}
	else if(m_Owner >= 0 && (m_Type != WEAPON_GRENADE || g_Config.m_SvDestroyBulletsOnDeath || m_BelongsToPracticeTeam))
	{
		m_MarkedForDestroy = true;
		return;
	}

	if(((pTargetChr && (pOwnerChar ? !pOwnerChar->GrenadeHitDisabled() : g_Config.m_SvHit || m_Owner == -1 || pTargetChr == pOwnerChar)) || Collide || GLClipped) && !IsWeaponCollide)
	{
		if(m_Explosive /*??*/ && (!pTargetChr || (pTargetChr && (m_FreezeTicks <= 0 || (m_Type == WEAPON_SHOTGUN && Collide)))))
		{
			int Number = 1;
			if(GameServer()->EmulateBug(BUG_GRENADE_DOUBLEEXPLOSION) && m_LifeSpan == -1)
			{
				Number = 2;
			}
			for(int i = 0; i < Number; i++)
			{
				GameServer()->CreateExplosion(ColPos, m_Owner, m_Type, m_Owner == -1, (!pTargetChr ? -1 : pTargetChr->Team()), MultiMapIdx(),
					(m_Owner != -1) ? TeamMask : CClientMask().set());
				GameServer()->CreateSound(ColPos, m_SoundImpact,
					(m_Owner != -1) ? TeamMask : CClientMask().set());
			}
		}
		else if(m_FreezeTicks > 0)
		{
			CEntity *apEnts[MAX_CLIENTS];
			int Num = GameWorld()->FindEntities(CurPos, 1.0f, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER, MultiMapIdx());
			for(int i = 0; i < Num; ++i)
			{
				auto *pChr = static_cast<CCharacter *>(apEnts[i]);
				if(pChr && (m_Layer != LAYER_SWITCH || (m_Layer == LAYER_SWITCH && m_Number > 0 && Switchers()[m_Number].m_aStatus[pChr->Team()])))
					pChr->FreezeTicks(m_FreezeTicks);
			}
		}
		else if(pTargetChr)
			pTargetChr->TakeDamage(vec2(0, 0), 0, m_Owner, m_Type);

		if(pOwnerChar && !GLClipped &&
			((m_Type == WEAPON_GRENADE && pOwnerChar->HasTelegunGrenade()) || (m_Type == WEAPON_GUN && pOwnerChar->HasTelegunGun())))
		{
			int MapIdx = Collision()->GetPureMapIndex(pTargetChr ? pTargetChr->m_Pos : ColPos);
			int TileFIndex = Collision()->GetFrontTileIndex(MapIdx);
			bool IsSwitchTeleGun = Collision()->GetSwitchType(MapIdx) == TILE_ALLOW_TELE_GUN;
			bool IsBlueSwitchTeleGun = Collision()->GetSwitchType(MapIdx) == TILE_ALLOW_BLUE_TELE_GUN;

			if(IsSwitchTeleGun || IsBlueSwitchTeleGun)
			{
				// Delay specifies which weapon the tile should work for.
				// Delay = 0 means all.
				int Delay = Collision()->GetSwitchDelay(MapIdx);

				if(Delay == 1 && m_Type != WEAPON_GUN)
					IsSwitchTeleGun = IsBlueSwitchTeleGun = false;
				if(Delay == 2 && m_Type != WEAPON_GRENADE)
					IsSwitchTeleGun = IsBlueSwitchTeleGun = false;
				if(Delay == 3 && m_Type != WEAPON_LASER)
					IsSwitchTeleGun = IsBlueSwitchTeleGun = false;
			}

			if(TileFIndex == TILE_ALLOW_TELE_GUN || TileFIndex == TILE_ALLOW_BLUE_TELE_GUN || IsSwitchTeleGun || IsBlueSwitchTeleGun || pTargetChr)
			{
				bool Found;
				vec2 PossiblePos;

				if(!Collide)
					Found = GetNearestAirPosPlayer(pTargetChr ? pTargetChr->m_Pos : ColPos, &PossiblePos);
				else
					Found = GetNearestAirPos(NewPos, CurPos, &PossiblePos);

				if(Found)
				{
					pOwnerChar->m_TeleGunPos = PossiblePos;
					pOwnerChar->m_TeleGunTeleport = true;
					pOwnerChar->m_IsBlueTeleGunTeleport = TileFIndex == TILE_ALLOW_BLUE_TELE_GUN || IsBlueSwitchTeleGun;
				}
			}
		}

		// Wall pass-through gun cosmetics: the bullet keeps living on a wall hit for
		// everyone who sees the cosmetic (Snowflake bounces, PhaseGun passes through).
		// A player hit (pTargetChr) is a normal kill and falls through to HandleGunHit.
		bool SnowflakeWall = m_GunType == EGunType::Snowflake && Collide;
		bool PhaseWall = m_PhaseGun && Collide && !pTargetChr;
		if(SnowflakeWall || PhaseWall)
		{
			vec2 Vel = CurPos - PrevPos;
			vec2 HitDir = length_squared(Vel) > 0.0f ? normalize(Vel) : m_Direction;

			// To viewers with the gun cosmetic off a plain bullet would have died on
			// this first wall, so show them that impact once and then hide the ghost
			// (Snap() drops it while m_HasHitOnce is set).
			if(!m_HasHitOnce)
			{
				m_HasHitOnce = true;
				CreateCosmeticGunWallImpact(CurPos, HitDir);
			}

			if(SnowflakeWall)
			{
				// Bounce off the wall. Cap the step MovePoint takes so it can't tunnel
				// straight through a tile: the full per-tick velocity is larger than a
				// tile, so when the bullet starts inside a solid - e.g. fired straight
				// down while standing on a tile, since the muzzle spawns a few pixels into
				// the floor - MovePoint would otherwise jump clear through to the far side
				// and keep going instead of reflecting.
				const float MaxStep = 16.0f;
				if(length(Vel) > MaxStep)
					Vel = normalize(Vel) * MaxStep;

				m_StartTick = Server()->Tick();
				m_Pos = NewPos;
				Collision()->MovePoint(&m_Pos, &Vel, 1.0f, nullptr);

				if(length_squared(Vel) > 0.0f)
					m_Direction = normalize(Vel);
			}
			// PhaseWall leaves position/direction untouched so the bullet flies straight
			// through the wall and keeps going.

			return;
		}

		if(Collide && m_Bouncing != 0)
		{
			m_StartTick = Server()->Tick();
			m_Pos = NewPos + (-(m_Direction * 4));
			if(m_Bouncing == 1)
				m_Direction.x = -m_Direction.x;
			else if(m_Bouncing == 2)
				m_Direction.y = -m_Direction.y;
			if(absolute(m_Direction.x) < 1e-6f)
				m_Direction.x = 0;
			if(absolute(m_Direction.y) < 1e-6f)
				m_Direction.y = 0;
			m_Pos += m_Direction;
		}
		else if(m_Type == WEAPON_GUN)
		{
			HandleGunHit(NewPos, TeamMask, pOwner, pTargetChr);
			return;
		}
		else
		{
			if(m_FreezeTicks <= 0)
			{
				m_MarkedForDestroy = true;
				return;
			}
		}
	}
	if(m_LifeSpan == -1)
	{
		if(m_Explosive)
		{
			if(m_Owner >= 0)
				pOwnerChar = GameServer()->GetPlayerChar(m_Owner);

			TeamMask = CClientMask().set();
			if(pOwnerChar && pOwnerChar->IsAlive())
			{
				TeamMask = pOwnerChar->TeamMask();
			}

			GameServer()->CreateExplosion(ColPos, m_Owner, m_Type, m_Owner == -1, (!pOwnerChar ? -1 : pOwnerChar->Team()), MultiMapIdx(),
				(m_Owner != -1) ? TeamMask : CClientMask().set());
			GameServer()->CreateSound(ColPos, m_SoundImpact,
				(m_Owner != -1) ? TeamMask : CClientMask().set());
		}
		m_MarkedForDestroy = true;
		return;
	}

	int x = Collision()->GetIndex(PrevPos, CurPos);
	int z;
	if(g_Config.m_SvOldTeleportWeapons)
		z = Collision()->IsTeleport(x);
	else
		z = Collision()->IsTeleportWeapon(x);
	if(z && !Collision()->TeleOuts(z - 1).empty())
	{
		int TeleOut = GameServer()->m_World.m_Core.RandomOr0(Collision()->TeleOuts(z - 1).size());
		m_Pos = Collision()->TeleOuts(z - 1)[TeleOut];
		m_StartTick = Server()->Tick();
	}
}

void CProjectile::HandleGunHit(vec2 NewPos, CClientMask Mask, CPlayer *pOwner, CCharacter *pTargetChr)
{
	float Pt = (Server()->Tick() - m_StartTick - 1) / (float)Server()->TickSpeed();
	float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);

	vec2 Direction = normalize(NewPos - PrevPos);
	if(Direction == vec2(0, 0))
		Direction = m_Direction;

	CCharacter *pOwnerChr = nullptr;
	if(pOwner)
		pOwnerChr = pOwner->GetCharacter();

	if(m_EmoteGun && pTargetChr)
	{
		const int TargetId = pTargetChr->GetPlayer()->GetCid();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_CosmeticMaskGun.test(i))
			{
				CClientMask TeamMask = CClientMask().set();

				if(pOwnerChr && pOwnerChr->IsAlive())
					TeamMask = pOwnerChr->TeamMask();

				GameServer()->SendEmote(TargetId, m_EmoteGun - 1, i);
				m_CosmeticMaskGun.reset(i);
				m_OppCosmeticMaskGun.reset(i);
			}
		}
	}

	// Fancy hit effect for viewers with gun-hit effects on, plain damage indicator
	// for viewers with them off.
	CClientMask FancyMask = m_CosmeticMaskGunHit;
	CClientMask PlainMask = m_OppCosmeticMaskGunHit;

	// If this is a pass-through bullet (Snowflake / PhaseGun) that already "died" at a
	// wall for viewers with the gun cosmetic off, don't give them a phantom hit effect
	// somewhere they can no longer see the bullet - restrict to gun-cosmetic viewers.
	if(m_HasHitOnce)
	{
		FancyMask &= m_CosmeticMaskGun;
		PlainMask &= m_CosmeticMaskGun;
	}

	if(m_ConfettiGun)
	{
		vec2 AirPos;
		GetNearestAirPos(NewPos, CurPos, &AirPos);
		GameServer()->CreateBirthdayEffect(AirPos, FancyMask);
	}
	else
	{
		GameServer()->CreateIndEffect(m_DamageIndEffect, CurPos, Direction, FancyMask);
	}

	if(pOwnerChr)
		GameServer()->CreateDamageInd(CurPos, -std::atan2(Direction.x, Direction.y), 10, PlainMask);

	m_MarkedForDestroy = true;
}

void CProjectile::CreateCosmeticGunWallImpact(vec2 Pos, vec2 Direction)
{
	// Reproduce a plain bullet's wall-death impact, but only for viewers with the gun
	// cosmetic off (m_OppCosmeticMaskGun already excludes the owner). Style it per each
	// viewer's own gun-hit-effect preference: fancy for those who have them on, plain
	// for those who don't.
	CClientMask FancyMask = m_OppCosmeticMaskGun & m_CosmeticMaskGunHit;
	CClientMask PlainMask = m_OppCosmeticMaskGun & m_OppCosmeticMaskGunHit;

	GameServer()->CreateIndEffect(m_DamageIndEffect, Pos, Direction, FancyMask);
	GameServer()->CreateDamageInd(Pos, -std::atan2(Direction.x, Direction.y), 10, PlainMask);
}

void CProjectile::TickPaused()
{
	++m_StartTick;
}

CNetObj_Projectile CProjectile::NetInfoVanilla() const
{
	CNetObj_Projectile Result = {};
	Result.m_X = (int)m_Pos.x;
	Result.m_Y = (int)m_Pos.y;
	Result.m_VelX = (int)(m_Direction.x * 100.0f);
	Result.m_VelY = (int)(m_Direction.y * 100.0f);
	Result.m_StartTick = m_StartTick;
	Result.m_Type = m_Type;
	return Result;
}

bool CProjectile::NetIsInfoLegacyCompatible() const
{
	const int MaxPos = 0x7fffffff / 100;
	if(absolute((int)m_Pos.y) + 1 >= MaxPos || absolute((int)m_Pos.x) + 1 >= MaxPos)
	{
		//If the modified data would be too large to fit in an integer, send normal data instead
		return false;
	}
	return true;
}

CNetObj_DDRaceProjectile CProjectile::NetInfoLegacy() const
{
	dbg_assert(NetIsInfoLegacyCompatible(), "can't send incompatible projectile");

	//Send additional/modified info, by modifying the fields of the netobj
	float Angle = -std::atan2(m_Direction.x, m_Direction.y);

	int Data = 0;
	Data |= (absolute(m_Owner) & 255) << 0;
	if(m_Owner < 0)
		Data |= LEGACYPROJECTILEFLAG_NO_OWNER;
	//This bit tells the client to use the extra info
	Data |= LEGACYPROJECTILEFLAG_IS_DDNET;
	// LEGACYPROJECTILEFLAG_BOUNCE_HORIZONTAL, LEGACYPROJECTILEFLAG_BOUNCE_VERTICAL
	Data |= (m_Bouncing & 3) << 10;
	if(m_Explosive)
		Data |= LEGACYPROJECTILEFLAG_EXPLOSIVE;
	if(m_FreezeTicks > 0)
		Data |= LEGACYPROJECTILEFLAG_FREEZE;

	CNetObj_DDRaceProjectile Result = {};
	Result.m_X = (int)(m_Pos.x * 100.0f);
	Result.m_Y = (int)(m_Pos.y * 100.0f);
	Result.m_Angle = (int)(Angle * 1000000.0f);
	Result.m_Data = Data;
	Result.m_StartTick = m_StartTick;
	Result.m_Type = m_Type;
	return Result;
}

CNetObj_DDNetProjectile CProjectile::NetInfo() const
{
	CNetObj_DDNetProjectile Result = {};

	int Flags = 0;
	if(m_Bouncing & 1)
	{
		Flags |= PROJECTILEFLAG_BOUNCE_HORIZONTAL;
	}
	if(m_Bouncing & 2)
	{
		Flags |= PROJECTILEFLAG_BOUNCE_VERTICAL;
	}
	if(m_Explosive)
	{
		Flags |= PROJECTILEFLAG_EXPLOSIVE;
	}
	if(m_FreezeTicks > 0)
	{
		Flags |= PROJECTILEFLAG_FREEZE;
	}

	if(m_Owner < 0)
	{
		Result.m_VelX = round_to_int(m_Direction.x * 1e6f);
		Result.m_VelY = round_to_int(m_Direction.y * 1e6f);
	}
	else
	{
		Result.m_VelX = round_to_int(m_InitDir.x);
		Result.m_VelY = round_to_int(m_InitDir.y);
		Flags |= PROJECTILEFLAG_NORMALIZE_VEL;
	}

	Result.m_X = round_to_int(m_Pos.x * 100.0f);
	Result.m_Y = round_to_int(m_Pos.y * 100.0f);
	Result.m_Type = m_Type;
	Result.m_StartTick = m_StartTick;
	Result.m_Owner = m_Owner;
	Result.m_SwitchNumber = m_Number;
	Result.m_TuneZone = m_TuneZone;
	Result.m_Flags = Flags;
	return Result;
}

void CProjectile::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	// <FoxNet
	vec2 SnapPos = GetPos(Ct);

	if(NetworkClipped(SnappingClient, SnapPos) || !GetId().has_value())
		return;
	// FoxNet>
	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient); // FoxNet
	if(SnappingClientVersion < VERSION_DDNET_ENTITY_NETOBJS)
	{
		CCharacter *pSnapChar = GameServer()->GetPlayerChar(SnappingClient);
		int Tick = (Server()->Tick() % Server()->TickSpeed()) % ((m_Explosive) ? 6 : 20);
		if(pSnapChar && pSnapChar->IsAlive() && (m_Layer == LAYER_SWITCH && m_Number > 0 && !Switchers()[m_Number].m_aStatus[pSnapChar->Team()] && (!Tick)))
			return;
	}

	CCharacter *pOwnerChar = nullptr;
	CClientMask TeamMask = CClientMask().set();

	if(m_Owner >= 0)
		pOwnerChar = GameServer()->GetPlayerChar(m_Owner);

	if(pOwnerChar && pOwnerChar->IsAlive())
		TeamMask = pOwnerChar->TeamMask();

	if(SnappingClient != SERVER_DEMO_CLIENT && m_Owner != -1 && !TeamMask.test(SnappingClient))
		return;

	// <FoxNet
	CPlayer *pSnapPl = SnappingClient >= 0 ? GameServer()->m_apPlayers[SnappingClient] : nullptr;
	if(pOwnerChar && pSnapPl && (pSnapPl->Acc()->m_Configs.m_Cosmetics.m_ShowGuns || SnappingClient == m_Owner) && m_Type == WEAPON_GUN)
	{
		// PrevSnapPos should be a bit behind SnapPos to make the laser look continuous
		float Pt = (Server()->Tick() - m_StartTick - 1.5f) / (float)Server()->TickSpeed();
		vec2 PrevSnapPos = GetPos(Pt);

		const bool Mixed = m_GunType == EGunType::Mixed;
		if(m_GunType == EGunType::Laser)
		{
			std::array<int, 2> LaserIds = {m_ExtraId.value(), GetId().value()};
			if(LaserIds[0] > LaserIds[1])
				std::swap(LaserIds[0], LaserIds[1]);

			if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
			{
				GameServer()->SnapLaserObject(CSnapContext(SnappingClientVersion, SixUp, SnappingClient),
					LaserIds.at(0), PrevSnapPos, SnapPos, Server()->Tick(), m_Owner, LASERTYPE_DOOR, -1, -1, LASERFLAG_NO_PREDICT);

				GameServer()->SnapLaserObject(CSnapContext(SnappingClientVersion, SixUp, SnappingClient),
					LaserIds.at(1), SnapPos, SnapPos, Server()->Tick(), m_Owner, LASERTYPE_DOOR, -1, -1, LASERFLAG_NO_PREDICT);
			}
			else
			{
				CNetObj_CosmeticLaser Laser = {};
				Laser.m_FromX = (int)SnapPos.x;
				Laser.m_FromY = (int)SnapPos.y;
				Laser.m_ToX = (int)PrevSnapPos.x;
				Laser.m_ToY = (int)PrevSnapPos.y;
				Laser.m_TickOffset = 0;
				Laser.m_Type = LASERTYPE_DOOR;
				Laser.m_Owner = m_Owner;
				Laser.m_Alpha = -1;
				Laser.m_Flags = COSMETIC_LASER_FLAG_FROM_HEAD | COSMETIC_LASER_FLAG_TO_HEAD;
				Server()->SnapNewItem(GetId().value(), Laser);
			}

			return;
		}
		if(m_GunType == EGunType::Snowflake)
		{
			if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
			{
				GameServer()->SnapLaserObject(CSnapContext(SnappingClientVersion, SixUp, SnappingClient),
					GetId().value(), SnapPos, SnapPos, Server()->Tick(), m_Owner, LASERTYPE_FREEZE, -1, -1, LASERFLAG_NO_PREDICT);
			}
			else
			{
				CNetObj_CosmeticLaser Laser = {};
				Laser.m_FromX = (int)SnapPos.x;
				Laser.m_FromY = (int)SnapPos.y;
				Laser.m_ToX = (int)SnapPos.x;
				Laser.m_ToY = (int)SnapPos.y;
				Laser.m_TickOffset = 0;
				Laser.m_Type = LASERTYPE_FREEZE;
				Laser.m_Owner = m_Owner;
				Laser.m_Alpha = -1;
				Laser.m_Flags = COSMETIC_LASER_FLAG_FROM_HEAD;
				Server()->SnapNewItem(GetId().value(), Laser);
			}

			return;
		}
		if(m_GunType == EGunType::Heart || Mixed)
		{
			int Type = POWERUP_HEALTH;
			if(Mixed)
				Type = m_MixedShield;

			if(SnappingClient == SERVER_DEMO_CLIENT || !pSnapPl->m_SupportsCosmeticSnaps)
			{
				GameServer()->SnapPickup(CSnapContext(SnappingClientVersion, Server()->IsSixup(SnappingClient), SnappingClient),
					GetId().value(), SnapPos, Type, 0, -1, PICKUPFLAG_NO_PREDICT);
			}
			else
			{
				CNetObj_CosmeticPickup Pickup = {};

				Pickup.m_X = (int)SnapPos.x;
				Pickup.m_Y = (int)SnapPos.y;
				Pickup.m_Type = Type;
				Pickup.m_Subtype = 0;
				Pickup.m_Owner = m_Owner;
				Pickup.m_Alpha = -1;
				Pickup.m_Rotation = 0;
				Pickup.m_Flags = 0;
				Server()->SnapNewItem(GetId().value(), Pickup);
			}
			return;
		}
	}

	// A wall pass-through bullet (Snowflake / PhaseGun) keeps living, but its path no
	// longer matches a plain bullet's once it has passed its first wall. Viewers with
	// the gun cosmetic off would otherwise see the ghost bullet jump around / fly
	// through walls, so hide it from them from that first wall hit onward.
	if(m_HasHitOnce && pSnapPl && !pSnapPl->Acc()->m_Configs.m_Cosmetics.m_ShowGuns && SnappingClient != m_Owner)
		return;
	// FoxNet>
	if(SnappingClientVersion >= VERSION_DDNET_ENTITY_NETOBJS)
	{
		Server()->SnapNewItem(GetId().value(), NetInfo());
	}
	else if(SnappingClientVersion >= VERSION_DDNET_ANTIPING_PROJECTILE && NetIsInfoLegacyCompatible())
	{
		if(SnappingClientVersion >= VERSION_DDNET_MSG_LEGACY)
		{
			Server()->SnapNewItem(GetId().value(), NetInfoLegacy());
		}
		else
		{
			CNetObj_DDRaceProjectile DDRaceProjectile = NetInfoLegacy();
			CNetObj_Projectile Projectile = {};
			static_assert(sizeof(DDRaceProjectile) == sizeof(Projectile));
			mem_copy(&Projectile, &DDRaceProjectile, sizeof(Projectile));
			Server()->SnapNewItem(GetId().value(), Projectile);
		}
	}
	else
	{
		Server()->SnapNewItem(GetId().value(), NetInfoVanilla());
	}
}

void CProjectile::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}

// DDRace

bool CProjectile::CanCollide(int ClientId)
{
	if(m_DDRaceTeam != GameServer()->GetDDRaceTeam(ClientId))
		return false;
	if(m_IsSolo)
		return m_Owner == ClientId;
	return true;
}

void CProjectile::SetBouncing(int Value)
{
	m_Bouncing = Value;
}
