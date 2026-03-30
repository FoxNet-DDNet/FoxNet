#include "text.h"

#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>

constexpr float CellSize = 16.0f;

CLaserText::CLaserText(CGameWorld *pGameWorld, int MultiMapIdx, int Owner, vec2 Pos, int AliveTicks, const char *pText) :
	CText(pGameWorld, MultiMapIdx, Owner, Pos, AliveTicks, pText, CGameWorld::ENTTYPE_LASER)
{
	m_CurTicks = Server()->Tick();
	m_StartTick = Server()->Tick();
	m_AliveTicks = AliveTicks;
	m_Owner = Owner;

	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(Owner);
	m_Mask = CClientMask().set();
	if(pOwnerChar)
		m_Mask = pOwnerChar->TeamMask();

	str_copy(m_aText, pText);
	m_Pos = Pos;
	SetData(CellSize);

	GameWorld()->InsertEntity(this);
}

void CLaserText::Snap(int SnappingClient)
{
	if(!m_Mask.test(SnappingClient))
		return;

	for(const auto &Data : m_vData)
	{
		vec2 Pos = Data.m_Pos - vec2(m_CenterX, 0);
		if(NetworkClipped(SnappingClient, Pos))
			continue;

		CNetObj_DDNetLaser *pObj = Server()->SnapNewItem<CNetObj_DDNetLaser>(Data.m_Id);
		if(!pObj)
			return;

		pObj->m_ToX = Pos.x;
		pObj->m_ToY = Pos.y;
		pObj->m_FromX = Pos.x;
		pObj->m_FromY = Pos.y;
		pObj->m_StartTick = Server()->Tick();
		pObj->m_Owner = m_Owner;
		pObj->m_Type = LASERTYPE_RIFLE;
		pObj->m_Flags = LASERFLAG_NO_PREDICT;
	}
}
