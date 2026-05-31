/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PROJECTILE_H
#define GAME_SERVER_ENTITIES_PROJECTILE_H

#include "character.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/entity.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

class CProjectile : public CEntity
{
public:
	CProjectile(CGameWorld *pGameWorld, int MultiMapIdx, int Type, int Owner, vec2 Pos, vec2 Dir,
		int Span, int FreezeTicks, bool Explosive, int SoundImpact, vec2 InitDir, int Layer = 0, int Number = 0);

	vec2 GetPos(float Time, int ClientId = -1);

	CNetObj_Projectile NetInfoVanilla() const;
	bool NetIsInfoLegacyCompatible() const;
	CNetObj_DDRaceProjectile NetInfoLegacy() const;
	CNetObj_DDNetProjectile NetInfo() const;

	void Reset() override;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;

	int m_GunType = GUNTYPE_NONE;
	bool m_MixedShield = false; // Switching between shield and heart
	std::optional<int> m_ExtraId = -1; // Needed for m_LaserGun
	CClientMask m_CosmeticMask;
	CClientMask m_OppCosmeticMask;

	void HandleGunHit(vec2 NewPos, CClientMask Mask, CCharacter *pOwnerChr, CCharacter *pTargetChr);

private:
	vec2 m_Direction;
	int m_LifeSpan;
	int m_Owner;
	int m_Type;
	int m_SoundImpact;
	int m_StartTick;
	bool m_Explosive;

	// DDRace

	int m_Bouncing;
	int m_FreezeTicks;
	int m_TuneZone;
	bool m_BelongsToPracticeTeam;
	int m_DDRaceTeam;
	bool m_IsSolo;
	vec2 m_InitDir;

public:
	// <FoxNet
	int StartTick() const { return m_StartTick; }
	int Type() const { return m_Type; }
	// FoxNet>

	void SetBouncing(int Value);

	bool CanCollide(int ClientId) override;
	int GetOwnerId() const override { return m_Owner; }
};

#endif
