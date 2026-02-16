// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_LASER_DEATH_H
#define GAME_SERVER_FOXNET_COSMETICS_LASER_DEATH_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

constexpr int MAX_PARTICLES = 28;
constexpr int TICKDELAY = 5;

class CSnapData
{
public:
	int m_aIds[MAX_PARTICLES];
	vec2 m_aPos[MAX_PARTICLES];
	int m_StartTick[MAX_PARTICLES];
	int m_TeamMask;
};

class CLaserDeath : public CEntityOwned
{
	int m_EndTick;

	CSnapData m_SnapData;
	CClientMask m_Mask;

	bool m_Vanish = false;

public:
	CLaserDeath(CGameWorld *pGameWorld, int Owner, vec2 Pos, CClientMask Mask);
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_LASER_DEATH_H
