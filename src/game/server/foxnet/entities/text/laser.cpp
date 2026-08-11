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

	// the client reads the owner out of the laser, so it has to be in its own id space
	int Owner = m_Owner;
	if(!Server()->Translate(Owner, SnappingClient))
		Owner = -1;

	for(const auto &Data : m_vData)
	{
		vec2 Pos = Data.m_Pos - vec2(m_CenterX, 0);
		if(NetworkClipped(SnappingClient, Pos))
			continue;

		CNetObj_DDNetLaser Obj = {};

		Obj.m_ToX = Pos.x;
		Obj.m_ToY = Pos.y;
		Obj.m_FromX = Pos.x;
		Obj.m_FromY = Pos.y;
		Obj.m_StartTick = Server()->Tick();
		Obj.m_Owner = Owner;
		Obj.m_Type = LASERTYPE_RIFLE;
		Obj.m_Flags = LASERFLAG_NO_PREDICT;
		Server()->SnapNewItem(Data.m_Id, Obj);
	}
}
