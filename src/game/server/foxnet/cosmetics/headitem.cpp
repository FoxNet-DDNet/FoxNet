// Made by qxdFox
#include "headitem.h"

#include <base/log.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <optional>

CHeadItem::EDirection CHeadItem::FacingDirection(int SnappingClient)
{
	bool FacingLeft = normalize(GetCharacter()->GetSnappedTargetPos(SnappingClient)).x < 0;
	return FacingLeft ? EDirection::LEFT : EDirection::RIGHT;
}

CHeadItem::CHeadItem(CGameWorld *pGameWorld, int Owner, vec2 Pos, int Type, vec2 Offset) :
	CEntityOwned(pGameWorld, Owner, CGameWorld::ENTTYPE_HEAD_ITEM, Pos)
{
	m_Pos = Pos;

	// Type of Entity
	m_Type = Type;
	m_Offset = Offset;

	if(m_Type == HEADITEM_COSMETIC)
	{
		for(size_t i = 0; i < std::size(m_aIds); i++)
			m_aIds[i] = Server()->SnapNewId();
		std::sort(std::begin(m_aIds), std::end(m_aIds), [](const std::optional<int> &a, const std::optional<int> &b) { return a.value() < b.value(); });
	}
	else
		std::fill(std::begin(m_aIds), std::end(m_aIds), std::nullopt);
	GameWorld()->InsertEntity(this);
}

void CHeadItem::Reset()
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("headitem", "Reset");

	for(size_t i = 0; i < std::size(m_aIds); i++)
	{
		if(m_aIds[i].has_value())
			Server()->SnapFreeId(m_aIds[i].value());
	}

	m_MarkedForDestroy = true;
}

void CHeadItem::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(!GetPlayer())
	{
		Reset();
		return;
	}

	switch(m_Type)
	{
	case HEADITEM_SPAWNSOLO:
		if(!GetCharacter() || !GetCharacter()->m_SpawnSolo)
		{
			Reset();
			return;
		}
		break;
	case HEADITEM_COSMETIC:
		if(GetPlayer()->Cosmetics()->m_HatType == EHatType::None)
		{
			Reset();
			return;
		}
		break;
	default:
		Reset();
		return;
	}

	if(!GetCharacter())
		return;
	m_Pos = GetCharacter()->GetPos();

	const vec2 Vel = GetCharacter()->GetVelocity();
	const vec2 PrevVel = GetCharacter()->Core()->m_PrevVel;

	const bool FacingLeft = GetCharacter()->Input()->m_TargetX < 0;

	const float CurSpeed = length(Vel);

	bool XStopped = abs(Vel.x) < 0.6f && abs(PrevVel.x) > 2.0f;
	bool YStopped = abs(Vel.y) < 0.6f && abs(PrevVel.y) > 2.0f;

	constexpr float StopStrength = 0.08f;
	if(XStopped)
	{
		float StopImpulse = (PrevVel.x / 10.0f) * StopStrength;
		for(int i = 0; i < 2; i++)
		{
			float DirectionMultiplier = FacingLeft ? 1.0f : -1.0f;
			m_aAntennaVels[i] += StopImpulse * DirectionMultiplier * (1.0f + i * 0.2f);
		}
	}
	if(YStopped)
	{
		float StopImpulse = (PrevVel.y / 10.0f) * StopStrength;
		for(int i = 0; i < 2; i++)
		{
			float DirectionMultiplier = FacingLeft ? 1.0f : -1.0f;
			m_aAntennaVels[i] += StopImpulse * DirectionMultiplier * (1.0f + i * 0.2f);
		}
	}

	const float YVelContribution = std::clamp(Vel.y * 0.01f * (FacingLeft ? -1.0f : 1.0f), -0.4f, 0.4f);
	const float TargetAngle = std::clamp(-Vel.x * 0.03f + YVelContribution, -0.6f, 0.6f);
	const float AngleDiff = TargetAngle - m_SwayAngle;

	constexpr float SpringStiffness = 0.15f;
	constexpr float Damping = 0.85f;

	m_SwayVel += AngleDiff * SpringStiffness;
	m_SwayVel *= Damping;

	m_SwayAngle += m_SwayVel;

	for(int i = 0; i < 2; i++)
	{
		if(CurSpeed > 2.0f)
		{
			m_aNoisePhase[i] += 0.12f + i * 0.08f;

			constexpr float BobFrequency = 0.05f; // lower for more frequent bounces
			if(std::sin(m_aNoisePhase[i]) > BobFrequency)
			{
				const float BobStrength = std::min(CurSpeed * 0.001f, 0.04f);
				float RandomBob = (std::sin(m_aNoisePhase[i] * 7.3f) * 0.5f + 0.5f) * BobStrength; // bounce strength
				m_aAntennaVels[i] += RandomBob * (Vel.x < 0 ? 1.0f : -1.0f);
			}
		}

		float AntennaTarget = TargetAngle * (1.0f + i * 0.2f);
		float AntennaDiff = AntennaTarget - m_aAntennaAngles[i];

		m_aAntennaVels[i] += AntennaDiff * SpringStiffness;
		m_aAntennaVels[i] *= Damping;
		m_aAntennaAngles[i] += m_aAntennaVels[i];
	}
}

