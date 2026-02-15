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
	int m_aLaserIds[NUM_LASERS];
	vec2 m_aTo[NUM_LASERS];
	vec2 m_aFrom[NUM_LASERS];
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
	CSnap m_Snap;

	int m_Lifetime;

	bool m_Switch;

	CClientMask m_TeamMask;

	void SetPowerupVisual();

	CPowerupData m_Data;

	void SetData();

	CClients m_aClients[MAX_CLIENTS];

	void HandleClient(int ClientId);

public:
	CPowerUp(CGameWorld *pGameWorld, CCollision *pCollision, vec2 Pos, EPowerUp Type);

	void OnFire();

	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_PORTAL_H
