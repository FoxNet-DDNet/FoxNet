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

constexpr float CellSize = 8.0f;

CProjectileText::CProjectileText(CGameWorld *pGameWorld, int MultiMapIdx, int Owner, vec2 Pos, int AliveTicks, const char *pText, int Type) :
	CText(pGameWorld, MultiMapIdx, Owner, Pos, AliveTicks, pText, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_CurTicks = Server()->Tick();
	m_StartTick = Server()->Tick();
	m_AliveTicks = AliveTicks;
	m_Owner = Owner;
	m_Type = Type;

	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(Owner);
	m_Mask = CClientMask().set();
	if(pOwnerChar)
		m_Mask = pOwnerChar->TeamMask();

	str_copy(m_aText, pText);
	m_Pos = Pos;
	SetData(CellSize);

	GameWorld()->InsertEntity(this);
}

void CProjectileText::Snap(int SnappingClient)
{
	if(!m_Mask.test(SnappingClient))
		return;
	const int TickParity = Server()->Tick() & 1;

	size_t NumIds = m_vData.size();

	int Idx = 0;
	for(const auto &Data : m_vData)
	{
		vec2 Pos = Data.m_Pos - vec2(m_CenterX, 0);
		if(NetworkClipped(SnappingClient, Pos))
			continue;
		if(NumIds >= 135 && ((Idx + TickParity) & 1) != 0)
		{
			Idx++;
			continue;
		}

		CNetObj_DDNetProjectile *pProj = Server()->SnapNewItem<CNetObj_DDNetProjectile>(Data.m_Id);
		if(!pProj)
		{
			Idx++;
			continue;
		}
		pProj->m_X = round_to_int(Pos.x * 100.0f);
		pProj->m_Y = round_to_int(Pos.y * 100.0f);
		pProj->m_Type = m_Type;
		pProj->m_Owner = m_Owner;
		pProj->m_StartTick = 0;
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		Idx++;
	}
}
