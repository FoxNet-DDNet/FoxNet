#include "epic_circle.h"

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

#include <algorithm>
#include <cmath>

CEpicCircle::CEpicCircle(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_EPIC_CIRCLE, Pos)
{
	m_Pos = Pos;

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		m_aIds[i] = Server()->SnapNewId();
	}
	std::sort(std::begin(m_aIds), std::end(m_aIds), [](const std::optional<int> &a, const std::optional<int> &b) { return a.has_value() && (!b.has_value() || a.value() < b.value()); });
	GameWorld()->InsertEntity(this);
}

CEpicCircle::~CEpicCircle()
{
	for(const auto &Id : m_aIds)
		if(Id.has_value())
			Server()->SnapFreeId(Id.value());
}

void CEpicCircle::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("epiccircle", "Reset");

	m_MarkedForDestroy = true;
}

void CEpicCircle::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_EpicCircle)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	m_Pos = GetCharacter()->GetPos();

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		float rad = 16.0f * powf(sinf(Server()->Tick() / 30.0f), 3) * 1 + 75;
		float TurnFac = 0.025f;
		m_RotatePos[i].x = cosf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * rad;
		m_RotatePos[i].y = sinf(2 * pi * (i / (float)MAX_PARTICLES) + Server()->Tick() * TurnFac) * rad;
	}
}

void CEpicCircle::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;

	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		if(m_aIds[i].has_value())
			SnapCosmeticProjectile(SnappingClient, m_aIds[i].value(), m_Owner, m_RotatePos[i], vec2(0, 0), 0, WEAPON_HAMMER);
	}
}
