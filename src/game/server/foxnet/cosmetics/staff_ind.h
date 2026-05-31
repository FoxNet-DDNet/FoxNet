#ifndef GAME_SERVER_FOXNET_COSMETICS_STAFF_IND_H
#define GAME_SERVER_FOXNET_COSMETICS_STAFF_IND_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CStaffInd : public CEntityOwned
{
	enum
	{
		BALL,
		ARMOR,
		BALL_FRONT,
		NUM_IDS
	};

	std::optional<int> m_aIds[NUM_IDS];
	vec2 m_aPos[2];

	float m_Dist;
	bool m_BallFirst;

public:
	CStaffInd(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_STAFF_IND_H
