// Made by qxdFox
#include "gun_projectile.h"

#include "foxnet_entity.h"

#include <array>
#include <cmath>
#include <utility>

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

CGunProjectile::CGunProjectile(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir, vec2 MouseTarget, int Span) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_GUN_PROJECTILE, Pos)
{
	m_Pos = Pos;
	m_Direction = Dir;
	m_StartTick = Server()->Tick();

	m_SpawnPos = Pos;
	m_SpawnDir = Dir;
	m_MouseTarget = MouseTarget;
	m_SpawnTick = Server()->Tick();

	m_VanillaPrevPos = Pos;
	m_VanillaDead = false;

	CCharacter *pOwnerChar = GetCharacter();
	m_TuneZone = pOwnerChar ? pOwnerChar->GetOverriddenTuneZone() : Collision()->IsTune(Collision()->GetMapIndex(m_Pos));

	CCosmetics *pCosmetics = GetPlayer() ? GetPlayer()->Cosmetics() : nullptr;
	m_GunType = pCosmetics ? pCosmetics->m_GunType : EGunType::None;
	m_PhaseGun = pCosmetics && pCosmetics->m_PhaseGun;
	m_ConfettiGun = pCosmetics && pCosmetics->m_ConfettiGun;
	m_EmoteGun = pCosmetics ? pCosmetics->m_EmoticonGun : 0;
	m_DamageIndEffect = pCosmetics ? pCosmetics->m_DamageIndType : 0;

	// The vanilla bullet dies at the normal gun range; special gun types (which visibly
	// travel differently) live a little longer, matching the previous behaviour.
	m_VanillaLifeTicks = Span;
	m_LifeSpan = m_GunType != EGunType::None ? (int)(Span * 1.25f) : Span;

	m_MaskGun = m_MaskInd = CClientMask().set();
	m_MaskGunOpp = m_MaskIndOpp = CClientMask();
	if(pOwnerChar)
	{
		m_MaskGun = pOwnerChar->CosmeticMask(EItemType::Gun);
		m_MaskGunOpp = pOwnerChar->OppositeCosmeticMask(EItemType::Gun);
		m_MaskInd = pOwnerChar->CosmeticMask(EItemType::Indicator);
		m_MaskIndOpp = pOwnerChar->OppositeCosmeticMask(EItemType::Indicator);

		m_MixedShield = pOwnerChar->m_MixedShield;
		pOwnerChar->m_MixedShield = !pOwnerChar->m_MixedShield;
	}
	else
		m_MixedShield = false;

	if(m_GunType == EGunType::Laser)
		m_ExtraId = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

void CGunProjectile::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(m_ExtraId.has_value())
	{
		Server()->SnapFreeId(m_ExtraId.value());
		m_ExtraId.reset();
	}

	m_MarkedForDestroy = true;
}

CGunProjectile::EWallBehavior CGunProjectile::WallBehavior() const
{
	if(m_GunType == EGunType::Snowflake)
		return WALL_BOUNCE;
	if(m_PhaseGun)
		return WALL_PHASE;
	return WALL_DIE;
}

float CGunProjectile::GunSpeed()
{
	return GetTuning(m_TuneZone)->m_GunSpeed;
}

float CGunProjectile::GunCurvature()
{
	return GetTuning(m_TuneZone)->m_GunCurvature;
}

vec2 CGunProjectile::RealPos(float Time)
{
	return CalcPos(m_Pos, m_Direction, GunCurvature(), GunSpeed(), Time);
}

vec2 CGunProjectile::VanillaPos(int Tick)
{
	float Time = (Tick - m_SpawnTick) / (float)Server()->TickSpeed();
	return CalcPos(m_SpawnPos, m_SpawnDir, GunCurvature(), GunSpeed(), Time);
}

