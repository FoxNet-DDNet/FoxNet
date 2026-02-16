// Made by qxdFox
#include "pickup_pet.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <cmath>

CPickupPet::CPickupPet(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_PICKUP, Pos)
{
	m_Pos = Pos;
	m_PetMode = 1;
	m_CurType = POWERUP_ARMOR;
	m_SwitchDelay = Server()->Tick() + Server()->TickSpeed();
	GameWorld()->InsertEntity(this);
}

void CPickupPet::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("pickuppet", "Reset");

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);

	if(!GetCharacter())
		return;

	if(GetCharacter()->Core()->m_FakeTuned)
	{
		GameServer()->ResetFakeTunes(GetPlayer()->GetCid(), GetCharacter()->GetOverriddenTuneZone());
	}
}

void CPickupPet::Tick()
{
	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_PickupPet)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	if(m_PetMode == PET_MODE_AFK)
		PlayerAfkMode();
	else if(m_PetMode == PET_MODE_FOLLOW)
		FollowMode();
	else
		StaticMode();
}

void CPickupPet::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;

	if(Server()->Tick() > m_SwitchDelay)
	{
		if(m_CurType == POWERUP_ARMOR)
			m_CurType = POWERUP_HEALTH;
		else
			m_CurType = POWERUP_ARMOR;

		m_SwitchDelay = static_cast<int64_t>(Server()->Tick()) + Server()->TickSpeed();
	}

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);

	if(g_Config.m_SvCorruptPickupPet)
		m_CurType = NUM_POWERUPS;

	GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), GetId(), m_Pos, m_CurType, 0, -1, PICKUPFLAG_NO_PREDICT);
}

void CPickupPet::PlayerAfkMode()
{
	// ToDo: add some afk animations to the pet
}

void CPickupPet::FollowMode()
{
	vec2 TargetPos = GetCharacter()->GetPos();
	vec2 Offset = vec2(0.0f, 0.0f);
	Offset.y = -72;

	m_aSpeed = 0.1f;
	bool LookingLeft = GetCharacter()->Core()->m_Angle > 402;

	if(abs(GetCharacter()->Core()->m_Vel.x) < 2.5f)
	{
		if(GetCharacter()->IsGrounded())
			Offset.y += 10.0f * sin(Server()->Tick() * 1.0f * pi / Server()->TickSpeed());
		Offset.x = 45;
	}
	else
	{
		Offset.x = 35;
	}

	bool FollowMouse = GetPlayer()->m_PlayerFlags & PLAYERFLAG_AIM && GetCharacter()->IsGrounded();

	if(FollowMouse)
	{
		Offset = vec2(0.0f, 0.0f);
		m_aSpeed = 0.08f;
		TargetPos = GetCharacter()->GetCursorPos();
	}

	bool ThreeBlocksUp = Collision()->CheckPoint(GetCharacter()->GetPos() + vec2(0, -3.0f * 32.0f));

	bool OneHalfBlocksUp = Collision()->CheckPoint(GetCharacter()->GetPos() + vec2(0, -1.5f * 32.0f));
	bool OneHalfBlocksDown = Collision()->CheckPoint(GetCharacter()->GetPos() + vec2(0, 1.5f * 32.0f));

	bool OneBlockUp = Collision()->CheckPoint(GetCharacter()->GetPos() + vec2(0, -32.0f));
	bool OneBlockDown = Collision()->CheckPoint(GetCharacter()->GetPos() + vec2(0, 32.0f));

	if(OneBlockUp)
	{
		TargetPos.y += 72.0f;
	}
	else if(OneHalfBlocksUp)
	{
		if(OneHalfBlocksDown)
			TargetPos.y += 100.0f;
		else
			TargetPos.y += 58.0f;
	}
	else if(ThreeBlocksUp)
	{
		TargetPos.y += 36.0f;
	}

	if(!LookingLeft)
		Offset.x = -Offset.x;

	m_aPos.x = TargetPos.x + Offset.x;
	m_aPos.y = TargetPos.y + Offset.y;

	for(int i = -20; i <= 0; i++)
	{
		float ExtraOffset = abs(i);

		if(TargetPos.x < m_aPos.x && Collision()->CheckPoint(m_aPos + vec2(abs(i) / 10.0f * 32.0f, 0.0f)))
		{
			Offset.x = ExtraOffset / 10.0f * 32.0f;
			if(i == 0 && OneBlockUp && !OneBlockDown)
				Offset.y += 48.0f;
		}
		else if(TargetPos.x > m_aPos.x && Collision()->CheckPoint(m_aPos + vec2(abs(i) / 10.0f * -32.0f, 0.0f)))
		{
			Offset.x = ExtraOffset / 10.0f * -32.0f;
			if(i == 0 && OneBlockUp && !OneBlockDown)
				Offset.y += 48.0f;
		}
	}

	TargetPos.x += Offset.x;
	TargetPos.y += Offset.y;

	vec2 NewPos = vec2(0.0f, 0.0f);
	NewPos.x = m_Pos.x + m_aSpeed * (TargetPos.x - m_Pos.x);
	NewPos.y = m_Pos.y + m_aSpeed * (TargetPos.y - m_Pos.y);

	// Check for collision with blocks
	bool CollidesLeft = NewPos.x < m_Pos.x && Collision()->CheckPoint(vec2(NewPos.x, m_Pos.y));
	bool CollidesRight = NewPos.x > m_Pos.x && Collision()->CheckPoint(vec2(NewPos.x, m_Pos.y));
	bool CollidesFloor = NewPos.y > m_Pos.y && Collision()->CheckPoint(vec2(m_Pos.x, NewPos.y));
	bool CollidesCeiling = NewPos.y < m_Pos.y && Collision()->CheckPoint(vec2(m_Pos.x, NewPos.y));

	if((!CollidesLeft && !CollidesRight && !CollidesFloor && !CollidesCeiling) || !FollowMouse)
	{
		m_Pos = NewPos;
	}
	else
	{
		if(!CollidesLeft && !CollidesRight)
			m_Pos.x = NewPos.x;

		if(!CollidesFloor && !CollidesCeiling)
			m_Pos.y = NewPos.y;

		if(CollidesFloor)
			m_Pos.y = floor(m_Pos.y);

		if(CollidesCeiling)
			m_Pos.y = ceil(m_Pos.y);

		if(CollidesLeft)
			m_Pos.x = ceil(m_Pos.x);

		if(CollidesRight)
			m_Pos.x = floor(m_Pos.x);
	}
}

void CPickupPet::StaticMode()
{
}