void CHeadItem::Snap(int SnappingClient)
{
	if(SnappingClient < 0 || SnappingClient >= MAX_CLIENTS)
		return;

	if(NetworkClipped(SnappingClient))
		return;

	CPlayer *pSnapPlayer;
	if(!CanSnapEntity(SnappingClient, &pSnapPlayer))
		return;

	EHatType PlHatType = GetPlayer()->Cosmetics()->m_HatType;

	if(m_Type != HEADITEM_SPAWNSOLO)
	{
		if(m_Owner != SnappingClient && pSnapPlayer && !pSnapPlayer->Acc()->m_Configs.m_Cosmetics.m_ShowHats)
			return;

		if(m_Type == HEADITEM_COSMETIC)
		{
			switch(PlHatType)
			{
			case EHatType::Party: SnapPartyHat(SnappingClient); return;
			case EHatType::Tophat: SnapTopHat(SnappingClient); return;
			case EHatType::Antennae: SnapAntennae(SnappingClient); return;
			default: break;
			}
		}

		if(GetCharacter()->m_SpawnSolo)
			return;
	}

	int Type = 0;
	int SubType = 0;
	int Flags = 0;

	switch(m_Type)
	{
	case HEADITEM_SPAWNSOLO:
		Type = POWERUP_ARMOR;
		SubType = 0;
		break;
	case HEADITEM_COSMETIC:
		Type = POWERUP_WEAPON;
		SubType = (int)PlHatType - 1;
		Flags |= GetCharacter()->Acc()->m_Configs.m_HatItemFlags;
		break;
	default:
		break;
	}

	int Rotation = 0;
	if(Flags == PICKUPFLAG_ROTATE + PICKUPFLAG_XFLIP + PICKUPFLAG_YFLIP)
		Rotation = 270;
	else if(Flags == PICKUPFLAG_XFLIP + PICKUPFLAG_YFLIP)
		Rotation = 180;
	else if(Flags == PICKUPFLAG_ROTATE)
		Rotation = 90;

	Flags |= PICKUPFLAG_NO_PREDICT;

	SnapCosmeticPickup(SnappingClient, GetId().value(), Flags, m_Owner, m_Offset, Type, SubType, Rotation, -1, COSMETIC_FLAG_ANCHORED);
}

void CHeadItem::SnapPartyHat(int SnappingClient)
{
	const int NumPoints = 2;
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	vec2 HatFrom[NumPoints] = {vec2(19.0f, -48.0f), vec2(19.0f, -48.0f)};
	vec2 HatTo[NumPoints] = {vec2(-13.5f, -14.0f), vec2(17.0f, -9.0f)};
	int Flags[NumPoints] = {COSMETIC_FLAG_ANCHORED, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD};

	bool Still = std::abs(pOwnerChr->GetVelocity().x) < 0.01f && std::abs(pOwnerChr->GetVelocity().y) < 0.01f && pOwnerChr->IsGrounded();

	if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
	{
		for(int i = 0; i < NumPoints; i++)
		{
			vec2 Center = vec2(0, 0);
			Rotate(Center, &HatFrom[i], 0.2f);
			Rotate(Center, &HatTo[i], 0.2f);
			HatFrom[i] += vec2(-1.5f, 3.5f);
			HatTo[i] += vec2(-1.5f, 3.5f);
		}
	}

	bool FacingRight = FacingDirection(SnappingClient) == EDirection::RIGHT;

	for(size_t i = 0; i < NumPoints; i++)
	{
		if(FacingRight)
		{
			HatFrom[i].x = -HatFrom[i].x;
			HatTo[i].x = -HatTo[i].x;
		}

		if(!m_aIds[i].has_value())
			continue;
		SnapCosmeticLaser(SnappingClient, m_aIds[i].value(), m_Owner, HatFrom[i], HatTo[i], 4, LASERTYPE_GUN, -1, Flags[i]);
	}
}

