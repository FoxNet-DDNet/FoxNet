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

	bool m_Invisible;
	bool m_Vanish;
	bool m_IgnoreGamelayer;
	bool m_TelekinesisImmunity;
	int m_IncludeServerInfo;

	bool m_Obfuscated;
	bool m_SpiderHook;
	bool m_Spazzing;

	int m_AccLoginAttemps;
	int m_AccRegisters;

	int m_Page;
	int m_SubPage;
};

#endif // GAME_SERVER_FOXNET_PERSISTENT_DATA_H