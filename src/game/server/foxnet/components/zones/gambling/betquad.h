#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_BETQUAD_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_BETQUAD_H

#include <base/vmath.h>

/*
 * A "Bet_<name>" quad from the map: the patch of table a player puts their cursor on and hammers to
 * pick that option. m_BetOption indexes whatever option list the owning game declares.
 */
class CBetQuadData
{
public:
	int m_MapIndex = 0;
	vec2 m_Pos[4] = {vec2(0, 0)};
	// The quad's pivot, for games that want to draw the option's icon on the patch itself
	vec2 m_Pivot = vec2(0, 0);
	int m_SnapId = -1;
	int m_BetOption = -1;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_BETQUAD_H
