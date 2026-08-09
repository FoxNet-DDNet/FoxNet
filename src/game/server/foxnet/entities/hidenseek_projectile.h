// Made by qxdFox
#ifndef GAME_SERVER_FOXNET_ENTITIES_HIDENSEEK_PROJECTILE_H
#define GAME_SERVER_FOXNET_ENTITIES_HIDENSEEK_PROJECTILE_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/server/foxnet/entities/foxnet_entity.h>

class CGameWorld;
class CCharacter;
class CHideAndSeekZone;

// The bullet a seeker shoots in the hide and seek minigame.
// It only ever touches alive hiders of the running round, everything else on the map is left alone.
// It flies like a normal gun bullet, but since it has no owner the client cannot predict it,
// so it drops damage indicators along its path to stay readable on high ping.
class CHideAndSeekProjectile : public CEntityOwned
{
public:
	CHideAndSeekProjectile(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir, int Span, int FreezeTicks);

	void Reset() override;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;

private:
	vec2 m_Direction;
	int m_StartTick;
	int m_LifeSpan;
	int m_FreezeTicks;
	int m_TuneZone;

	CHideAndSeekZone *Zone();
	vec2 FlightPos(float Time);
	CCharacter *IntersectHider(CHideAndSeekZone *pZone, vec2 From, vec2 To, vec2 *pHitPos);
	void CreateIndLine(vec2 Pos, vec2 Direction);
};

#endif // GAME_SERVER_FOXNET_ENTITIES_HIDENSEEK_PROJECTILE_H