void CHeadItem::SnapTopHat(int SnappingClient)
{
	const vec2 Center = vec2(0, 0);
	const int NumPoints = 5;
	CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner);

	vec2 HatFrom[NumPoints] = {
		vec2(15.0f, -43.0f), // top line
		vec2(-12.5f, -44.0f), // right line down
		vec2(15.0f, -43.0f), // left line down
		vec2(26.0f, -19.0f), // bottom line
		vec2(-26.5f, -21.0f), // bottom dot
	};

	vec2 HatTo[NumPoints] = {
		vec2(-12.5f, -44.0f), // top line
		vec2(-12.5f, -20.0f), // right line down
		vec2(14.5f, -20.0f), // left line down
		vec2(-26.5f, -21.0f), // bottom line
		vec2(-26.5f, -21.0f), // bottom dot
	};

	int HatTickOffset[NumPoints] = {
		5, // top line
		5, // right line down
		5, // left line down
		4, // Long Bottom Line
		4 // bottom dot
	};

	int Flags[NumPoints] = {
		COSMETIC_FLAG_ANCHORED,
		COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD,
		COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD,
		COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD | COSMETIC_LASER_FLAG_TO_HEAD,
		COSMETIC_FLAG_ANCHORED};

	bool Still = std::abs(pOwnerChr->GetVelocity().x) < 0.01f && std::abs(pOwnerChr->GetVelocity().y) < 0.01f && pOwnerChr->IsGrounded();
	for(int i = 0; i < NumPoints; i++)
	{
		Rotate(Center, &HatFrom[i], 0.2f);
		Rotate(Center, &HatTo[i], 0.2f);

		if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
		{
			Rotate(Center, &HatFrom[i], 0.2f);
			Rotate(Center, &HatTo[i], 0.2f);
			HatFrom[i] += vec2(-1.5f, 3.5f);
			HatTo[i] += vec2(-1.5f, 3.5f);
		}
	}
	bool FacingRight = FacingDirection(SnappingClient) == EDirection::RIGHT;

	for(size_t i = 0; i < NumPoints; i++)
	{
		if(FacingRight)
		{
			HatFrom[i].x = -HatFrom[i].x;
			HatTo[i].x = -HatTo[i].x;
		}
		if(!m_aIds[i].has_value())
			continue;
		SnapCosmeticLaser(SnappingClient, m_aIds[i].value(), m_Owner, HatFrom[i], HatTo[i], HatTickOffset[i], LASERTYPE_GUN, -1, Flags[i]);
	}
}

void CHeadItem::SnapAntennae(int SnappingClient)
{
	const int NumPoints = 2;
	CCharacter *pOwnerChr = GetCharacter();

	vec2 HatFrom[NumPoints] = {vec2(-23.5f, -45.0f), vec2(0.0f, -43.5f)};
	vec2 HatTo[NumPoints] = {vec2(-5.0f, -9.0f), vec2(5.0f, -9.0f)};

	int Flags[NumPoints] = {COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD, COSMETIC_FLAG_ANCHORED | COSMETIC_LASER_FLAG_FROM_HEAD};

	bool Still = std::abs(pOwnerChr->GetVelocity().x) < 0.01f && std::abs(pOwnerChr->GetVelocity().y) < 0.01f && pOwnerChr->IsGrounded();

	if(Still && (pOwnerChr->GetPlayer()->IsPaused() || pOwnerChr->GetPlayer()->IsAfk()))
	{
		for(int i = 0; i < NumPoints; i++)
		{
			vec2 Center = vec2(0, 0);
			Rotate(Center, &HatFrom[i], -0.2f);
			Rotate(Center, &HatTo[i], -0.2f);
			HatFrom[i] += vec2(1.5f, 3.5f);
			HatTo[i] += vec2(1.5f, 3.5f);
		}
	}

	bool FacingLeft = FacingDirection(SnappingClient) == EDirection::LEFT;

	// Detect direction change and add impulse for bouncy turn animation
	if(FacingLeft != m_LastFacingLeft)
	{
		// Add a velocity impulse to make antennae bounce when turning (reduced for gentler effect)
		for(int i = 0; i < 2; i++)
		{
			// Direction change causes a spring reaction
			float TurnImpulse = FacingLeft ? -0.12f : 0.12f;
			m_aAntennaVels[i] += TurnImpulse * (1.0f + i * 0.3f);
		}
		m_LastFacingLeft = FacingLeft;
	}

	for(size_t i = 0; i < NumPoints; i++)
	{
		vec2 From = HatFrom[i];
		vec2 To = HatTo[i];

		if(FacingLeft)
		{
			From.x = -From.x;
			To.x = -To.x;
		}

		// Apply independent spring physics to each antenna
		float SwayAmount = m_aAntennaAngles[i];

		// Rotate the tip (From) around the base (To)
		vec2 Rel = From - To;
		Rotate(vec2(0, 0), &Rel, SwayAmount);
		From = To + Rel;

		if(!m_aIds[i].has_value())
			continue;

		SnapCosmeticLaser(SnappingClient, m_aIds[i].value(), m_Owner, From, To, 4, LASERTYPE_GUN, -1, Flags[i]);
	}
}
