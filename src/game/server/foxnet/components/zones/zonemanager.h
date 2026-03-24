#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H

#include "zone.h"

#include <base/vmath.h>

#include <game/collision.h>
#include <game/quad_data.h>
#include <game/server/foxnet/component.h>

#include <utility>
#include <vector>

class CGameContext;
class CQuad;
class CMapItemLayerQuads;

class CZoneManager : public CServerComponent
{
	std::vector<IZone *> m_avpZones[(int)EZoneType::Num];

public:
	const std::vector<IZone *> Zones(EZoneType Type) const { return m_avpZones[(int)Type]; }

	void OnMapLoad(size_t MapIdx) override;
	void OnMapUnload(size_t MapIdx) override;
	void OnTick() override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONEMANAGER_H
