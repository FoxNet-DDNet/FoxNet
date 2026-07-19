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
}

#endif
