// Made by qxdFox
#include "dot_trail.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

CDotTrail::CDotTrail(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_DOT_TRAIL, Pos)
{
	m_Pos = Pos;

	GameWorld()->InsertEntity(this);
}

void CDotTrail::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("dottrail", "Reset");
	m_MarkedForDestroy = true;
}

void CDotTrail::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(!GetPlayer() || GetPlayer()->Cosmetics()->m_Trail != TRAILTYPE_DOT)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;
	m_PrevPos = m_Pos;
	m_Pos = GetCharacter()->GetPos();
}

void CDotTrail::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowTrails)
		return;

	if(m_PrevPos == m_Pos)
		return;

	if(GetId().has_value())
		SnapCosmeticProjectile(SnappingClient, GetId().value(), m_Owner, vec2(0, 0), vec2(0, 0), 0, WEAPON_HAMMER, -1, COSMETIC_FLAG_ANCHORED);
}
