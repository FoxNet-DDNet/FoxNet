#include "persistent_data.h"

#include "game/server/player.h"
#include <game/server/foxnet/accounts.h>

void CSavePlayerData::Save(CPlayer *pPl)
{
	m_Configs = pPl->Acc()->m_Configs;

	m_Invisible = pPl->m_Invisible;
	m_Vanish = pPl->m_Vanish;
	m_IgnoreGamelayer = pPl->m_IgnoreGamelayer;
	m_TelekinesisImmunity = pPl->m_TelekinesisImmunity;
	m_IncludeServerInfo = pPl->m_IncludeServerInfo;

	m_Obfuscated = pPl->m_Obfuscated;
	m_SpiderHook = pPl->m_SpiderHook;
	m_Spazzing = pPl->m_Spazzing;

	m_AccLoginAttempts = pPl->m_AccLoginAttempts;
	m_AccRegisters = pPl->m_AccRegisters;

	m_Page = pPl->GetPage();
	m_SubPage = pPl->GetSubPage();
}

bool CSavePlayerData::Load(CPlayer *pPl) const
{
	pPl->Acc()->m_Configs = m_Configs;

	pPl->m_Invisible = m_Invisible;
	pPl->m_Vanish = m_Vanish;
	pPl->m_IgnoreGamelayer = m_IgnoreGamelayer;
	pPl->m_TelekinesisImmunity = m_TelekinesisImmunity;
	pPl->m_IncludeServerInfo = m_IncludeServerInfo;

	pPl->m_Obfuscated = m_Obfuscated;
	pPl->m_SpiderHook = m_SpiderHook;
	pPl->m_Spazzing = m_Spazzing;

	pPl->m_AccLoginAttempts = m_AccLoginAttempts;
	pPl->m_AccRegisters = m_AccRegisters;

	pPl->SetPage(m_Page);
	pPl->SetSubPage(m_SubPage);
	return true;
}
