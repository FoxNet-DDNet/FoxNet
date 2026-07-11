// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_GUN_PROJECTILE_H
#define GAME_SERVER_FOXNET_ENTITIES_GUN_PROJECTILE_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/player.h>

#include <optional>

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

	vec2 m_Direction;
	int m_StartTick;
	int m_LifeSpan;
	int m_VanillaLifeTicks;
	int m_TuneZone;

	vec2 m_SpawnPos;
	vec2 m_SpawnDir;
	vec2 m_MouseTarget;
	int m_SpawnTick;

	vec2 m_VanillaPrevPos;
	bool m_VanillaDead;

	EGunType m_GunType;
	bool m_PhaseGun;
	bool m_ConfettiGun;
	int m_EmoteGun;
	int m_DamageIndEffect;
	bool m_MixedShield;

	vec2 m_WantedDirection = vec2(0, 0);

	CClientMask m_MaskGun;
	CClientMask m_MaskGunOpp;
	CClientMask m_MaskInd;
	CClientMask m_MaskIndOpp;

	std::optional<int> m_ExtraId;

	EWallBehavior WallBehavior() const;

	vec2 RealPos(float Time);
	vec2 VanillaPos(int Tick);
	float GunSpeed();
	float GunCurvature();
	float SpeedFactor();

	void TickVanillaPhantom();
	void TickSway();
	void TickControl();

	void EmitHitEffect(vec2 NewPos, vec2 CurPos, vec2 Direction, CClientMask Audience);

	void SnapCosmeticBullet(int SnappingClient);
	void SnapVanillaBullet(int SnappingClient);

	void SnapProjectileNetObj(int SnappingClient);

	void SnapGunProjectile(int SnapId, int SnappingClient, int Owner, vec2 Pos, vec2 Direction);
};

#endif // GAME_SERVER_FOXNET_ENTITIES_GUN_PROJECTILE_H
