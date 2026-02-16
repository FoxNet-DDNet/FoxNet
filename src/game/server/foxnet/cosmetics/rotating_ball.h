#ifndef GAME_SERVER_FOXNET_COSMETICS_ROTATING_BALL_H
#define GAME_SERVER_FOXNET_COSMETICS_ROTATING_BALL_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CRotatingBall : public CEntityOwned
{
	int m_Id1;

	int m_RotateDelay;
	int m_LaserDirAngle;
	int m_LaserInputDir;
	bool m_IsRotating;

	vec2 m_LaserPos;
	vec2 m_ProjPos;

	int m_TableDirV[2][2];

public:
	CRotatingBall(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_ROTATING_BALL_H
