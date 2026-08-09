#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_MINIGAME_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_MINIGAME_H

#include "zone.h"

#include <base/vmath.h>

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
	std::vector<size_t> m_vAreaQuadIndices;

protected:
	/*
	 * Registers a quad and marks it as part of the play area. Use this instead of AddQuad for the layer
	 * that defines where the minigame begins, ContainsPlayer() tests against exactly these quads.
	 */
	void AddAreaQuad(const CQuadData &QuadData);

public:
	IMinigame(CGameContext *pGameContext, size_t MapIndex) :
		CQuadZone(pGameContext, MapIndex) {}
	~IMinigame() override;

	/*
	 * True while this minigame owns the player. This is the membership test every hook should use.
	 */
	[[nodiscard]] bool IsInArea(int ClientId) const;

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

	virtual void OnClientDrop(int ClientId, const char *pReason) {}

	virtual void OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx) {}

	virtual int ShowOthers(CPlayer *pPlayer) { return -1; }
	virtual bool CanUseCommand(CPlayer *pPlayer, const char *pCommand) { return true; }
	virtual bool CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget) { return true; }
	virtual bool CanSnapCharacter(CCharacter *pChr, int SnappingClient) { return true; }
	virtual bool CanDropWeapon(CCharacter *pChr, int Weapon) { return true; }
	virtual void OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg) {}
	virtual void OnCharacterSpawn(int ClientId, vec2 Pos) {}
	virtual bool OnCharacterFire(int ClientId, int Weapon) { return true; }
	virtual void OnCharacterHammerHit(int ClientId, int Target) {}
	virtual bool SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags) { return true; }

	virtual void OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo &ClientInfo, int *pTeam, int *pLatency, int *pScore) {}
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_MINIGAME_H
