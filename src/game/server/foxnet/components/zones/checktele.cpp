#include "checktele.h"

#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

#include <vector>

void CCheckpointFromZone::OnTick()
{
	if(!GameServer()->GlobalTuning(MultiMapIndex())->m_MovingTiles)
		return;

	std::vector<CEntity *> apEnts = GameServer()->m_World.EntitiesOfType(CGameWorld::ENTTYPE_PICKUPDROP);

	for(const CQuadData &QuadData : Quads())
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
			if(!pPlayer || !pPlayer->GetCharacter())
				continue;
			if(pPlayer->MultiMapIdx() != (int)MultiMapIndex())
				continue;
			CCharacter *pChr = pPlayer->GetCharacter();
			if(!pChr->IsAlive())
				continue;

			if(!InsideQuad(pChr->GetPos(), QuadData))
				continue;

			HandleTeleport(pChr);
		}
		for(CEntity *pEnt : apEnts)
		{
			if(pEnt->MultiMapIdx() != (int)MultiMapIndex())
				continue;

			if(!InsideQuad(pEnt->GetPos(), QuadData))
				continue;

			HandleTeleport(pEnt);
		}
	}
}

void CCheckpointFromZone::HandleTeleport(CEntity *pEnt)
{
	// Teleport to last tele checkpoint out, or spawn if none
	if(pEnt->ObjectType() == CGameWorld::ENTTYPE_CHARACTER)
	{
		CCharacter *pChr = static_cast<CCharacter *>(pEnt);
		if(pChr->Core()->m_Super || pChr->Core()->m_Invincible)
			return; // First try TeleCheckOuts from current to older checkpoints

		bool Teleported = false;
		for(int k = pChr->m_TeleCheckpoint - 1; k >= 0; --k)
		{
			const auto &outs = Collision()->TeleCheckOuts(k);
			if(!outs.empty())
			{
				const int idx = pChr->GameWorld()->m_Core.RandomOr0(outs.size());
				pChr->SetPosition(outs[idx]);
				pChr->ResetVelocity();
				if(!g_Config.m_SvTeleportHoldHook)
				{
					pChr->ResetHook();
					pChr->GameWorld()->ReleaseHooked(pChr->GetPlayer()->GetCid());
				}
				Teleported = true;
				break;
			}
		}
		// If none found, teleport to spawn
		if(!Teleported)
		{
			vec2 SpawnPos;
			if(GameServer()->m_pController->CanSpawn(pChr->GetPlayer()->GetTeam(), &SpawnPos, GameServer()->GetDDRaceTeam(pChr->GetPlayer()->GetCid())))
			{
				pChr->SetPosition(SpawnPos);
				pChr->ResetVelocity();
				if(!g_Config.m_SvTeleportHoldHook)
				{
					pChr->ResetHook();
					pChr->GameWorld()->ReleaseHooked(pChr->GetPlayer()->GetCid());
				}
			}
		}
	}
	else if(pEnt->ObjectType() == CGameWorld::ENTTYPE_PICKUPDROP)
	{
		CPickupDrop *pPup = static_cast<CPickupDrop *>(pEnt);

		bool Teleported = false;
		for(int k = pPup->TeleCheckpoint() - 1; k >= 0; --k)
		{
			const auto &outs = Collision()->TeleCheckOuts(k);
			if(!outs.empty())
			{
				const int idx = pPup->GameWorld()->m_Core.RandomOr0(outs.size());
				pPup->ForceSetPos(outs[idx]);
				pPup->SetRawVelocity(vec2(0, 0));
				Teleported = true;
				break;
			}
		}
		// If none found, teleport to spawn
		if(!Teleported)
		{
			vec2 SpawnPos;
			if(GameServer()->m_pController->CanSpawn(0, &SpawnPos, pPup->Team()))
			{
				pPup->ForceSetPos(SpawnPos);
				pPup->SetRawVelocity(vec2(0, 0));
			}
		}
	}
}
