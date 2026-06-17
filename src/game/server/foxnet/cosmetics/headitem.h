// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_HEADITEM_H
#define GAME_SERVER_FOXNET_COSMETICS_HEADITEM_H

#include <base/vmath.h>

#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

#include <optional>

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

	float m_SwayAngle = 0.0f;
	float m_SwayVel = 0.0f;

	float m_aAntennaAngles[2] = {0.0f, 0.0f};
	float m_aAntennaVels[2] = {0.0f, 0.0f};
	float m_aNoisePhase[2] = {0.0f, 0.0f};
	bool m_LastFacingLeft = false;
	void SnapAntennae(int SnappingClient);

	std::optional<int> m_aIds[5];

	enum class EDirection
	{
		LEFT,
		RIGHT
	};

	EDirection FacingDirection(int SnappingClient);

public:
	CHeadItem(CGameWorld *pGameWorld, int Owner, vec2 Pos, int Type, vec2 Offset);
	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_COSMETICS_HEADITEM_H
