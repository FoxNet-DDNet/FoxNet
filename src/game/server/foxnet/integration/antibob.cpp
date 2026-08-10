#include "antibob.h"

#include <base/log.h>

#include <engine/console.h>

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/teams.h>

CAntibobContext g_AntibobContext;
extern "C" {

int AntibobVersion()
{
	// 12.00
	return 1200;
}

void AntibobRcon(const char *pLine)
{
	if(!g_AntibobContext.m_pConsole)
	{
		log_error("antibob", "console not initialized yet");
		return;
	}
	g_AntibobContext.m_pConsole->ExecuteLine(pLine, IConsole::CLIENT_ID_FOXNET);
}

static int AntibobMultiMapIndex(CGameContext *pGameServer, int ClientId)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && pGameServer->m_apPlayers[ClientId])
		return pGameServer->m_apPlayers[ClientId]->MultiMapIdx();
	return DefaultMapIndex;
}

static CCollision *AntibobCollision(CGameContext *pGameServer, int ClientId)
{
	CCharacter *pCharacter = ClientId >= 0 && ClientId < MAX_CLIENTS ? pGameServer->GetPlayerChar(ClientId) : nullptr;
	return pCharacter ? pCharacter->Collision() : pGameServer->Collision(AntibobMultiMapIndex(pGameServer, ClientId));
}

bool AntibobMapSize(int ClientId, int *pWidth, int *pHeight)
{
	if(!g_AntibobContext.m_pGameServer || !pWidth || !pHeight)
		return false;
	CGameContext *pGameServer = g_AntibobContext.m_pGameServer;
	const CCollision *pCollision = AntibobCollision(pGameServer, ClientId);
	*pWidth = pCollision->GetWidth();
	*pHeight = pCollision->GetHeight();
	return true;
}

bool AntibobTile(int ClientId, int TileX, int TileY, CAntibobTileData *pData)
{
	if(!pData)
		return false;
	*pData = {};
	if(!g_AntibobContext.m_pGameServer)
		return false;
	CGameContext *pGameServer = g_AntibobContext.m_pGameServer;
	const int MultiMapIndex = AntibobMultiMapIndex(pGameServer, ClientId);
	const int Team = ClientId >= 0 && ClientId < MAX_CLIENTS && pGameServer->m_apPlayers[ClientId] ? pGameServer->GetDDRaceTeam(ClientId) : 0;
	const CCollision *pCollision = AntibobCollision(pGameServer, ClientId);
	if(TileX < 0 || TileY < 0 || TileX >= pCollision->GetWidth() || TileY >= pCollision->GetHeight())
		return false;

	const int Index = pCollision->GetPureMapIndex(TileX * 32, TileY * 32);
	const CTile *pFront = pCollision->FrontLayer();
	const CSwitchTile *pSwitch = pCollision->SwitchLayer();
	const CTeleTile *pTele = pCollision->TeleLayer();
	pData->m_Game = pCollision->GetTileIndex(Index);
	pData->m_GameFlags = pCollision->GetTileFlags(Index);
	if(pFront)
	{
		pData->m_Front = pCollision->GetFrontTileIndex(Index);
		pData->m_FrontFlags = pCollision->GetFrontTileFlags(Index);
	}
	if(pSwitch)
	{
		pData->m_Switch = pSwitch[Index].m_Type;
		pData->m_SwitchFlags = pSwitch[Index].m_Flags;
		pData->m_SwitchNumber = pSwitch[Index].m_Number;
		pData->m_SwitchDelay = pSwitch[Index].m_Delay;
		pData->m_SwitchActive = pSwitch[Index].m_Number == 0;
		const auto &vMaps = pGameServer->Switchers();
		if(MultiMapIndex >= 0 && MultiMapIndex < (int)vMaps.size() && pSwitch[Index].m_Number > 0 && pSwitch[Index].m_Number < vMaps[MultiMapIndex].size() && Team >= 0 && Team < MAX_CLIENTS)
			pData->m_SwitchActive = vMaps[MultiMapIndex][pSwitch[Index].m_Number].m_aStatus[Team];
	}
	if(pTele)
	{
		pData->m_Tele = pTele[Index].m_Type;
		pData->m_TeleNumber = pTele[Index].m_Number;
	}
	return true;
}

unsigned int AntibobPlayerFlags(int ClientId)
{
	if(!g_AntibobContext.m_pGameServer || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return 0;
	unsigned int Flags = 0;
	// Checked before the character, a debug dummy must stay exempt even while it has
	// no character at all (joining, dead, dropping).
	if(g_AntibobContext.m_pGameServer->Server()->DebugDummy(ClientId))
		Flags |= ANTIBOB_PLAYERFLAG_DUMMY;

	CCharacter *pCharacter = g_AntibobContext.m_pGameServer->GetPlayerChar(ClientId);
	if(!pCharacter)
		return Flags;
	if(pCharacter->Teams()->IsPractice(pCharacter->Team()))
		Flags |= ANTIBOB_PLAYERFLAG_PRACTICE;
	if(pCharacter->Core()->m_DeepFrozen || pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_LiveFrozen)
		Flags |= ANTIBOB_PLAYERFLAG_FROZEN;

	// Other things that override movement in some way
	if(pCharacter->m_Ufo.Active() || pCharacter->m_InSnake)
		Flags |= ANTIBOB_PLAYERFLAG_FROZEN;
	return Flags;
}
}
