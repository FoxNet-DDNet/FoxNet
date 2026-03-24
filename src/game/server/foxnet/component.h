#ifndef GAME_SERVER_FOXNET_COMPONENTS_COMPONENT_H
#define GAME_SERVER_FOXNET_COMPONENTS_COMPONENT_H

#include <base/system.h>

#include <vector>

class CGameContext;
class IServer;
class IConsole;	
class CPlayer;
class CCharacter;
class CAccountSession;
class CMultiMaps;

class CServerComponent
{
	CGameContext *m_pGameServer;

public:
	virtual void InitComponent(CGameContext *pGameServer);

	virtual void SendChatTarget(int ClientId, const char *pMessage);
	
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;
	IConsole *Console() const;

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

	virtual void OnConsoleInit() {}
	virtual void OnInit() {}
	/*
	 * Called before void CGameContext::Clear()
	 */
	virtual void OnPreReset() {}
	/*
	* Called before ~CGameContext() in CGameContext::Clear()
	*/
	virtual void OnReset() {}
	virtual void OnShutdown(void *pPersistentData) {}
	virtual void OnTick() {}
	virtual void OnSnap(int ClientId, bool GlobalSnap, bool RecordingDemo) {}
	virtual void OnPostGlobalSnap() {}
	virtual void OnClientEnter(int ClientId) {}
	virtual void OnClientDrop(int ClientId, const char *pReason) {}
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_COMPONENT_H