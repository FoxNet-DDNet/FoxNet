#include "persistent_data.h"

#include <base/str.h>

#include <game/server/player.h>

void CSavePlayerData::Save(CPlayer *pPlayer)
{
	m_Configs = pPlayer->Acc()->m_Configs;
	if(pPlayer->Acc()->m_LoggedIn)
		str_copy(m_aUsername, pPlayer->Acc()->m_aUsername, sizeof(m_aUsername));
	else
		m_aUsername[0] = '\0';

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

	m_SupportsCosmeticSnaps = pPlayer->m_SupportsCosmeticSnaps;
}

bool CSavePlayerData::Load(CPlayer *pPlayer) const
{
	pPlayer->Acc()->m_Configs = m_Configs;
	if(m_aUsername[0] != '\0')
		str_copy(pPlayer->Acc()->m_aUsername, m_aUsername, sizeof(pPlayer->Acc()->m_aUsername));
	else
		pPlayer->Acc()->m_aUsername[0] = '\0';

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

	pPlayer->m_SupportsCosmeticSnaps = m_SupportsCosmeticSnaps;
	return true;
}
