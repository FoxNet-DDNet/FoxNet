#include "flyingpoint.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>

#include <algorithm>

// Made by qxdFox
CFlyingPoint::CFlyingPoint(CGameWorld *pGameWorld, int Owner, vec2 Pos, int To, vec2 InitialVel, vec2 ToPos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_FLYINGPOINT, Pos)
{
	m_Pos = Pos;
	m_PrevPos = Pos;
	m_InitialVel = InitialVel;
	m_To = To;
	m_ToPos = ToPos;
	m_InitialAmount = 1.0f;

	GameWorld()->InsertEntity(this);
}

void CFlyingPoint::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("flyingpoint", "Reset");

	CCharacter *pToChar = GameServer()->GetPlayerChar(m_To);
	if(pToChar)
	{
		pToChar->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		GameServer()->CreateSoundGlobal(SOUND_PICKUP_HEALTH, m_To);
	}

	m_MarkedForDestroy = true;
}

void CFlyingPoint::Tick()
{
	if(m_MarkedForDestroy)
		return;

	vec2 ToPos = m_ToPos;

	if(m_To != -1)
	{
		CCharacter *pChr = GameServer()->GetPlayerChar(m_To);
		if(!pChr)
		{
			Reset();
			return;
		}

		ToPos = pChr->GetPos();
	}

	const float Dist = distance(m_Pos, ToPos);
	if(Dist < 24.0f)
	{
		Reset();
		return;
	}

	const vec2 Dir = normalize(ToPos - m_Pos);
	m_Pos += Dir * std::clamp(Dist, 1.0f, 24.0f) * (1.0f - m_InitialAmount) + m_InitialVel * m_InitialAmount;

	m_InitialAmount *= 0.98f;
	m_PrevPos = m_Pos;
}

void CFlyingPoint::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(!CanSnapEntity(SnappingClient))
		return;

	CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(GetId());
	if(!pProj)
		return;

	pProj->m_X = round_to_int(m_Pos.x * 100.0f);
	pProj->m_Y = round_to_int(m_Pos.y * 100.0f);
	pProj->m_Type = WEAPON_HAMMER;
	pProj->m_StartTick = 0;
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
}