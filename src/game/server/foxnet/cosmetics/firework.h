// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_FIREWORK_H
#define GAME_SERVER_FOXNET_COSMETICS_FIREWORK_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

#include <cstdint>

static constexpr int MAX_FIREWORKS = 25;

class CFirework : public CEntityOwned
{
	int64_t m_StartTick;

	enum class State
	{
		NONE = 0,
		START,
		EXPLOSION,
	} m_State;

	float m_aLifetime[MAX_FIREWORKS];
	vec2 m_StartPos;

	vec2 m_aPos[MAX_FIREWORKS];
	vec2 m_aVel[MAX_FIREWORKS];

	int m_aIds[MAX_FIREWORKS];

public:
	CFirework(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_FIREWORK_H
