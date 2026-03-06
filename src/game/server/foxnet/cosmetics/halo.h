// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_HALO_H
#define GAME_SERVER_FOXNET_COSMETICS_HALO_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CHalo : public CEntityOwned
{
	enum
	{
		NUM_IDS = 6
	};

	class CSnapData
	{
	public:
		vec2 m_Pos;
		int m_Id;
	} m_aSnap[NUM_IDS];
	int m_StartTick;

	void SetData();

public:
	CHalo(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_HALO_H
