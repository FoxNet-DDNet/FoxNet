#include "persistent_data.h"

#include "game/server/player.h"

void CSavePlayerData::Save(CPlayer *pPl)
{
	m_HideCosmetics = pPl->m_HideCosmetics;
	m_HidePowerUps = pPl->m_HidePowerUps;
}

bool CSavePlayerData::Load(CPlayer *pPl)
{
	pPl->m_HideCosmetics = m_HideCosmetics;
	pPl->m_HidePowerUps = m_HidePowerUps;
	return true;
}
