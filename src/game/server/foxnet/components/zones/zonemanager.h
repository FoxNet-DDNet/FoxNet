#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H

#include "zone.h"

#include <base/vmath.h>

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
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H
