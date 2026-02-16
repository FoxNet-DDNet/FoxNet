#include "rotating_ball.h"

#include "game/server/entities/character.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <cstdlib>
#include <generated/protocol.h>
#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>

CRotatingBall::CRotatingBall(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_ROTATING_BALL, Pos)
{
	m_Pos = Pos;

	m_IsRotating = true;

	m_RotateDelay = Server()->TickSpeed() + 10;
	m_LaserDirAngle = 0;
	m_LaserInputDir = 0;

	m_TableDirV[0][0] = 5;
	m_TableDirV[0][1] = 12;
	m_TableDirV[1][0] = -12;
	m_TableDirV[1][1] = -5;

	m_Id1 = Server()->SnapNewId();
	GameWorld()->InsertEntity(this);
}

void CRotatingBall::Reset()
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("rotatingball", "Reset");

	Server()->SnapFreeId(m_Id1);

	Server()->SnapFreeId(GetId());
	GameWorld()->RemoveEntity(this);
}

void CRotatingBall::Tick()
{
	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_RotatingBall)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	m_Pos = GetCharacter()->GetPos();

	m_RotateDelay--;
	if(m_RotateDelay <= 0)
	{
		m_IsRotating ^= true;

		int DirSelect = rand() % 2;
		m_LaserInputDir = rand() % (m_TableDirV[DirSelect][1] - m_TableDirV[DirSelect][0] + 1) + m_TableDirV[DirSelect][0];
		m_RotateDelay = m_IsRotating ? Server()->TickSpeed() + (rand() % (7 - 3 + 1) + 3) : Server()->TickSpeed() + (rand() % (20 - 5 + 1) + 5);
	}

	if(m_IsRotating)
		m_LaserDirAngle += m_LaserInputDir;

	m_LaserPos.x = 65 * sin(m_LaserDirAngle * pi / 180.0f);
	m_LaserPos.y = 65 * cos(m_LaserDirAngle * pi / 180.0f);

	m_ProjPos.x = m_LaserPos.x + 22 * sin(Server()->Tick() * 13 * pi / 180.0f);
	m_ProjPos.y = m_LaserPos.y + 22 * cos(Server()->Tick() * 13 * pi / 180.0f);
}

void CRotatingBall::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;


	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);
	vec2 Pos = GetCharacter()->GetPredictedPos(SnappingClient, false) + m_ProjPos;
	vec2 LaserPos = GetCharacter()->GetPredictedPos(SnappingClient, false) + m_LaserPos;

	GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), GetId(), LaserPos, LaserPos, Server()->Tick(), m_Owner, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);

	CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(m_Id1);
	if(!pProj)
		return;

	pProj->m_X = round_to_int(Pos.x * 100.0f);
	pProj->m_Y = round_to_int(Pos.y * 100.0f);
	pProj->m_Type = WEAPON_HAMMER;
	pProj->m_Owner = m_Owner;
	pProj->m_StartTick = 0;
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
}
