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

class CHeadItem : public CEntityOwned
{
	int m_Type;
	vec2 m_Offset;

	void SnapPartyHat(int SnappingClient);
	void SnapTopHat(int SnappingClient);

	std::optional<int> m_aIds[5];

public:
	CHeadItem(CGameWorld *pGameWorld, int Owner, vec2 Pos, int Type, vec2 Offset);
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_HEADITEM_H
