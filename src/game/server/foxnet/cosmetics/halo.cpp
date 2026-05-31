// Made by qxdFox
#include "halo.h"

#include <base/log.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>
#include <iterator>

CHalo::CHalo(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_HALO, Pos)
{
	m_Pos = Pos;

	m_StartTick = Server()->Tick();

	for(size_t Idx = 0; Idx < std::size(m_aSnap); Idx++)
		m_aSnap[Idx].m_Id = Server()->SnapNewId();

	// Sort based on m_Id
	std::sort(std::begin(m_aSnap), std::end(m_aSnap), [](const CSnapData &a, const CSnapData &b) { return a.m_Id.value() < b.m_Id.value(); });

	GameWorld()->InsertEntity(this);
}

void CHalo::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("halo", "Reset");
	for(size_t Idx = 0; Idx < std::size(m_aSnap); Idx++)
	{
		if(m_aSnap[Idx].m_Id.has_value())
			Server()->SnapFreeId(m_aSnap[Idx].m_Id.value());
	}
	m_MarkedForDestroy = true;
}

void CHalo::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_Halo)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	m_Pos = GetCharacter()->GetPos();
	SetData();
}

void CHalo::SetData()
{
	int Tick = Server()->Tick() - m_StartTick;

	const float L = 40.0f;

	const vec2 Center(0.0f, -64.0f);

	for(size_t Idx = 0; Idx < std::size(m_aSnap); Idx++)
	{
		m_aSnap[Idx].m_Pos = Center;

		m_aSnap[Idx].m_Pos.x = sinf(Tick * 0.025f + Idx) * L;

		const float OffsetY = sinf(Tick * 0.1f + Idx) * 8.0f;
		m_aSnap[Idx].m_Pos.y += OffsetY;

		const float Tilt = sinf(Tick * 0.03f) * 0.15f;
		Rotate(Center + vec2(0, OffsetY), &m_aSnap[Idx].m_Pos, Tilt);
	}
}

void CHalo::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;

	for(size_t Idx = 0; Idx < std::size(m_aSnap); Idx++)
	{
		if(!m_aSnap[Idx].m_Id.has_value())
			continue;
		SnapCosmeticLaser(SnappingClient, m_aSnap[Idx].m_Id.value(), m_Owner, m_aSnap[Idx].m_Pos, m_aSnap[Idx].m_Pos, 0, LASERTYPE_GUN, -1, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD);
	}
}
