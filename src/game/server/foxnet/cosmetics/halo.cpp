// Made by qxdFox
#include "halo.h"

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
#include <game/server/player.h>

CHalo::CHalo(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_HALO, Pos)
{
	m_Pos = Pos;

	m_StartTick = Server()->Tick();

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
		m_aSnap[Idx].m_Id = Server()->SnapNewId();

	std::sort(m_aSnap, m_aSnap + NUM_IDS, [](const CSnapData &a, const CSnapData &b) { return a.m_Id < b.m_Id; });

	GameWorld()->InsertEntity(this);
}

void CHalo::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("halo", "Reset");
	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CHalo::Tick()
{
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

	const vec2 Center(0.0f, -56.0f);

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
	{
		m_aSnap[Idx].m_Pos = Center;

		m_aSnap[Idx].m_Pos.x = sinf(Tick * 0.025f + Idx) * L;

		const float OffsetY = sinf(Tick * 0.1f + Idx) * 8.0f;
		m_aSnap[Idx].m_Pos.y += OffsetY;

		const float Tilt = sinf(Tick * 0.03f) * 0.15f;
		Collision()->Rotate(Center + vec2(0, OffsetY), &m_aSnap[Idx].m_Pos, Tilt);
	}
}

void CHalo::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowTrails)
		return;

	CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(GetId());
	if(!pProj)
		return;

	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, false);

	for(int Idx = 0; Idx < NUM_IDS; ++Idx)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);

		vec2 From = m_aSnap[Idx].m_Pos + Pos;
		vec2 To = m_aSnap[Idx].m_Pos + Pos;

		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), m_aSnap[Idx].m_Id, To, From, Server()->Tick(), m_Owner, 0, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
