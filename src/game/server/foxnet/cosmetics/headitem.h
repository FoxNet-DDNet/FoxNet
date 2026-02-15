// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_HEADITEM_H
#define GAME_SERVER_FOXNET_COSMETICS_HEADITEM_H

#include <base/vmath.h>

#include <game/collision.h>
#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

enum HeadItemType
{
	HEADITEM_SPAWNSOLO = 0,
	HEADITEM_COSMETIC = 1,
};

class CHeadItem : public CFoxNetEntity
{
	int m_Type;
	vec2 m_Offset;

	void SnapPartyHat(int SnappingClient);

	int m_aIds[2];

public:
	CHeadItem(CGameWorld *pGameWorld, CCollision *pCollision, int Owner, vec2 Pos, int Type, vec2 Offset);
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_HEADITEM_H
