#ifndef GAME_SERVER_FOXNET_PERSISTENT_DATA_H
#define GAME_SERVER_FOXNET_PERSISTENT_DATA_H

#include <base/system.h>

#include <vector>

class CPlayer;

class CSavePlayerData
{
public:
	CSavePlayerData() = default;
	~CSavePlayerData() = default;
	void Save(CPlayer *pPl);
	bool Load(CPlayer *pPl);

private:
	bool m_HideCosmetics;
	bool m_HidePowerUps;
};

#endif // GAME_SERVER_FOXNET_PERSISTENT_DATA_H