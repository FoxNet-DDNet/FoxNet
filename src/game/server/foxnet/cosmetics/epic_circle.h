#ifndef GAME_SERVER_FOXNET_COSMETICS_EPIC_CIRCLE_H
#define GAME_SERVER_FOXNET_COSMETICS_EPIC_CIRCLE_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CEpicCircle : public CFoxNetEntity
{
	enum
	{
		MAX_PARTICLES = 9
	};

	int m_aIds[MAX_PARTICLES];
	vec2 m_RotatePos[MAX_PARTICLES];

public:
	CEpicCircle(CGameWorld *pGameWorld, CCollision *pCollision, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_EPIC_CIRCLE_H
