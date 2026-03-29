#ifndef GAME_SERVER_FOXNET_COSMETICS_LOVELY_H
#define GAME_SERVER_FOXNET_COSMETICS_LOVELY_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CLovely : public CEntityOwned
{
	enum
	{
		MAX_HEARTS = 3
	};

	float m_SpawnDelay;

	struct SLovelyData
	{
		int m_Id;
		vec2 m_Pos;
		float m_Lifespan;
	};
	SLovelyData m_aData[MAX_HEARTS];
	void SpawnNewHeart();

public:
	CLovely(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_LOVELY_H
