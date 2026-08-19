#ifndef GAME_SERVER_FOXNET_INTEGRATION_ANTIBOB_H
#define GAME_SERVER_FOXNET_INTEGRATION_ANTIBOB_H

#include <base/dynamic.h>

class IConsole;
class CGameContext;

struct CAntibobTileData
{
	unsigned char m_Game;
	unsigned char m_GameFlags;
	unsigned char m_Front;
	unsigned char m_FrontFlags;
	unsigned char m_Switch;
	unsigned char m_SwitchFlags;
	unsigned char m_SwitchNumber;
	unsigned char m_SwitchDelay;
	unsigned char m_SwitchActive;
	unsigned char m_Tele;
	unsigned char m_TeleNumber;
};

enum
{
	ANTIBOB_PLAYERFLAG_PRACTICE = 1 << 0,
	ANTIBOB_PLAYERFLAG_FROZEN = 1 << 1,
	// Server side debug dummy (add_dummies). Inputs are generated locally and there
	// is no real client behind it, so it is exempt from every check and punishment.
	ANTIBOB_PLAYERFLAG_DUMMY = 1 << 2,
};

class CAntibobContext
{
public:
	IConsole *m_pConsole = nullptr;
	CGameContext *m_pGameServer = nullptr;
};

extern CAntibobContext g_AntibobContext;

#ifndef ANTIBOBAPI
#define ANTIBOBAPI DYNAMIC_EXPORT
#endif

extern "C" {
ANTIBOBAPI int AntibobVersion();
ANTIBOBAPI void AntibobRcon(const char *pLine);
ANTIBOBAPI bool AntibobMapSize(int ClientId, int *pWidth, int *pHeight);
ANTIBOBAPI bool AntibobTile(int ClientId, int TileX, int TileY, CAntibobTileData *pData);
ANTIBOBAPI unsigned int AntibobPlayerFlags(int ClientId);
// Not covered by AntibobVersion on purpose. The version check is an equality test, so
// bumping it would make an older module lose tiles and player flags entirely rather than
// just this. These resolve to null on an older server and the port is simply omitted.
ANTIBOBAPI int AntibobServerPort();
ANTIBOBAPI void AntibobServerInstance(char *pBuf, int BufSize);
}

#endif
