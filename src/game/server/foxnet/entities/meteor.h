// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_METEOR_H
#define GAME_SERVER_FOXNET_ENTITIES_METEOR_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

#include <optional>

class CQuadData;

constexpr int NumGrenades = 3;
constexpr int NumLasers = 3;

class CMeteor : public CEntityOwned
{
	int m_aIds[6];
	int m_aOtherIds[2];

	enum class EState
	{
		Falling = 0,
		Explosion,
	};

	EState m_State;

	vec2 m_TargetPos; // Cursor Position of the Client
	const CQuadData *m_pTargetQuad = nullptr;
	vec2 m_SummonPos; // Somewhere above the Target Pos with x Offset as well

	vec2 m_CurrentPos;

	vec2 FindTargetBlock(vec2 InitialPos);
	vec2 GetRandomStartPos();

	int m_StartTick;
	int m_StartTeam; // DDRace Team

	bool m_IsCosmetic;

public:
	CMeteor(CGameWorld *pGameWorld, int Owner, vec2 TargetPos, bool IsCosmetic = false);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_METEOR_H
