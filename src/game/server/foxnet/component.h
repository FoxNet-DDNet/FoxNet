#ifndef GAME_SERVER_FOXNET_COMPONENT_H
#define GAME_SERVER_FOXNET_COMPONENT_H

#include <base/system.h>

#include <generated/protocol.h>

#include <vector>

class CGameContext;
class IServer;
class IConsole;
class CCollision;
class CPlayer;
class CCharacter;
class CAccountSession;
class CMultiMaps;

class CServerComponent
{
	CGameContext *m_pGameServer;
	IServer *m_pServer;

public:
	virtual ~CServerComponent() = default;
	virtual void InitComponent(CGameContext *pGameServer);

	virtual void SendChatTarget(int ClientId, const char *pMessage);

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return m_pServer; }
	IConsole *Console() const;
	CCollision *Collision(size_t MultiMapIdx) const;

	CPlayer *GetPlayer(int ClientId);
	CCharacter *GetCharacter(int ClientId);
	CAccountSession *GetAcc(int ClientId) const;

	CMultiMaps *MultiMaps(size_t Idx) const;

	/*
	 * Called when a map gets loaded, MapIdx is the index of the map in the multi map array, starting with 0
	 */
	virtual void OnMapLoad(size_t MapIdx) {}
	/*
	 * Called when a map gets unloaded, MapIdx is the index of the map in the multi map array, starting with 0
	 */
	virtual void OnMapUnload(size_t MapIdx) {}

	/*
	* Called when the console initializes
	*/
	virtual void OnConsoleInit() {}
	/*
	 * Called after When the main map gets loaded
	 */
	virtual void OnInit() {}
	/*
	 * Called when the server is shutting down, pPersistentData is a pointer to a struct that can be used to store data that should persist through the shutdown and the next init, for example for map rotation in map voting
	 */
	virtual void OnShutdown(void *pPersistentData) {}
	/*
	 * Called every tick
	 */
	virtual void OnTick() {}

	/*
	 * After the world has ticked, so after every character has taken its movement for this tick.
	 * Anything that pushes an entity out of somewhere belongs here rather than in OnTick: run
	 * before the movement it is answering for a position the entity has already left, and
	 * whatever moved it in -- a hook, most of all -- gets the last word every single tick.
	 */
	virtual void OnPostTick() {}
	/*
	 * Called every snap
	 */
	virtual void OnSnap(int ClientId, bool GlobalSnap, bool RecordingDemo) {}
	/*
	 * Used to override Game Info Snapdata
	 */
	virtual void OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx) {}
	virtual void OnPostGlobalSnap() {}
	/*
	 * Called when a client connects
	 */
	virtual void OnClientEnter(int ClientId) {}
	/*
	 * Called when a client disconnects
	 */
	virtual void OnClientDrop(int ClientId, const char *pReason) {}
	/*
	 * Called when CPlayer::m_ShowOthers is accesses anywhere
	 * retuning -1 will just return m_ShowOthers
	 * returning anything else overrides m_ShowOthers for the player
	 */
	virtual int ShowOthers(CPlayer *pPlayer) { return -1; }
	/*
	 * Called when a client tries to execute a chat command, return false to prevent the command from being executed
	 */
	virtual bool CanUseCommand(CPlayer *pPlayer, const char *pCommand) { return true; }
	// Minigame specific hooks
	/*
	 * Called when a player tries to spectate someone
	 * 
	 * @param pPlayer the player that tries to spectate
	 * @param pTarget the wanted spectate target
	 */
	virtual bool CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget) { return true; }
	/*
	 * Called when a character is snapped, return false to prevent it from being snapped for the client
	 * 
	 * @param pChr the character that is being snapped
	 * @param SnappingClient the client that is snapping this character
	 */
	virtual bool CanSnapCharacter(CCharacter *pChr, int SnappingClient) { return true; }
	/*
	 * Called when a player tries to drop a weapon, return false to prevent dropping the weapon
	 */
	virtual bool CanDropWeapon(CCharacter *pChr, int Weapon) { return true; }
	/*
	 * Called when a character spawns, Pos is the spawn position
	 */
	virtual void OnCharacterSpawn(int ClientId, vec2 Pos) {}
	/*
	 * Called when a character dies
	*/
	virtual void OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg) {}
	/*
	 * Called when a character fires a weapon, return false to prevent firing the weapon
	 */
	virtual bool OnCharacterFire(CCharacter *pChr, int Weapon) { return true; }
	/*
	 * Called when a character hits someone with the hammer
	 */
	virtual void OnCharacterHammerHit(int ClientId, int Target) {}
	/*
	 * Called when a Mask is being set for a client, return false to prevent the mask from being set
	 */
	virtual bool SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags) { return true; }
	/*
	 * Used to override player snap data
	 */
	virtual void OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo &ClientInfo, int *pTeam, int *pLatency, int *pScore) {}
};

#endif // GAME_SERVER_FOXNET_COMPONENT_H
