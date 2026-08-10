#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_MINIGAME_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_MINIGAME_H

#include "zone.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/quad_data.h>

#include <cstddef>
#include <vector>

class CPlayer;
class CCharacter;

/*
 * A stateful zone that owns the players standing inside it.
 *
 * Exactly one minigame can own a player at a time. CZoneManager resolves that once per tick and hands
 * the player over with OnPlayerLeave() followed by OnPlayerEnter(), so a minigame never has to watch
 * for another one claiming its players, and overlapping areas resolve the same way every tick instead
 * of depending on which zone happened to tick last.
 *
 * Minigames are also the only zones that get the hooks below dispatched to them, passive quad zones
 * stay out of those loops entirely.
 */
class IMinigame : public CQuadZone
{
	friend class CZoneManager;

	std::vector<size_t> m_vAreaQuadIndices;
	std::vector<int> m_vSnapIds;

	/*
	 * Who this minigame currently owns. Kept here rather than as a pointer on CPlayer so it cannot
	 * outlive the zone: when the map unloads the membership goes with it, nothing to clean up.
	 * Only CZoneManager writes these, it owns the decision.
	 */
	bool m_aInArea[MAX_CLIENTS] = {false};
	bool m_aSeenMotd[MAX_CLIENTS] = {false};

	void SetInArea(int ClientId, bool InArea) { m_aInArea[ClientId] = InArea; }
	void SendMotd(int ClientId);
	// Client ids get reused, a reconnecting player has not seen anything
	void ResetClientMotd(int ClientId) { m_aSeenMotd[ClientId] = false; }

protected:
	/*
	 * Registers a quad and marks it as part of the play area. Use this instead of AddQuad for the layer
	 * that defines where the minigame begins, ContainsPlayer() tests against exactly these quads.
	 */
	void AddAreaQuad(const CQuadData &QuadData);

	/*
	 * Reserves a snap id for OnSnap(), freed again when the zone goes away. This is what lets a minigame
	 * draw its own props without an entity: an entity is only worth it for something that has to live in
	 * the world, move, collide or be predicted.
	 * Returns -1 when the server is out of ids, check before snapping.
	 */
	int AllocSnapId();

public:
	IMinigame(CGameContext *pGameContext, size_t MapIndex) :
		CQuadZone(pGameContext, MapIndex) {}
	~IMinigame() override;

	/*
	 * True while this minigame owns the player. This is the membership test every hook should use.
	 */
	// Not CheckClientId: gamecontext.h includes us back through the zone manager
	[[nodiscard]] bool IsInArea(int ClientId) const { return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aInArea[ClientId]; }

	/*
	 * Decides whether the player belongs to this minigame this tick. The default is "standing inside one
	 * of the area quads", and players without a living character keep whatever they had, so dying in an
	 * area does not hand the player back to the normal game on its own. Override to add conditions.
	 */
	[[nodiscard]] virtual bool ContainsPlayer(const CPlayer *pPlayer) const;

	/*
	 * Called by CZoneManager when ownership of a player changes. On enter the player is already ours, on
	 * leave they no longer are. Both may run more than once for the same client, keep them idempotent.
	 */
	virtual void OnPlayerEnter(int ClientId) {}
	virtual void OnPlayerLeave(int ClientId) {}

	/*
	 * Shown once when a player enters the area, return "" for no motd
	 */
	[[nodiscard]] virtual const char *Motd() const { return ""; }

	/*
	 * Blanks the race finish time players of this minigame see on each other, for minigames that
	 * repurpose the scoreboard
	 */
	[[nodiscard]] virtual bool HidesFinishTime() const { return false; }

	/*
	 * Snaps whatever the minigame draws itself, called once per snapping client on this map
	 */
	virtual void OnSnap(int SnappingClient) {}

	virtual void OnClientDrop(int ClientId, const char *pReason) {}

	/*
	 * The client is leaving for good, or at least leaving this map: log out, map change, disconnect.
	 * Settle anything they have staked here, they are not coming back to collect it.
	 * Unlike the hooks above this reaches every minigame on the map, not just the one that owns them,
	 * because a stake outlives standing in the area.
	 */
	virtual void OnClientReset(int ClientId) {}

	/*
	 * Return false to stop the player from spending money elsewhere, e.g. while it is on the table
	 */
	virtual bool CanUseMoney(CPlayer *pPlayer) { return true; }

	/*
	 * Whether /bet means anything here. Standing in some area is not enough, the area has to be one
	 * that actually takes a wager.
	 */
	[[nodiscard]] virtual bool TakesWager() const { return false; }

	virtual void OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx) {}

	virtual int ShowOthers(CPlayer *pPlayer) { return -1; }
	virtual bool CanUseCommand(CPlayer *pPlayer, const char *pCommand) { return true; }
	virtual bool CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget) { return true; }
	virtual bool CanSnapCharacter(CCharacter *pChr, int SnappingClient) { return true; }
	virtual bool CanDropWeapon(CCharacter *pChr, int Weapon) { return true; }
	virtual void OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg) {}
	virtual void OnCharacterSpawn(int ClientId, vec2 Pos) {}
	virtual bool OnCharacterFire(CCharacter *pChr, int Weapon) { return true; }
	virtual void OnCharacterHammerHit(int ClientId, int Target) {}
	virtual bool SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags) { return true; }

	virtual void OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo &ClientInfo, int *pTeam, int *pLatency, int *pScore) {}
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_MINIGAME_H
