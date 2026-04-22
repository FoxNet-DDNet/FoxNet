#include "lovely.h"

#include <base/log.h>
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

CLovely::CLovely(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_LOVELY, Pos)
{
	m_SpawnDelay = 0;
	for(int i = 0; i < MAX_HEARTS; i++)
		m_aData[i].m_Id = Server()->SnapNewId();
	GameWorld()->InsertEntity(this);
}

void CLovely::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("lovely", "Reset");

	for(int i = 0; i < MAX_HEARTS; i++)
		Server()->SnapFreeId(m_aData[i].m_Id);

	m_MarkedForDestroy = true;
}

void CLovely::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(!GetPlayer() || !GetPlayer()->Cosmetics()->m_Lovely)
	{
		Reset();
		return;
	}
	if(!GetCharacter())
		return;

	m_Pos = GetCharacter()->GetPos();

	m_SpawnDelay--;
	if(m_SpawnDelay <= 0)
	{
		SpawnNewHeart();
		int SpawnTime = 45;
		m_SpawnDelay = Server()->TickSpeed() - (rand() % (SpawnTime - (SpawnTime - 10) + 1) + (SpawnTime - 10));
	}

	for(int i = 0; i < MAX_HEARTS; i++)
	{
		if(m_aData[i].m_Lifespan == -1)
			continue;

		m_aData[i].m_Lifespan--;
		m_aData[i].m_Pos.y -= 2.4f;

		if(m_aData[i].m_Lifespan == 0 || GetCollision()->TestBox(m_aData[i].m_Pos, vec2(14.f, 14.f)))
			m_aData[i].m_Lifespan = -1;
	}
}

void CLovely::SpawnNewHeart()
{
	for(int i = 0; i < MAX_HEARTS; i++)
	{
		if(m_aData[i].m_Lifespan > 0)
			continue;

		CCharacter *pOwner = GetCharacter();
		m_aData[i].m_Lifespan = Server()->TickSpeed() / 2.5f;
		m_aData[i].m_Pos = vec2(pOwner->GetPos().x + (rand() % 50 - 25), pOwner->GetPos().y - 30);
		pOwner->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		break;
	}
}

void CLovely::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowEffects)
		return;

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);
	for(int i = 0; i < MAX_HEARTS; i++)
	{
		if(m_aData[i].m_Lifespan == -1)
			continue;
		SnapCosmeticPickupPos(SnappingClient, m_aData[i].m_Id, PICKUPFLAG_NO_PREDICT, m_Owner, m_aData[i].m_Pos, POWERUP_HEALTH, 0);
	}
}
