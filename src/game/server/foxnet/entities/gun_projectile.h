// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_GUN_PROJECTILE_H
#define GAME_SERVER_FOXNET_ENTITIES_GUN_PROJECTILE_H

#include <optional>

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/player.h> // EGunType

class CGameWorld;
class CCharacter;

class CGunProjectile : public CEntityOwned
{
public:
	CGunProjectile(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir, vec2 MouseTarget, int Span);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;

private:
	enum EWallBehavior
	{
		WALL_DIE = 0,
		WALL_BOUNCE,
		WALL_PHASE,
	};

	// Real (authoritative) bullet state.
	vec2 m_Direction;
	int m_StartTick;
	int m_LifeSpan;
	int m_VanillaLifeTicks;
	int m_TuneZone;

	// vanilla bullet
	vec2 m_SpawnPos;
	vec2 m_SpawnDir;
	vec2 m_MouseTarget;
	int m_SpawnTick;

	// Vanilla phantom
	vec2 m_VanillaPrevPos;
	bool m_VanillaDead;

	EGunType m_GunType;
	bool m_PhaseGun;
	bool m_ConfettiGun;
	int m_EmoteGun;
	int m_DamageIndEffect;
	bool m_MixedShield;

	CClientMask m_MaskGun;
	CClientMask m_MaskGunOpp;
	CClientMask m_MaskInd;
	CClientMask m_MaskIndOpp;

	// Second snap id needed to draw the two-segment Laser cosmetic.
	std::optional<int> m_ExtraId;

	EWallBehavior WallBehavior() const;

	vec2 RealPos(float Time); // Real bullet position, curved gun path from m_Pos/m_Direction.
	vec2 VanillaPos(int Tick); // Phantom straight bullet position from the launch parameters.
	float GunSpeed();
	float GunCurvature();

	void TickVanillaPhantom();

	// Emits the on-hit effect to a set of viewers (Audience), splitting fancy/plain by
	// each viewer's own gun-hit-effect (ShowIndicators) preference. NewPos is the last
	// air position before impact, CurPos the impact point.
	void EmitHitEffect(vec2 NewPos, vec2 CurPos, vec2 Direction, CClientMask Audience);

	void SnapCosmeticBullet(int SnappingClient);
	void SnapVanillaBullet(int SnappingClient);
	// Snaps a plain, client-predicted gun projectile from the launch parameters (the
	// vanilla-looking bullet). Used for cosmetics-off viewers and for gun types with no
	// special visual (plain / confetti / emote / phase).
	void SnapProjectileNetObj(int SnappingClient);
};

#endif // GAME_SERVER_FOXNET_ENTITIES_GUN_PROJECTILE_H
