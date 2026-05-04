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

	bool m_Invisible = false;
	bool m_Vanish = false;
	bool m_IgnoreGamelayer = false;
	bool m_TelekinesisImmunity = false;
	bool m_IncludeServerInfo = true;

	bool m_Obfuscated = false;
	bool m_SpiderHook = false;
	bool m_Spazzing = false;

	int m_AccLoginAttempts = 0;
	int m_AccRegisters = 0;

	int m_Page = 0;
	int m_SubPage = 0;
	bool m_SupportsCosmeticSnaps = false;
};

#endif // GAME_SERVER_FOXNET_PERSISTENT_DATA_H
