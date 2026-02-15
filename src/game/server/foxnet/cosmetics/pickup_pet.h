// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_COSMETICS_PICKUPPET_H
#define GAME_SERVER_FOXNET_COSMETICS_PICKUPPET_H

#include <base/vmath.h>

#include <game/server/foxnet/entities/foxnet_entity.h>
#include <game/server/gameworld.h>

class CPickupPet : public CFoxNetEntity
{
	enum PetMode
	{
		PET_MODE_AFK,
		PET_MODE_FOLLOW,
		PET_MODE_STATIC,
	};

	vec2 m_aPos;

	float m_aSpeed;

	int m_CurType;
	int64_t m_SwitchDelay;

	int m_PetMode;

public:
	CPickupPet(CGameWorld *pGameWorld, CCollision *pCollision, int Owner, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;

	void SetPetMode(int Mode) { m_PetMode = Mode; }
	int GetPetMode() const { return m_PetMode; }

	void PlayerAfkMode();
	void StaticMode();
	void FollowMode();
};

#endif // GAME_SERVER_FOXNET_COSMETICS_PICKUPPET_H
