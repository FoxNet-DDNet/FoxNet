#include "persistent_data.h"

#include "game/server/player.h"

void CSavePlayerData::Save(CPlayer *pPl)
{
	m_HideCosmetics = pPl->m_HideCosmetics;
	m_HidePowerUps = pPl->m_HidePowerUps;

	m_Invisible = pPl->m_Invisible;
	m_Vanish = pPl->m_Vanish;
	m_IgnoreGamelayer = pPl->m_IgnoreGamelayer;
	m_TelekinesisImmunity = pPl->m_TelekinesisImmunity;
	m_IncludeServerInfo = pPl->m_IncludeServerInfo;

	m_Obfuscated = pPl->m_Obfuscated;
	m_SpiderHook = pPl->m_SpiderHook;
	m_Spazzing = pPl->m_Spazzing;

	m_AccLoginAttemps = pPl->m_AccLoginAttemps;
	m_AccRegisters = pPl->m_AccRegisters;

	m_Page = pPl->GetPage();
	m_SubPage = pPl->GetSubPage();
}

bool CSavePlayerData::Load(CPlayer *pPl)
{
	pPl->m_HideCosmetics = m_HideCosmetics;
	pPl->m_HidePowerUps = m_HidePowerUps;

	pPl->m_Invisible = m_Invisible;
	pPl->m_Vanish = m_Vanish;
	pPl->m_IgnoreGamelayer = m_IgnoreGamelayer;
	pPl->m_TelekinesisImmunity = m_TelekinesisImmunity;
	pPl->m_IncludeServerInfo = m_IncludeServerInfo;

	pPl->m_Obfuscated = m_Obfuscated;
	pPl->m_SpiderHook = m_SpiderHook;
	pPl->m_Spazzing = m_Spazzing;

	pPl->m_AccLoginAttemps = m_AccLoginAttemps;
	pPl->m_AccRegisters = m_AccRegisters;

	pPl->SetPage(m_Page);
	pPl->SetSubPage(m_SubPage);
	return true;
}
