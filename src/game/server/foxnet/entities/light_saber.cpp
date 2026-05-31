// Made by qxdFox
#include "light_saber.h"

#include "foxnet_entity.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <vector>

CLightSaber::CLightSaber(CGameWorld *pGameWorld, int Owner, vec2 Pos) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_LIGHTSABER, Pos)
{
	m_Pos = Pos;
	m_From = Pos;
	m_To = Pos;

	GameWorld()->InsertEntity(this);
}

void CLightSaber::Reset()
{
	if(m_MarkedForDestroy)
		return;
	if(g_Config.m_SvLogExtra >= 2)
		log_info("lightsaber", "Reset");

	if(GetCharacter())
		GetCharacter()->m_pLightSaber = nullptr;
	m_MarkedForDestroy = true;
}

void CLightSaber::OnFire()
{
	if(m_State == STATE_RETRACTED || m_State == STATE_RETRACTING)
		m_State = STATE_EXTENDING;
	else if(m_State == STATE_EXTENDED || m_State == STATE_EXTENDING)
		m_State = STATE_RETRACTING;
}

void CLightSaber::Tick()
{
	if(m_MarkedForDestroy)
		return;
	CCharacter *pChr = GameServer()->GetPlayerChar(m_Owner);

	if(!pChr)
	{
		Reset();
		return;
	}
	if(pChr->GetActiveWeapon() != WEAPON_LIGHTSABER)
	{
		if(m_Length > 0)
			m_State = STATE_RETRACTING;
		else
		{
			Reset();
			return;
		}
	}
	if(pChr->m_FreezeTime > 0 || pChr->IsPaused())
	{
		if(m_Length > 0)
			m_State = STATE_RETRACTING;
	}

	if(m_State == STATE_EXTENDING)
	{
		if(Server()->Tick() % 5 == 0)
			GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE, pChr->TeamMask());
		m_Length += LIGHT_SABER_SPEED;
		if(m_Length > LIGHT_SABER_MAX_LENGTH)
		{
			m_Length = LIGHT_SABER_MAX_LENGTH;
			m_State = STATE_EXTENDED;
		}
	}
	else if(m_State == STATE_RETRACTING)
	{
		if(Server()->Tick() % 5 == 0)
			GameServer()->CreateSound(m_Pos, SOUND_HOOK_LOOP, pChr->TeamMask());
		m_Length -= LIGHT_SABER_SPEED;
		if(m_Length < 0)
		{
			m_Length = 0;
			m_State = STATE_RETRACTED;
		}
	}
	else if(m_State == STATE_RETRACTED)
	{
		Reset();
		return;
	}
	m_Pos = pChr->m_Pos;
	m_From = vec2(0, 0);
	m_To = vec2(0, 0);
	vec2 WantedFrom = m_To + normalize(vec2(pChr->Input()->m_TargetX, pChr->Input()->m_TargetY)) * m_Length;
	GetCollision()->IntersectLine(m_To, WantedFrom, &m_From, 0);

	if(pChr->Core()->m_Solo)
		return;

	std::vector<CCharacter *> HitChars = GameWorld()->IntersectedCharacters(m_From, m_To, 6.0f, GameServer()->GetPlayerChar(m_Owner));
	if(HitChars.empty())
		return;

	for(CCharacter *pHit : HitChars)
	{
		if(pChr->Team() != TEAM_SUPER && pChr->Team() != pHit->Team())
			continue;
		if(pChr->Core()->m_Solo)
			continue;

		pHit->SetEmote(EMOTE_PAIN, Server()->Tick() + 2);
	}
}

void CLightSaber::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(!GetCharacter())
		return;

	if(SnappingClient != SERVER_DEMO_CLIENT)
	{
		const CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
		if(!pSnapPlayer)
			return;

		if(!TeamMask().test(SnappingClient))
			return;

		if(pSnapPlayer->GetCharacter() && GetCharacter())
			if(!GetCharacter()->CanSnapCharacter(SnappingClient))
				return;

		if(GetPlayer()->m_Vanish && SnappingClient != GetPlayer()->GetCid() && SnappingClient != -1)
			if(!pSnapPlayer->m_Vanish && Server()->GetAuthedState(SnappingClient) < AUTHED_ADMIN)
				return;
	}

	if(m_Length <= 0)
		return;

	vec2 From = GetCharacter()->GetPredictedPos(SnappingClient, false) + m_From;
	vec2 To = GetCharacter()->GetPredictedPos(SnappingClient, false) + m_To;

	const vec2 WantedFrom = To + normalize(vec2(GetCharacter()->Input()->m_TargetX, GetCharacter()->Input()->m_TargetY)) * m_Length;
	GetCollision()->IntersectLine(To, WantedFrom, &From, 0);

	const int SnapVer = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);
	if(!GetId().has_value())
		return;
	GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), GetId().value(), To, From, Server()->Tick() - 3, m_Owner, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
}
