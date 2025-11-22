// Made by qxdFox
#include "dot_trail.h"

#include "game/server/entities/character.h"

#include <base/vmath.h>

#include <generated/protocol.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/server/foxnet/shop.h>
#include <base/math.h>

CDotTrail::CDotTrail(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_DOT_TRAIL, Pos)
{
	m_Pos = Pos;
	m_Owner = Owner;

	GameWorld()->InsertEntity(this);
}

void CDotTrail::Reset()
{
	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CDotTrail::Tick()
{
	CPlayer *pOwnerPl = GameServer()->m_apPlayers[m_Owner];
	if(!pOwnerPl || pOwnerPl->Cosmetics()->m_Trail != TRAILTYPE_DOT)
	{
		Reset();
		return;
	}
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		return;

	m_Pos = pOwner->GetPos();
}

void CDotTrail::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);
	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if(!pOwnerChr || !pSnapPlayer)
		return;

	if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowTrails)
		return;

	if(pOwnerChr->IsPaused())
		return;

	if(!pOwnerChr->TeamMask().test(SnappingClient))
		return;

	if(pSnapPlayer->GetCharacter() && pOwnerChr)
		if(!pOwnerChr->CanSnapCharacter(SnappingClient))
			return;

	if(pOwnerChr->GetPlayer()->m_Vanish && SnappingClient != pOwnerChr->GetPlayer()->GetCid() && SnappingClient != -1)
		if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
			return;

	CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(GetId());
	if(!pProj)
		return;

	vec2 Pos = m_Pos + pOwnerChr->GetVelocity();
	if(m_Owner == SnappingClient)
		Pos = pOwnerChr->GetPredictedPos(pOwnerChr->m_Pos, pOwnerChr->m_PrevPos);

	pProj->m_X = round_to_int(Pos.x * 100.0f);
	pProj->m_Y = round_to_int(Pos.y * 100.0f);
	pProj->m_Type = WEAPON_HAMMER;
	pProj->m_Owner = m_Owner;
	pProj->m_StartTick = 0;
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
}
