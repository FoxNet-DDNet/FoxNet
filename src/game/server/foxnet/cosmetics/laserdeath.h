// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_LASER_DEATH_H
#define GAME_SERVER_FOXNET_COSMETICS_LASER_DEATH_H

#include <game/server/entity.h>
#include <game/server/gameworld.h>
#include <base/vmath.h>

constexpr int MAX_PARTICLES = 13;
constexpr float SNAPDELAY = 5.5f;

class CSnapData
{
public:
	int m_aIds[MAX_PARTICLES];
	vec2 m_aPos[MAX_PARTICLES];
	int m_StartTick[MAX_PARTICLES];
	int m_TeamMask;
};

class CLaserDeath : public CEntity
{
	int m_Owner;
	int m_EndTick;

	CSnapData m_SnapData;
	CClientMask m_Mask;

	bool m_Vanish = false;

public:
	CLaserDeath(CGameWorld *pGameWorld, int Owner, vec2 Pos, CClientMask Mask);

	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_LASER_DEATH_H
