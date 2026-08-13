// made by fokkonaut
#include "custom_projectile.h"

#include "foxnet_entity.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>
#include <generated/server_data.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

CCustomProjectile::CCustomProjectile(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir,
	bool Explosive, bool Freeze, bool Unfreeze, int Type, float Lifetime, float Accel, float Speed) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_CUSTOM_PROJECTILE, Pos)
{
	m_Pos = Pos;
	m_Core = normalize(Dir) * Speed;
	m_Freeze = Freeze;
	m_Explosive = Explosive;
	m_Unfreeze = Unfreeze;
	m_Direction = Dir;
	m_EvalTick = Server()->Tick();
	m_LifeTime = Server()->TickSpeed() * Lifetime;
	m_Type = Type;
	m_Accel = Accel;

	m_PrevPos = m_Pos;

	m_TuneZone = Collision()->IsTune(Collision()->GetMapIndex(m_Pos));

	GameWorld()->InsertEntity(this);
}

void CCustomProjectile::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("custom-projectile", "Reset");

	m_MarkedForDestroy = true;
}

void CCustomProjectile::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(m_Owner >= 0 && !GetCharacter() && Config()->m_SvDestroyBulletsOnDeath)
	{
		Reset();
		return;
	}

	m_LifeTime--;
	if(m_LifeTime <= 0)
	{
		Reset();
		return;
	}

	Move();
	HitCharacter();

	if(GetCollision()->IsSolid(m_Pos.x, m_Pos.y))
	{
		if(m_Explosive)
		{
			GameServer()->CreateExplosion(m_Pos, m_Owner, m_Type, m_Owner == -1, GetCharacter() ? GetCharacter()->Team() : -1, MultiMapIdx(), TeamMask());
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE, TeamMask());
		}

		if(m_CollisionState == NOT_COLLIDED)
			m_CollisionState = COLLIDED_ONCE;
	}
	else if(m_CollisionState == COLLIDED_ONCE)
		m_CollisionState = COLLIDED_TWICE;

	// weapon teleport
	// int x = Collision()->GetIndex(m_PrevPos, m_Pos);
	// int z;
	// if(Config()->m_SvOldTeleportWeapons)
	//	z = Collision()->IsTeleport(x);
	// else
	//	z = Collision()->IsTeleportWeapon(x);
	// if(z && ((CGameControllerDDRace *)GameServer()->m_pController)->m_TeleOuts[z - 1].size())
	//{
	//	int Num = ((CGameControllerDDRace *)GameServer()->m_pController)->m_TeleOuts[z - 1].size();
	//	m_Pos = ((CGameControllerDDRace *)GameServer()->m_pController)->m_TeleOuts[z - 1][(!Num) ? Num : rand() % Num];
	//	m_EvalTick = Server()->Tick();
	// }

	m_PrevPos = m_Pos;
}

void CCustomProjectile::Move()
{
	m_Pos += m_Core;
	m_Core *= m_Accel;
}

void CCustomProjectile::HitCharacter()
{
	vec2 NewPos = m_Pos + m_Core;
	CCharacter *pHit = GameWorld()->IntersectCharacter(m_PrevPos, NewPos, 6.0f, NewPos, GetCharacter(), m_Owner);
	if(!pHit)
		return;

	if(m_Freeze)
		pHit->Freeze();
	else if(m_Unfreeze)
		pHit->Unfreeze();

	if(m_Explosive)
	{
		GameServer()->CreateExplosion(m_Pos, m_Owner, m_Type, m_Owner == -1, pHit->Team(), MultiMapIdx(), TeamMask());
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE, TeamMask());
	}
	// else
	// pHit->TakeDamage(vec2(0, 0), g_pData->m_Weapons.m_aId[GameServer()->GetWeaponType(m_Type)].m_Damage, m_Owner, m_Type);

	if(GetCharacter()->GetActiveWeapon() == WEAPON_HEARTGUN)
	{
		pHit->SetEmote(EMOTE_HAPPY, Server()->Tick() + 2 * Server()->TickSpeed());
		GameServer()->SendEmoticon(pHit->GetPlayer()->GetCid(), EMOTICON_HEARTS, -1);
	}

	Reset();
}

void CCustomProjectile::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(!CanSnapEntity(SnappingClient))
		return;

	if(!GetId().has_value())
		return;

	CCharacter *pSnapChar = GameServer()->GetPlayerChar(SnappingClient);
	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	if(SnappingClientVersion < VERSION_DDNET_ENTITY_NETOBJS)
	{
		int Tick = (Server()->Tick() % Server()->TickSpeed()) % ((m_Explosive) ? 6 : 20);
		if(pSnapChar && pSnapChar->IsAlive() && (m_Layer == LAYER_SWITCH && m_Number > 0 && !Switchers()[m_Number].m_aStatus[pSnapChar->Team()] && (!Tick)))
			return;
	}

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);
	GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), GetId().value(), m_Pos, m_Type, 0, -1, PICKUPFLAG_NO_PREDICT);
}
