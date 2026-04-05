#ifndef GAME_SERVER_FOXNET_PERSISTENT_DATA_H
#define GAME_SERVER_FOXNET_PERSISTENT_DATA_H

#include <base/system.h>

#include <game/server/foxnet/components/accounts/accounts.h>

#include <vector>

class CPlayer;

class CSavePlayerData
{
public:
	CSavePlayerData() = default;
	~CSavePlayerData() = default;
	void Save(CPlayer *pPlayer);
	bool Load(CPlayer *pPlayer) const;

private:
	CAccConfigs m_Configs;
	char m_aUsername[ACC_MAX_USERNAME_LENGTH] = "";

	bool m_Invisible;
	bool m_Vanish;
	bool m_IgnoreGamelayer;
	bool m_TelekinesisImmunity;
	bool m_IncludeServerInfo;

	bool m_Obfuscated;
	bool m_SpiderHook;
	bool m_Spazzing;

	int m_AccLoginAttempts;
	int m_AccRegisters;

	int m_Page;
	int m_SubPage;

	bool m_SupportsCosmeticSnaps;
};

#endif // GAME_SERVER_FOXNET_PERSISTENT_DATA_H