void CGunProjectile::TickVanillaPhantom()
{
	if(m_VanillaDead)
		return;

	// A plain bullet would die at its first wall or when its (shorter) lifetime ends.
	// Once that happens we stop showing the vanilla bullet to cosmetics-off viewers,
	// so their client doesn't keep extrapolating it straight through walls.
	if(Server()->Tick() - m_SpawnTick >= m_VanillaLifeTicks)
	{
		m_VanillaDead = true;
		return;
	}

	vec2 CurPos = VanillaPos(Server()->Tick());
	vec2 ColPos, BeforePos;
	if(Collision()->IntersectLine(m_VanillaPrevPos, CurPos, &ColPos, &BeforePos))
	{
		m_VanillaDead = true;

		// Cosmetics-off viewers see a plain bullet die right here, so give them the same
		// hit effect at this spot (cosmetic viewers get theirs where the real bullet dies).
		vec2 Direction = normalize(CurPos - m_VanillaPrevPos);
		if(Direction == vec2(0, 0))
			Direction = m_SpawnDir;
		EmitHitEffect(BeforePos, ColPos, Direction, m_MaskGunOpp);
		return;
	}

	m_VanillaPrevPos = CurPos;
}

void CGunProjectile::EmitHitEffect(vec2 NewPos, vec2 CurPos, vec2 Direction, CClientMask Audience)
{
	// Within the audience, the fancy gun-hit effect (or confetti) goes to viewers with
	// gun-hit effects on, plain damage indicator stars to those with them off.
	CClientMask FancyMask = Audience & m_MaskInd;
	CClientMask PlainMask = Audience & m_MaskIndOpp;

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

	GameServer()->CreateDamageInd(CurPos, -std::atan2(Direction.x, Direction.y), 10, PlainMask);
}

