#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H

#include "zone.h"

#include <base/vmath.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/quad_data.h>
#include <game/server/foxnet/component.h>
#include <engine/console.h>

#include <utility>
#include <vector>

class CGameContext;
class CQuad;
class CMapItemLayerQuads;

class CZoneManager : public CServerComponent
{
	std::vector<IZone *> m_avpZones[(int)EZoneType::Num];

	std::vector<int> m_vIds;

	void SnapQuadIds();
	void FreeQuadIds();

	bool m_DebugSnappingQuads = false;
	static void ConDebugSnapQuads(IConsole::IResult *pResult, void *pUserData);
	static void ConNumQuads(IConsole::IResult *pResult, void *pUserData);

public:
	IZone *FindZoneByMapIndex(EZoneType Type, size_t MultiMapIdx);

	std::vector<IZone *> Zones(EZoneType Type) const { return m_avpZones[(int)Type]; }

	void OnMapLoad(size_t MapIdx) override;
	void OnMapUnload(size_t MapIdx) override;
	void OnTick() override;

	void OnSnap(int SnappingClient, bool GlobalSnap, bool RecordingDemo) override;
	void OnConsoleInit() override;

	void OnClientDrop(int ClientId, const char *pReason) override;
	void OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx) override;

	int ShowOthers(CPlayer *pPlayer) override;
	bool CanUseCommand(CPlayer *pPlayer, const char *pCommand) override;
	bool CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget) override;
	bool CanSnapCharacter(CCharacter *pChr, int SnappingClient) override;
	bool CanDropWeapon(CCharacter *pChr, int Weapon) override;
	void OnCharacterSpawn(int ClientId, vec2 Pos) override;
	void OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg) override;
	bool OnCharacterFire(int ClientId, int Weapon) override;
	void OnCharacterHammerHit(int ClientId, int Target) override;
	bool SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags) override;

	void OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo &ClientInfo, int *pTeam, int *pLatency, int *pScore) override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H
