// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_WORM_H
#define GAME_SERVER_FOXNET_ENTITIES_WORM_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/server/entity.h>
#include <game/server/gameworld.h>

#include <optional>

enum class EWormTargetType
{
	ClosestInFront = 0,
	RandomTee,
	RandomPosition,
};

class CWormConfig
{
public:
	int m_Segments = 8;
	float m_Length = 40.0f; // length of a segment
	float m_Speed = 38.0f;
	float m_GroundAcceleration = 0.18f;
	float m_GroundTurnRadiusTiles = 15.0f;
	float m_AirGravity = 0.5f;
	float m_TeeRetargetSeconds = 5.0f;
	float m_RandomPositionTimeoutSeconds = 10.0f;
	float m_RandomSearchRadiusTiles = 30.0f;
	float m_NearbySolidRangeTiles = 15.0f;
	EWormTargetType m_TargetType = EWormTargetType::RandomPosition;
};

class WormSegment
{
public:
	vec2 m_From;
	vec2 m_To;

	std::optional<int> m_Id; // SnapId
};

class CWorm : public CEntity
{
	std::vector<WormSegment> m_vSegments;
	CWormConfig m_WormConfig;

	vec2 m_TargetPos;
	vec2 m_Velocity;
	int m_TargetClientId;
	int m_TargetStartTick;
	bool m_HasTarget;
	bool m_UseCachedRandomTargetNext = false;

	vec2 m_StartPos;

	vec2 FindTargetPos();
	bool CanSnapTo(int SnappingClient);

	int m_LastEffectTick;

public:
	CWorm(CGameWorld *pGameWorld, int MultiMapIdx, vec2 Pos, CWormConfig WormConfig);
	~CWorm() override;

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_WORM_H
