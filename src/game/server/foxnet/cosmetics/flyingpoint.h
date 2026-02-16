#ifndef GAME_SERVER_FOXNET_COSMETICS_FLYINGPOINT_H
#define GAME_SERVER_FOXNET_COSMETICS_FLYINGPOINT_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CFlyingPoint : public CEntityOwned
{
private:
	vec2 m_InitialVel;
	float m_InitialAmount;

	// Either a to clientid is set or a to position
	int m_To;
	vec2 m_ToPos;
	vec2 m_PrevPos;

public:
	CFlyingPoint(CGameWorld *pGameWorld, int Owner, vec2 Pos, int To, vec2 InitialVel, vec2 ToPos = vec2(-1, -1));

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_FLYINGPOINT_H