void CGunProjectile::Tick()
{
	if(m_MarkedForDestroy)
		return;

	CCharacter *pOwnerChar = GetCharacter();
	CPlayer *pOwner = GetPlayer();

	// Keep the vanilla bullet (for cosmetics-off viewers) simulated independently of the
	// real one, so it always dies exactly where a plain bullet would.
	TickVanillaPhantom();

	if(!pOwnerChar || !pOwnerChar->IsAlive())
	{
		Reset();
		return;
	}

	float Pt = (Server()->Tick() - m_StartTick - 1) / (float)Server()->TickSpeed();
	float Ct = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	vec2 PrevPos = RealPos(Pt);
	vec2 CurPos = RealPos(Ct);
	vec2 ColPos;
	vec2 NewPos;
	int Collide = Collision()->IntersectLine(PrevPos, CurPos, &ColPos, &NewPos);

	CCharacter *pTargetChr = nullptr;
	if(!pOwnerChar->GrenadeHitDisabled())
		pTargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, ColPos, 6.0f, ColPos, pOwnerChar, m_Owner);

	if((pTargetChr && pTargetChr->Core()->m_Passive) || pOwnerChar->Core()->m_Passive)
		pTargetChr = nullptr;
	if(pTargetChr && !pTargetChr->Core()->m_Hittable)
		pTargetChr = nullptr;

	bool GLClipped = GameLayerClipped(CurPos) && (!pOwner || !pOwner->m_IgnoreGamelayer);

	if(m_LifeSpan > -1)
		m_LifeSpan--;

	bool IsWeaponCollide = pTargetChr && pTargetChr->IsAlive() && !pTargetChr->CanCollide(m_Owner);

	if((pTargetChr || Collide || GLClipped) && !IsWeaponCollide)
	{
		if(pTargetChr)
			pTargetChr->TakeDamage(vec2(0, 0), 0, m_Owner, WEAPON_GUN);

		// Telegun
		if(!GLClipped && pOwnerChar->HasTelegunGun())
		{
			int MapIdx = Collision()->GetPureMapIndex(pTargetChr ? pTargetChr->m_Pos : ColPos);
			int TileFIndex = Collision()->GetFrontTileIndex(MapIdx);
			bool IsSwitchTeleGun = Collision()->GetSwitchType(MapIdx) == TILE_ALLOW_TELE_GUN;
			bool IsBlueSwitchTeleGun = Collision()->GetSwitchType(MapIdx) == TILE_ALLOW_BLUE_TELE_GUN;

			if(IsSwitchTeleGun || IsBlueSwitchTeleGun)
			{
				int Delay = Collision()->GetSwitchDelay(MapIdx);
				// Delay specifies which weapon the tile works for (0 = all); this is a gun.
				if(Delay == 2 || Delay == 3)
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

		EWallBehavior Behavior = WallBehavior();
		bool WallOnly = Collide && !pTargetChr;

		if(WallOnly && Behavior == WALL_BOUNCE)
		{
			// Reflect off the wall and keep living. Cap the step MovePoint takes so it
			// can't tunnel through a tile when the bullet starts inside a solid (e.g.
			// fired straight down while standing on a tile, since the muzzle spawns a few
			// pixels into the floor).
			vec2 Vel = CurPos - PrevPos;
			const float MaxStep = 16.0f;
			if(length(Vel) > MaxStep)
				Vel = normalize(Vel) * MaxStep;

			m_StartTick = Server()->Tick();
			m_Pos = NewPos;
			Collision()->MovePoint(&m_Pos, &Vel, 1.0f, nullptr);
			if(length_squared(Vel) > 0.0f)
				m_Direction = normalize(Vel);
			return;
		}

		if(WallOnly && Behavior == WALL_PHASE)
		{
			// Pass straight through the wall and keep flying.
			return;
		}

		// Otherwise the real bullet dies here (hit a player, a wall it doesn't pass
		// through, or left the game layer).
		vec2 Direction = normalize(NewPos - PrevPos);
		if(Direction == vec2(0, 0))
			Direction = m_Direction;

		if(m_EmoteGun && pTargetChr)
		{
			const int TargetId = pTargetChr->GetPlayer()->GetCid();
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(m_MaskGun.test(i))
					GameServer()->SendEmote(TargetId, m_EmoteGun - 1, i);
		}

		// Cosmetic viewers see the bullet die here; cosmetics-off viewers get their effect
		// from the vanilla phantom instead (at the wall where a plain bullet would die).
		EmitHitEffect(NewPos, CurPos, Direction, m_MaskGun);
		Reset();
		return;
	}

	if(m_LifeSpan == -1)
	{
		Reset();
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

void CGunProjectile::SnapCosmeticBullet(int SnappingClient)
{
	const int Ct = Server()->Tick() - m_StartTick;
	vec2 SnapPos = RealPos(Ct / (float)Server()->TickSpeed());

	if(NetworkClipped(SnappingClient, SnapPos))
		return;

	const int Owner = m_Owner;

	if(m_GunType == EGunType::Laser)
	{
		// PrevSnapPos trails SnapPos slightly so the laser looks continuous.
		float Pt = (Server()->Tick() - m_StartTick - 1.5f) / (float)Server()->TickSpeed();
		vec2 PrevSnapPos = RealPos(Pt);

		bool Supports = SnappingClient != SERVER_DEMO_CLIENT && GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps;
		if(Supports)
		{
			// Modern clients draw the whole thing from one cosmetic laser.
			SnapCosmeticLaserPos(SnappingClient, GetId().value(), Owner, SnapPos, PrevSnapPos, 0, LASERTYPE_DOOR, -1, COSMETIC_LASER_FLAG_FROM_HEAD | COSMETIC_LASER_FLAG_TO_HEAD);
		}
		else
		{
			// Older clients get a trailing segment plus a dot at the head, one per id.
			std::array<int, 2> LaserIds = {m_ExtraId.value(), GetId().value()};
			if(LaserIds[0] > LaserIds[1])
				std::swap(LaserIds[0], LaserIds[1]);

			SnapCosmeticLaserPos(SnappingClient, LaserIds[0], Owner, PrevSnapPos, SnapPos, 0, LASERTYPE_DOOR, -1, 0);
			SnapCosmeticLaserPos(SnappingClient, LaserIds[1], Owner, SnapPos, SnapPos, 0, LASERTYPE_DOOR, -1, 0);
		}
		return;
	}

	if(m_GunType == EGunType::Snowflake)
	{
		SnapCosmeticLaserPos(SnappingClient, GetId().value(), Owner, SnapPos, SnapPos, 0, LASERTYPE_FREEZE, -1, COSMETIC_LASER_FLAG_FROM_HEAD);
		return;
	}

	if(m_GunType == EGunType::Heart || m_GunType == EGunType::Mixed)
	{
		int Type = POWERUP_HEALTH;
		if(m_GunType == EGunType::Mixed)
			Type = m_MixedShield;

		SnapCosmeticPickupPos(SnappingClient, GetId().value(), PICKUPFLAG_NO_PREDICT, Owner, SnapPos, Type, 0, 0, -1, 0);
		return;
	}

	// No special bullet visual (plain / confetti / emote / phase): draw a normal bullet.
	// It uses the launch parameters so it looks vanilla; for PhaseGun it simply keeps
	// being snapped past the wall since the entity stays alive (unlike the vanilla
	// bullet, this is not gated on m_VanillaDead).
	SnapProjectileNetObj(SnappingClient);
}

void CGunProjectile::SnapVanillaBullet(int SnappingClient)
{
	// The vanilla bullet dies where a plain bullet would; stop drawing it after that.
	if(m_VanillaDead)
		return;

	SnapProjectileNetObj(SnappingClient);
}

void CGunProjectile::SnapProjectileNetObj(int SnappingClient)
{
	vec2 SnapPos = VanillaPos(Server()->Tick());
	if(NetworkClipped(SnappingClient, SnapPos))
		return;

	const int Ver = GameServer()->GetClientVersion(SnappingClient);
	const int Id = GetId().value();

	const int MaxPos = 0x7fffffff / 100;
	bool LegacyCompatible = !(absolute((int)m_SpawnPos.y) + 1 >= MaxPos || absolute((int)m_SpawnPos.x) + 1 >= MaxPos);

	if(Ver >= VERSION_DDNET_ENTITY_NETOBJS)
	{
		CNetObj_DDNetProjectile Proj = {};
		Proj.m_X = round_to_int(m_SpawnPos.x * 100.0f);
		Proj.m_Y = round_to_int(m_SpawnPos.y * 100.0f);
		Proj.m_VelX = round_to_int(m_MouseTarget.x);
		Proj.m_VelY = round_to_int(m_MouseTarget.y);
		Proj.m_Type = WEAPON_GUN;
		Proj.m_StartTick = m_SpawnTick;
		Proj.m_Owner = m_Owner;
		Proj.m_SwitchNumber = 0;
		Proj.m_TuneZone = m_TuneZone;
		Proj.m_Flags = PROJECTILEFLAG_NORMALIZE_VEL;
		Server()->SnapNewItem(Id, Proj);
	}
	else if(Ver >= VERSION_DDNET_ANTIPING_PROJECTILE && LegacyCompatible)
	{
		float Angle = -std::atan2(m_SpawnDir.x, m_SpawnDir.y);
		int Data = (absolute(m_Owner) & 255) << 0;
		Data |= LEGACYPROJECTILEFLAG_IS_DDNET;

		CNetObj_DDRaceProjectile Proj = {};
		Proj.m_X = (int)(m_SpawnPos.x * 100.0f);
		Proj.m_Y = (int)(m_SpawnPos.y * 100.0f);
		Proj.m_Angle = (int)(Angle * 1000000.0f);
		Proj.m_Data = Data;
		Proj.m_StartTick = m_SpawnTick;
		Proj.m_Type = WEAPON_GUN;

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
		Proj.m_X = (int)m_SpawnPos.x;
		Proj.m_Y = (int)m_SpawnPos.y;
		Proj.m_VelX = (int)(m_SpawnDir.x * 100.0f);
		Proj.m_VelY = (int)(m_SpawnDir.y * 100.0f);
		Proj.m_StartTick = m_SpawnTick;
		Proj.m_Type = WEAPON_GUN;
		Server()->SnapNewItem(Id, Proj);
	}
}

void CGunProjectile::Snap(int SnappingClient)
{
	if(!GetId().has_value())
		return;
	if(!CanSnapEntity(SnappingClient))
		return;

	// The owner always sees their own gun cosmetic; others see it only with ShowGuns on.
	// Checked live so toggling the setting takes effect immediately.
	bool SeesCosmetic = SnappingClient == SERVER_DEMO_CLIENT || SnappingClient == m_Owner;
	if(!SeesCosmetic)
	{
		CPlayer *pSnapPl = GameServer()->m_apPlayers[SnappingClient];
		SeesCosmetic = pSnapPl && pSnapPl->Acc()->m_Configs.m_Cosmetics.m_ShowGuns;
	}

	if(SeesCosmetic)
		SnapCosmeticBullet(SnappingClient);
	else
		SnapVanillaBullet(SnappingClient);
}

void CGunProjectile::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : (m_Owner == Client2 ? Client1 : m_Owner);
}
