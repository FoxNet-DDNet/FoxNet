// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_LISSAJOUS_H
#define GAME_SERVER_FOXNET_COSMETICS_LISSAJOUS_H

#include <game/server/entity.h>
#include <game/server/gameworld.h>
#include <base/vmath.h>

class CLissajous : public CEntity
{
	enum
	{
		NUM_POINTS = 30,
		NUM_IDS = NUM_POINTS + 1,
	};


	class CSnapData
	{
	public:
		int m_Id;
		vec2 m_From;
		vec2 m_To;
	} m_Snap[NUM_IDS];


	int m_Owner;

	int m_StartTick;

	float Flow();
	vec2 LissajousPos(int Point);

public:
	CLissajous(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_LISSAJOUS_H
