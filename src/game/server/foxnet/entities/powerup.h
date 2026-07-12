// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_POWERUP_H
#define GAME_SERVER_FOXNET_ENTITIES_POWERUP_H

#include <base/vmath.h>

#include <game/server/entity.h>
#include <game/server/gameworld.h>

static constexpr int NUM_LASERS = 5;

enum class EPowerUp
{
	INVALID = 0,
	XP,
	MONEY,
	BOOST,
	NUM_TYPES
};

class CPowerupData
{
public:
	EPowerUp m_Type = EPowerUp::INVALID;
	long m_Value = 0;
};

class CSnap
{
public:
	std::optional<int> m_Id;
	vec2 m_To;
	vec2 m_From;
	int m_LaserType;
};

class CClients
{
public:
	bool m_Collected = false;
	bool m_WasLoggedIn = false;

	NETADDR m_Addr;
};

class CPowerUp : public CEntity
{
	std::array<CSnap, NUM_LASERS> m_Snap;

	int m_StartTick;

	int m_Lifetime;
	bool m_Switch;

	int m_MaxCollections = 3;

	CClientMask m_TeamMask;

	void SetPowerupVisual();

	CPowerupData m_Data;

	void SetData();

	CClients m_aClients[MAX_CLIENTS];

	void HandleClient(int ClientId);

public:
	CPowerUp(CGameWorld *pGameWorld, int MultiMapIdx, vec2 Pos);

	void OnFire();

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_POWERUP_H
