#include "antibob.h"
#include "motion_peer.h"

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/teams.h>

#include <algorithm>
#include <cmath>

namespace
{
	CCharacter *Character(int ClientId)
	{
		return g_AntibobContext.m_pGameServer && ClientId >= 0 && ClientId < MAX_CLIENTS ?
			       g_AntibobContext.m_pGameServer->GetPlayerChar(ClientId) :
			       nullptr;
	}

	// This deliberately supports a small, well-defined physics subset. Never guess
	// through teleports, speedups, switches, tune transitions or custom map effects.
	bool PlainTile(int Tile)
	{
		return Tile == TILE_AIR || Tile == TILE_SOLID || Tile == TILE_NOHOOK || Tile == TILE_FREEZE || Tile == TILE_DEATH;
	}

	bool PlainArea(CCollision *pCollision, vec2 Pos, int TuneZone)
	{
		if(!std::isfinite(Pos.x) || !std::isfinite(Pos.y) || Pos.x < 32 || Pos.y < 32 ||
			Pos.x >= static_cast<float>(pCollision->GetWidth() - 1) * 32 || Pos.y >= static_cast<float>(pCollision->GetHeight() - 1) * 32)
			return false;
		const int X = round_to_int(Pos.x) / 32;
		const int Y = round_to_int(Pos.y) / 32;
		if(X < 1 || Y < 1 || X >= pCollision->GetWidth() - 1 || Y >= pCollision->GetHeight() - 1)
			return false;
		for(int Ty = Y - 1; Ty <= Y + 1; ++Ty)
			for(int Tx = X - 1; Tx <= X + 1; ++Tx)
			{
				const int Index = pCollision->GetPureMapIndex(Tx * 32, Ty * 32);
				if(!PlainTile(pCollision->GetTileIndex(Index)) || !PlainTile(pCollision->GetFrontTileIndex(Index)) ||
					(pCollision->SwitchLayer() && pCollision->SwitchLayer()[Index].m_Type) ||
					(pCollision->TeleLayer() && pCollision->TeleLayer()[Index].m_Type) ||
					pCollision->IsSpeedup(Index) || pCollision->IsTune(Index) != TuneZone)
					return false;
			}
		return true;
	}
}

extern "C" bool AntibobMotionStateV1(int ClientId, CAntibobMotionStateV1 *pState, int Size)
{
	if(!pState || Size != sizeof(*pState))
		return false;
	*pState = {};
	CCharacter *pCharacter = Character(ClientId);
	if(!pCharacter || !pCharacter->IsAlive() || pCharacter->IsPaused())
		return false;
	const CCharacterCore &Core = *pCharacter->Core();
	if(!std::isfinite(Core.m_Pos.x) || !std::isfinite(Core.m_Pos.y) || !std::isfinite(Core.m_Vel.x) || !std::isfinite(Core.m_Vel.y))
		return false;
	const auto &Input = *pCharacter->Input(); // After DDRaceTick, before Core.Tick.
	pState->m_Tick = g_AntibobContext.m_pGameServer->Server()->Tick();
	pState->m_SpawnTick = pCharacter->m_SpawnTick;
	pState->m_MapIndex = pCharacter->MultiMapIdx();
	pState->m_TuneZone = pCharacter->GetOverriddenTuneZone();
	pState->m_Input = {Input.m_Direction, Input.m_Jump, Input.m_Hook, Input.m_TargetX, Input.m_TargetY};
	pState->m_Pos[0] = Core.m_Pos.x;
	pState->m_Pos[1] = Core.m_Pos.y;
	pState->m_Vel[0] = Core.m_Vel.x;
	pState->m_Vel[1] = Core.m_Vel.y;
	pState->m_HookState = Core.m_HookState;
	pState->m_HookLaunched = (Core.m_TriggeredEvents & COREEVENT_HOOK_LAUNCH) != 0;
	pState->m_Jumped = Core.m_Jumped;
	pState->m_JumpedTotal = Core.m_JumpedTotal;
	pState->m_Jumps = Core.m_Jumps;
	pState->m_ActiveWeapon = Core.m_ActiveWeapon;
	if(AntibobPlayerFlags(ClientId) & ANTIBOB_PLAYERFLAG_FROZEN)
		pState->m_Flags |= ANTIBOB_MOTION_FROZEN;
	// These modes introduce forces or protections not represented by a core-only forecast.
	if(Core.m_Jetpack || Core.m_Super || Core.m_Invincible || Core.m_ActiveWeapon >= WEAPON_NINJA ||
		pCharacter->GetPlayer()->m_SpiderHook ||
		Core.HookedPlayer() >= 0 || !Core.m_AttachedPlayers.empty() || Core.m_HookedQuadId >= 0)
		pState->m_Flags |= ANTIBOB_MOTION_SPECIAL;
	const CTuningParams &Tuning = Core.m_FakeTuned ? Core.m_FakeTuning : Core.m_Tuning;
	pState->m_TuningHash = 2166136261u;
	for(int i = 0; i < CTuningParams::Num(); ++i)
	{
		pState->m_TuningHash = (pState->m_TuningHash ^ static_cast<unsigned int>(Tuning.NetworkArray()[i])) * 16777619u;
	}
	return true;
}

