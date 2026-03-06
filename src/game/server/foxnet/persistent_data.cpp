#include "persistent_data.h"

#include "game/server/player.h"
#include <game/server/foxnet/components/accounts/accounts.h>

void CSavePlayerData::Save(CPlayer *pPlayer)
{
	m_Configs = pPlayer->Acc()->m_Configs;

	m_Invisible = pPlayer->m_Invisible;
	m_Vanish = pPlayer->m_Vanish;
	m_IgnoreGamelayer = pPlayer->m_IgnoreGamelayer;
	m_TelekinesisImmunity = pPlayer->m_TelekinesisImmunity;
	m_IncludeServerInfo = pPlayer->m_IncludeServerInfo;

	m_Obfuscated = pPlayer->m_Obfuscated;
	m_SpiderHook = pPlayer->m_SpiderHook;
	m_Spazzing = pPlayer->m_Spazzing;

	m_AccLoginAttempts = pPlayer->m_AccLoginAttempts;
	m_AccRegisters = pPlayer->m_AccRegisters;

	m_Page = pPlayer->GetPage();
	m_SubPage = pPlayer->GetSubPage();
}

bool CSavePlayerData::Load(CPlayer *pPlayer) const
{
	pPlayer->Acc()->m_Configs = m_Configs;

	pPlayer->m_Invisible = m_Invisible;
	pPlayer->m_Vanish = m_Vanish;
	pPlayer->m_IgnoreGamelayer = m_IgnoreGamelayer;
	pPlayer->m_TelekinesisImmunity = m_TelekinesisImmunity;
	pPlayer->m_IncludeServerInfo = m_IncludeServerInfo;

	pPlayer->m_Obfuscated = m_Obfuscated;
	pPlayer->m_SpiderHook = m_SpiderHook;
	pPlayer->m_Spazzing = m_Spazzing;

	pPlayer->m_AccLoginAttempts = m_AccLoginAttempts;
	pPlayer->m_AccRegisters = m_AccRegisters;

	pPlayer->SetPage(m_Page);
	pPlayer->SetSubPage(m_SubPage);
	return true;
}
