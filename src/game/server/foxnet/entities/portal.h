// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_PORTAL_H
#define GAME_SERVER_FOXNET_ENTITIES_PORTAL_H

#include "foxnet_entity.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/server/gameworld.h>

struct CPortalData
{
	float m_PortalRadius;
	bool m_Active;
	vec2 m_Pos;
	int m_Team;
};

class CPortal : public CEntityOwned
{
	MACRO_ALLOC_POOL_ID()

	enum
	{
		NUM_PORTALS = 2,
		SEGMENTS = 12,
		NUM_IDS = SEGMENTS + 1,
		NUM_POS = SEGMENTS + 1,
		NUM_PRTCL = 3
	};

	struct CSnapPortal
	{
	public:
		std::optional<int> m_aIds[NUM_IDS];
		vec2 m_aFrom[NUM_POS];
		vec2 m_aTo[NUM_POS];
		std::optional<int> m_aParticleIds[NUM_PRTCL];
	};
	CSnapPortal m_Snap[NUM_PORTALS];

	void SetPortalVisual();

	CPortalData m_aData[NUM_PORTALS];

	int m_State;
	int m_Lifetime; // In ticks

	bool m_aCanTeleport[MAX_CLIENTS];
	int m_aBlockedPortal[MAX_CLIENTS];

	enum States
	{
		STATE_NONE = 0,
		STATE_FIRST_SET,
		STATE_BOTH_SET,
	};

	void RemovePortals();
	bool TrySetPortal();
	void HandleTele();

public:
	CPortal(CGameWorld *pGameWorld, int Owner, vec2 Pos);

	void OnFire();

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITIES_PORTAL_H