extern "C" bool AntibobMotionForecastV1(int ClientId, const CAntibobMotionInputV1 *pInput, CAntibobMotionForecastV1 *pForecast, int Size)
{
	if(!pForecast || Size != sizeof(*pForecast) || !pInput || pInput->m_Direction < -1 || pInput->m_Direction > 1 ||
		(pInput->m_TargetX == 0 && pInput->m_TargetY == 0))
		return false;
	*pForecast = {};
	CAntibobMotionStateV1 State;
	if(!AntibobMotionStateV1(ClientId, &State, sizeof(State)) || State.m_Flags)
		return false;
	CCharacter *pCharacter = Character(ClientId);
	CCharacterCore Core = *pCharacter->Core();
	CCollision *pCollision = pCharacter->Collision();
	// Bound work and exclude nearby tees rather than inventing their future inputs.
	if(!std::isfinite(Core.m_Vel.x) || !std::isfinite(Core.m_Vel.y) || length(Core.m_Vel) > 48.0f || !PlainArea(pCollision, Core.m_Pos, State.m_TuneZone))
		return false;
	CGameContext *pGameServer = g_AntibobContext.m_pGameServer;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CCharacter *pOther = pGameServer->GetPlayerChar(i);
		if(i == ClientId || !pOther || pOther->MultiMapIdx() != State.m_MapIndex || distance(pOther->Core()->m_Pos, Core.m_Pos) >= 768.0f)
			continue;
		// The forecasted tee must stay solo: PlainArea excludes solo-end tiles.
		// Require native solo and interaction checks as well as the core flag;
		// collision-disabled tiles alone do not prevent incoming hook/hammer.
		if(!AntibobMotionSoloPeer({Core.m_Solo, Core.m_Super, pCharacter->CanCollide(i)},
			   {pOther->Core()->m_Solo, pOther->Core()->m_Super, pOther->CanCollide(ClientId)},
			   pCharacter->Teams()->m_Core.GetSolo(ClientId)))
			return false;
	}
	// Reject a wide neighbourhood around current quad bounds. Actual movement is
	// also checked on every subsequent tick; animated geometry is never certified.
	for(const auto &Quad : pCollision->Quads())
		if(Core.m_Pos.x >= Quad.m_AabbMin.x - 768 && Core.m_Pos.x <= Quad.m_AabbMax.x + 768 &&
			Core.m_Pos.y >= Quad.m_AabbMin.y - 768 && Core.m_Pos.y <= Quad.m_AabbMax.y + 768)
			return false;
	for(int Type = 0; Type < static_cast<int>(EZoneType::Num); ++Type)
	{
		const CQuadZone *pZone = pGameServer->m_ZoneManager.FindZoneByMapIndex(static_cast<EZoneType>(Type), State.m_MapIndex);
		if(pZone)
			for(const auto &Quad : pZone->Quads())
				if(Quad.AabbIntersects(Core.m_Pos, vec2(768, 768)))
					return false;
	}
	if(pGameServer->m_ZoneManager.InMinigame(ClientId))
		return false;
	// Null world is essential: a copied core must never mutate live hooked peers,
	// consume server RNG, or invoke an antiping callback on a real character.
	Core.SetCoreWorld(nullptr, pCollision, nullptr);
	Core.SetAntiPingInterfereCallback([](int, bool) {});
	Core.m_Input.m_Direction = pInput->m_Direction;
	Core.m_Input.m_Jump = pInput->m_Jump;
	Core.m_Input.m_Hook = pInput->m_Hook;
	Core.m_Input.m_TargetX = pInput->m_TargetX;
	Core.m_Input.m_TargetY = pInput->m_TargetY;
	for(int Tick = 0; Tick < ANTIBOB_MOTION_TICKS; ++Tick)
	{
		// With a null world the core cannot perform hook teleports. Reject that
		// counterfactual too, even when the tee itself is nowhere near the tele.
		if(Core.m_Input.m_Hook && (Core.m_HookState == HOOK_IDLE || Core.m_HookState == HOOK_FLYING))
		{
			const vec2 Dir = Core.m_HookState == HOOK_IDLE ? normalize(vec2(Core.m_Input.m_TargetX, Core.m_Input.m_TargetY)) : Core.m_HookDir;
			const vec2 From = Core.m_HookState == HOOK_IDLE ? Core.m_Pos + Dir * CCharacterCore::PhysicalSize() * 1.5f : Core.m_HookPos;
			const CTuningParams &Tuning = Core.m_FakeTuned ? Core.m_FakeTuning : Core.m_Tuning;
			int Tele = 0;
			if(pCollision->IntersectLineTeleHook(From, From + Dir * Tuning.m_HookFireSpeed, nullptr, nullptr, &Tele) == TILE_TELEINHOOK)
				return false;
		}
		Core.Tick(true);
		if(Core.m_NewHook || Core.m_HookedQuadId >= 0)
			return false;
		// Match the post-core bookkeeping before deferred Move/Quantize.
		if(Core.m_EndlessHook)
			Core.m_HookTick = 0;
		if(Core.m_Jumps == -1 || Core.m_Jumps == 0 || (Core.m_Jumps == 1 && Core.m_Jumped > 0))
			Core.m_Jumped |= 2;
		else if(Core.m_JumpedTotal < Core.m_Jumps - 1 && Core.m_Jumped > 1)
			Core.m_Jumped = 1;
		if(Core.m_EndlessJump && Core.m_Jumped > 1)
			Core.m_Jumped = 1;
		Core.Move();
		Core.Quantize();
		if(!PlainArea(pCollision, Core.m_Pos, State.m_TuneZone) || length(Core.m_Vel) > 48.0f)
			return false;
		pForecast->m_aPos[Tick][0] = Core.m_Pos.x;
		pForecast->m_aPos[Tick][1] = Core.m_Pos.y;
		pForecast->m_aVel[Tick][0] = Core.m_Vel.x;
		pForecast->m_aVel[Tick][1] = Core.m_Vel.y;
	}
	return true;
}
