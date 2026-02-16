// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_HEARTHAT_H
#define GAME_SERVER_FOXNET_COSMETICS_HEARTHAT_H

#include <base/vmath.h>

#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CHeartHat : public CEntityOwned
{
	enum
	{
		NUM_HEARTS = 2
	};

	int m_aIds[NUM_HEARTS];
	float m_Dist;
	bool m_switch;

public:
	CHeartHat(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_HEARTHAT_H
