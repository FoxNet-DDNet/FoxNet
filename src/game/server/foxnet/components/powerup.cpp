#include "powerup.h"

#include "zones/zone.h"
#include "zones/zonemanager.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/net.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <generated/protocol.h>

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/component.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/teams.h>
#include <game/server/player.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
	void CollectMapSpawnPoints(CMultiMaps *pMultiMap, std::vector<vec2> &OutSeeds)
	{
		CCollision *pCollision = &pMultiMap->m_Collision;
		const int W = pCollision->GetWidth();
		const int H = pCollision->GetHeight();
		OutSeeds.clear();
		OutSeeds.reserve(16);

		for(int y = 0; y < H; ++y)
		{
			for(int x = 0; x < W; ++x)
			{
				const int Ent = pCollision->Entity(x, y, LAYER_GAME);
				if(Ent >= ENTITY_SPAWN && Ent <= ENTITY_SPAWN_BLUE)
					OutSeeds.emplace_back(x * 32.0f + 16.0f, y * 32.0f + 16.0f);

				if(pCollision->FrontLayer())
				{
					const int FrontEnt = pCollision->Entity(x, y, LAYER_FRONT);
					if(FrontEnt >= ENTITY_SPAWN && FrontEnt <= ENTITY_SPAWN_BLUE)
						OutSeeds.emplace_back(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				}
			}
		}
	}

	// Returns true if a stopper tile with the given index/flags blocks downward
	// movement onto it, i.e. it acts as a floor a falling player lands and rests
	// on. Mirrors the CANTMOVE_DOWN cases of GetMoveRestrictionsRaw in collision.cpp.
	bool StopperBlocksDown(int Tile, int Flags)
	{
		Flags = Flags & (TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE);
		switch(Tile)
		{
		case TILE_STOP:
			return Flags == ROTATION_0 ||
			       Flags == (static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_180));
		case TILE_STOPS:
			return Flags == ROTATION_0 || Flags == ROTATION_180 ||
			       Flags == (static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_0)) ||
			       Flags == (static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_180));
		case TILE_STOPA:
			return true;
		default:
			return false;
		}
	}

	struct SSpawnBuildData
	{
		int m_Width = 0;
		int m_Height = 0;
		std::vector<int> m_GameTiles;
		std::vector<int> m_FrontTiles;
		std::vector<int> m_GameFlags;
		std::vector<int> m_FrontFlags;
		std::vector<int> m_SwitchTiles;
		std::vector<CTeleTile> m_TeleTiles;
		std::vector<std::vector<vec2>> m_TeleOuts;
		std::vector<std::vector<vec2>> m_TeleCheckOuts;
		std::vector<vec2> m_Seeds;

		std::vector<CQuadData> m_QuadDatas;
		std::vector<uint8_t> m_QuadUnfreezeTiles;
	};

	SSpawnBuildData SnapshotBuildData(CMultiMaps *pMultiMap, CZoneManager *pZoneManager)
	{
		SSpawnBuildData Data;
		if(!pMultiMap)
			return Data;

		CCollision *pCollision = &pMultiMap->m_Collision;
		Data.m_Width = pCollision->GetWidth();
		Data.m_Height = pCollision->GetHeight();
		if(Data.m_Width <= 0 || Data.m_Height <= 0)
			return Data;

		const int Size = Data.m_Width * Data.m_Height;
		Data.m_GameTiles.resize(Size);
		Data.m_FrontTiles.resize(Size);
		Data.m_GameFlags.resize(Size);
		Data.m_FrontFlags.resize(Size);
		Data.m_SwitchTiles.resize(Size);

		const CTeleTile *pTele = pCollision->TeleLayer();
		if(pTele)
			Data.m_TeleTiles.assign(pTele, pTele + Size);

		for(int i = 0; i < Size; ++i)
		{
			Data.m_GameTiles[i] = pCollision->GetTileIndex(i);
			Data.m_FrontTiles[i] = pCollision->GetFrontTileIndex(i);
			Data.m_GameFlags[i] = pCollision->GetTileFlags(i);
			Data.m_FrontFlags[i] = pCollision->GetFrontTileFlags(i);
			Data.m_SwitchTiles[i] = pCollision->GetSwitchType(i);
		}

		CollectMapSpawnPoints(pMultiMap, Data.m_Seeds);
		for(int i = 0; i < Size; ++i)
		{
			if(Data.m_TeleTiles.empty())
				break;

			const CTeleTile &Tile = Data.m_TeleTiles[i];
			if(Tile.m_Number <= 0)
				continue;

			const int TeleIndex = Tile.m_Number - 1;
			const vec2 Pos(i % Data.m_Width * 32.0f + 16.0f, i / Data.m_Width * 32.0f + 16.0f);
			if(Tile.m_Type == TILE_TELEOUT)
			{
				if((int)Data.m_TeleOuts.size() <= TeleIndex)
					Data.m_TeleOuts.resize(TeleIndex + 1);
				Data.m_TeleOuts[TeleIndex].push_back(Pos);
			}
			else if(Tile.m_Type == TILE_TELECHECKOUT)
			{
				if((int)Data.m_TeleCheckOuts.size() <= TeleIndex + 1)
					Data.m_TeleCheckOuts.resize(TeleIndex + 2);
				Data.m_TeleCheckOuts[Tile.m_Number].push_back(Pos);
			}
		}

		if(pZoneManager)
		{
			for(const CQuadZone *pZone : pZoneManager->Zones(EZoneType::Unfreeze))
			{
				for(const CQuadData &QuadData : pZone->Quads())
				{
					Data.m_QuadDatas.push_back(QuadData);
				}
			}
		}

		Data.m_QuadUnfreezeTiles.assign(Size, 0);
		for(const CQuadData &Quad : Data.m_QuadDatas)
		{
			const int MinTileX = std::max(0, (int)std::floor(Quad.m_AabbMin.x / 32.0f));
			const int MaxTileX = std::min(Data.m_Width - 1, (int)std::floor(Quad.m_AabbMax.x / 32.0f));
			const int MinTileY = std::max(0, (int)std::floor(Quad.m_AabbMin.y / 32.0f));
			const int MaxTileY = std::min(Data.m_Height - 1, (int)std::floor(Quad.m_AabbMax.y / 32.0f));

			const vec2 aPoints[4] = {Quad.m_aPoints[0], Quad.m_aPoints[1], Quad.m_aPoints[2], Quad.m_aPoints[3]};
			for(int TileY = MinTileY; TileY <= MaxTileY; ++TileY)
			{
				for(int TileX = MinTileX; TileX <= MaxTileX; ++TileX)
				{
					const vec2 TilePos(TileX * 32.0f + 16.0f, TileY * 32.0f + 16.0f);
					if(!Quad.AabbIntersects(TilePos, vec2(16.0f, 16.0f)))
						continue;

					if(Quad.m_Animated)
					{
						Data.m_QuadUnfreezeTiles[TileY * Data.m_Width + TileX] = 1;
						continue;
					}

					if(!InsideQuadrilateral(TilePos, aPoints) && !InsideQuadrilateral(TilePos, aPoints, vec2(16.0f, 16.0f)))
						continue;

					Data.m_QuadUnfreezeTiles[TileY * Data.m_Width + TileX] = 1;
				}
			}
		}

		return Data;
	}

	std::vector<vec2> BuildSpawnCandidates(const SSpawnBuildData &Data, size_t MapIdx)
	{
		if(Data.m_Width <= 0 || Data.m_Height <= 0)
			return {};

		std::vector<vec2> vSpawnCandidates;
		if(Data.m_Seeds.empty())
			return {};

		const int W = Data.m_Width;
		const int H = Data.m_Height;

		const auto ToIndex = [&](int X, int Y) { return Y * W + X; };
		const auto InBounds = [&](int X, int Y) { return X >= 0 && X < W && Y >= 0 && Y < H; };
		const auto VisitedIndex = [&](int Idx, bool CrossedStart) { return (size_t)Idx * 2 + (CrossedStart ? 1u : 0u); };

		bool HasAnyStartTiles = false;
		for(int Y = 0; Y < H && !HasAnyStartTiles; ++Y)
		{
			for(int X = 0; X < W && !HasAnyStartTiles; ++X)
			{
				const int Idx = ToIndex(X, Y);
				if(Data.m_GameTiles[Idx] == TILE_START || Data.m_FrontTiles[Idx] == TILE_START)
					HasAnyStartTiles = true;
			}
		}

		const auto IsAirAt = [&](int Tx, int Ty) -> bool {
			if(!InBounds(Tx, Ty))
				return false;

			const int Idx = ToIndex(Tx, Ty);
			return Data.m_GameTiles[Idx] == TILE_AIR && Data.m_FrontTiles[Idx] == TILE_AIR;
		};

		const auto SurroundedByAir = [&](int Cx, int Cy, int RadiusTiles = 1) -> bool {
			for(int OutY = -RadiusTiles; OutY <= RadiusTiles; ++OutY)
			{
				for(int OutX = -RadiusTiles; OutX <= RadiusTiles; ++OutX)
				{
					if(!IsAirAt(Cx + OutX, Cy + OutY))
						return false;
				}
			}
			return true;
		};

		const auto IsTeleTileAt = [&](int Tx, int Ty) -> bool {
			if(Data.m_TeleTiles.empty() || !InBounds(Tx, Ty))
				return false;

			const int Idx = ToIndex(Tx, Ty);
			return Data.m_TeleTiles[Idx].m_Type != 0;
		};

		const auto IsFreezeLikeAtIndex = [&](int Idx) -> bool {
			const int Game = Data.m_GameTiles[Idx];
			const int Front = Data.m_FrontTiles[Idx];
			const int Sw = Data.m_SwitchTiles[Idx];
			return Game == TILE_FREEZE || Game == TILE_DFREEZE || Game == TILE_LFREEZE ||
			       Front == TILE_FREEZE || Front == TILE_DFREEZE || Front == TILE_LFREEZE ||
			       Sw == TILE_FREEZE || Sw == TILE_DFREEZE || Sw == TILE_LFREEZE;
		};

		// Deep freeze (TILE_DFREEZE) keeps the player frozen even after they leave
		// the tile; it only clears at a DUNFREEZE tile. Normal/live freeze instead
		// auto-thaws after a delay (see CCharacter::HandleTiles), so a player can
		// cross it by momentum and land anywhere without needing an unfreeze tile.
		const auto IsDeepFreezeAtIndex = [&](int Idx) -> bool {
			return Data.m_GameTiles[Idx] == TILE_DFREEZE ||
			       Data.m_FrontTiles[Idx] == TILE_DFREEZE ||
			       Data.m_SwitchTiles[Idx] == TILE_DFREEZE;
		};

		const auto IsUnfreezeLikeAtIndex = [&](int Idx) -> bool {
			const int Game = Data.m_GameTiles[Idx];
			const int Front = Data.m_FrontTiles[Idx];
			const int Sw = Data.m_SwitchTiles[Idx];

			const bool QuadUnfreeze = Idx >= 0 && Idx < (int)Data.m_QuadUnfreezeTiles.size() && Data.m_QuadUnfreezeTiles[Idx] != 0;

			return Game == TILE_UNFREEZE || Game == TILE_DUNFREEZE || Game == TILE_LUNFREEZE ||
			       Front == TILE_UNFREEZE || Front == TILE_DUNFREEZE || Front == TILE_LUNFREEZE ||
			       Sw == TILE_UNFREEZE || Sw == TILE_DUNFREEZE || Sw == TILE_LUNFREEZE ||
			       QuadUnfreeze;
		};

		const auto IsBlockedForSpawnNav = [&](int Tx, int Ty) -> bool {
			if(!InBounds(Tx, Ty))
				return true;

			const int Idx = ToIndex(Tx, Ty);
			const int Game = Data.m_GameTiles[Idx];
			const int Front = Data.m_FrontTiles[Idx];
			const bool Solid = Game == TILE_SOLID || Game == TILE_NOHOOK;
			const bool Finish = Game == TILE_FINISH || Front == TILE_FINISH;
			const bool Kill = Game == TILE_DEATH || Front == TILE_DEATH;
			const bool StopA = Game == TILE_STOPA || Front == TILE_STOPA;
			return Solid || Finish || Kill || StopA;
		};

		const auto IsStartAtIndex = [&](int Idx) -> bool {
			if(Idx < 0)
				return false;

			return Data.m_GameTiles[Idx] == TILE_START || Data.m_FrontTiles[Idx] == TILE_START;
		};

		const auto IsTeleInRegular = [](unsigned char Type) {
			return Type == TILE_TELEIN || Type == TILE_TELEINEVIL;
		};
		const auto IsTeleInCheckpoint = [](unsigned char Type) {
			return Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL;
		};
		const auto TeleCheckpointAtIndex = [&](int Idx) -> int {
			if(Data.m_TeleTiles.empty() || Idx < 0)
				return 0;

			if(Data.m_TeleTiles[Idx].m_Type == TILE_TELECHECK)
				return (int)Data.m_TeleTiles[Idx].m_Number;
			return 0;
		};

		constexpr int UnfreezeRadiusX = 13;
		constexpr int UnfreezeRadiusUp = 7;
		constexpr int UnfreezeRadiusDown = 50;

		const auto FindNearbyUnfreezeIndex = [&](int Tx, int Ty, bool CrossedStart, const std::vector<uint8_t> &Visited, int &OutIdx) -> bool {
			if(!InBounds(Tx, Ty))
				return false;

			const int MinX = std::max(0, Tx - UnfreezeRadiusX);
			const int MaxX = std::min(W - 1, Tx + UnfreezeRadiusX);
			const int MinY = std::max(0, Ty - UnfreezeRadiusUp);
			const int MaxY = std::min(H - 1, Ty + UnfreezeRadiusDown);

			bool Found = false;
			int BestIdx = -1;

			for(int Y = MinY; Y <= MaxY; ++Y)
			{
				for(int X = MinX; X <= MaxX; ++X)
				{
					const int Idx = ToIndex(X, Y);
					if(!IsUnfreezeLikeAtIndex(Idx))
						continue;

					if(IsBlockedForSpawnNav(X, Y))
						continue;

					const bool CandidateCrossedStart = CrossedStart || IsStartAtIndex(Idx);
					const bool CandidateUnvisited = !Visited[VisitedIndex(Idx, CandidateCrossedStart)];
					const bool CandidateBelow = Y >= Ty;
					const int Dy = std::abs(Y - Ty);
					const int Dx = std::abs(X - Tx);

					if(!Found)
					{
						Found = true;
						BestIdx = Idx;
						continue;
					}

					const int BestX = BestIdx % W;
					const int BestY = BestIdx / W;

					const bool BestCrossedStart = CrossedStart || IsStartAtIndex(BestIdx);
					const bool BestUnvisited = !Visited[VisitedIndex(BestIdx, BestCrossedStart)];
					const bool BestBelow = BestY >= Ty;
					const int BestDy = std::abs(BestY - Ty);
					const int BestDx = std::abs(BestX - Tx);

					if(CandidateUnvisited != BestUnvisited)
					{
						if(CandidateUnvisited && !BestUnvisited)
							BestIdx = Idx;
						continue;
					}
					if(CandidateBelow != BestBelow)
					{
						if(CandidateBelow && !BestBelow)
							BestIdx = Idx;
						continue;
					}
					if(Dy != BestDy)
					{
						if(Dy < BestDy)
							BestIdx = Idx;
						continue;
					}
					if(Dx < BestDx)
						BestIdx = Idx;
				}
			}

			if(!Found)
				return false;

			OutIdx = BestIdx;
			return true;
		};

		auto EnqueueTeleDestinationsByTeleNum = [&](int TeleNumMinusOne, std::vector<uint8_t> &Visited, std::deque<std::tuple<int, int, int, bool>> &Q, int Cp, bool CrossedStart) {
			if(TeleNumMinusOne < 0 || TeleNumMinusOne >= (int)Data.m_TeleOuts.size())
				return;

			const auto &vOuts = Data.m_TeleOuts[TeleNumMinusOne];
			if(vOuts.empty())
				return;

			for(const vec2 &OutPos : vOuts)
			{
				const int OutX = std::clamp((int)std::floor(OutPos.x / 32.0f), 0, W - 1);
				const int OutY = std::clamp((int)std::floor(OutPos.y / 32.0f), 0, H - 1);
				const int OutIdx = ToIndex(OutX, OutY);
				if(!IsBlockedForSpawnNav(OutX, OutY))
				{
					const bool NextCrossedStart = CrossedStart || IsStartAtIndex(OutIdx);
					const size_t OutVisitedIdx = VisitedIndex(OutIdx, NextCrossedStart);
					if(Visited[OutVisitedIdx])
						continue;
					Visited[OutVisitedIdx] = 1;
					Q.emplace_back(OutX, OutY, Cp, NextCrossedStart);
				}
			}
		};

		auto EnqueueTeleCheckpointDestinationsByCp = [&](int CpNumber, std::vector<uint8_t> &Visited, std::deque<std::tuple<int, int, int, bool>> &Q, bool CrossedStart) {
			if(CpNumber <= 0 || CpNumber >= (int)Data.m_TeleCheckOuts.size())
				return;

			const auto &vOuts = Data.m_TeleCheckOuts[CpNumber];
			if(vOuts.empty())
				return;

			for(const vec2 &OutPos : vOuts)
			{
				const int OutX = std::clamp((int)std::floor(OutPos.x / 32.0f), 0, W - 1);
				const int OutY = std::clamp((int)std::floor(OutPos.y / 32.0f), 0, H - 1);
				const int OutIdx = ToIndex(OutX, OutY);
				if(!IsBlockedForSpawnNav(OutX, OutY))
				{
					const bool NextCrossedStart = CrossedStart || IsStartAtIndex(OutIdx);
					const size_t OutVisitedIdx = VisitedIndex(OutIdx, NextCrossedStart);
					if(Visited[OutVisitedIdx])
						continue;
					Visited[OutVisitedIdx] = 1;
					Q.emplace_back(OutX, OutY, CpNumber, NextCrossedStart);
				}
			}
		};

		std::deque<std::tuple<int, int, int, bool>> Q;
		std::vector<uint8_t> Visited((size_t)W * H * 2, 0);
		std::vector<uint8_t> CandidateAdded((size_t)W * H, 0);

		for(const vec2 &Seed : Data.m_Seeds)
		{
			const int SeedX = std::clamp((int)std::floor(Seed.x / 32.0f), 0, W - 1);
			const int SeedY = std::clamp((int)std::floor(Seed.y / 32.0f), 0, H - 1);
			const int SeedIdx = ToIndex(SeedX, SeedY);
			const bool InitialCrossedStart = HasAnyStartTiles ? IsStartAtIndex(SeedIdx) : true;
			const size_t SeedVisitedIdx = VisitedIndex(SeedIdx, InitialCrossedStart);
			if(!Visited[SeedVisitedIdx])
			{
				Visited[SeedVisitedIdx] = 1;
				Q.emplace_back(SeedX, SeedY, 0, InitialCrossedStart);
			}
		}

		constexpr int SolidRadius = 6;
		const int DirX[4] = {1, -1, 0, 0};
		const int DirY[4] = {0, 0, 1, -1};

		const auto IsDeathAtIndex = [&](int Idx) -> bool {
			return Data.m_GameTiles[Idx] == TILE_DEATH || Data.m_FrontTiles[Idx] == TILE_DEATH;
		};

		// A tile a falling player lands and rests on: solid ground, or a stopper
		// oriented to block downward movement onto it.
		const auto BlocksFallAtIndex = [&](int Idx) -> bool {
			const int Game = Data.m_GameTiles[Idx];
			if(Game == TILE_SOLID || Game == TILE_NOHOOK)
				return true;
			if(StopperBlocksDown(Game, Data.m_GameFlags[Idx]))
				return true;
			if(StopperBlocksDown(Data.m_FrontTiles[Idx], Data.m_FrontFlags[Idx]))
				return true;
			return false;
		};

		// After a momentum crossing of normal freeze, the player only actually
		// arrives (and thaws) if they can come to rest off the freeze at the landing.
		// Model it as a downward fall from the exit tile: reaching an unfreeze tile or
		// landing on solid ground / a down-stopper means they settle and thaw; falling
		// straight back into freeze (or off the map / into death) means they never do,
		// so the far side isn't reachable alone. This is what drops areas that are
		// fully ringed by freeze (a hollow pocket, reachable only with a second player)
		// while keeping real floored areas behind a freeze wall.
		constexpr int FallScanTiles = 40;
		const auto CanSettleAfterCross = [&](int Ex, int Ey) -> bool {
			const int ExitIdx = ToIndex(Ex, Ey);
			if(IsUnfreezeLikeAtIndex(ExitIdx))
				return true;
			for(int Dy = 1; Dy <= FallScanTiles; ++Dy)
			{
				const int Ty = Ey + Dy;
				if(!InBounds(Ex, Ty))
					return false; // fell out of the map
				const int Idx = ToIndex(Ex, Ty);
				if(IsDeathAtIndex(Idx))
					return false;
				if(IsUnfreezeLikeAtIndex(Idx))
					return true;
				if(IsFreezeLikeAtIndex(Idx))
					return false; // falls straight back into freeze -> never thaws
				if(BlocksFallAtIndex(Idx))
					return true; // lands on solid ground / a stopper and thaws
				// open air: keep falling
			}
			return true; // long open drop -> a genuine open region, not a freeze pocket
		};

		// How far (in tiles) a player can realistically carry momentum through a
		// contiguous normal-freeze region before landing. Bounds the over-
		// approximation of the momentum crossing below.
		constexpr int MaxFreezeCrossTiles = 40;

		// Flood through a connected region of normal (non-deep) freeze starting at
		// (EntryX, EntryY), bounded by MaxFreezeCrossTiles, and collect the tile
		// indices where the region exits into open space. Since normal freeze auto-
		// thaws, each such exit is somewhere the player can end up after crossing.
		// Deep-freeze tiles are treated as walls here: they don't thaw on their own,
		// so crossing them by momentum doesn't make the far side reachable.
		std::vector<uint32_t> FreezeStamp((size_t)W * H, 0);
		uint32_t FreezeGen = 0;
		std::deque<std::pair<int, int>> FreezeQ;
		const auto CollectMomentumFreezeExits = [&](int EntryX, int EntryY, std::vector<int> &OutExits) {
			OutExits.clear();
			++FreezeGen;
			FreezeQ.clear();

			const int EntryIdx = ToIndex(EntryX, EntryY);
			FreezeStamp[EntryIdx] = FreezeGen;
			FreezeQ.emplace_back(EntryIdx, 0);

			while(!FreezeQ.empty())
			{
				const auto [Idx, Dist] = FreezeQ.front();
				FreezeQ.pop_front();

				const int Cx = Idx % W;
				const int Cy = Idx / W;
				for(int k = 0; k < 4; ++k)
				{
					const int Nx = Cx + DirX[k];
					const int Ny = Cy + DirY[k];
					if(!InBounds(Nx, Ny))
						continue;

					const int NIdx = ToIndex(Nx, Ny);
					if(FreezeStamp[NIdx] == FreezeGen)
						continue;
					FreezeStamp[NIdx] = FreezeGen;

					if(IsBlockedForSpawnNav(Nx, Ny))
						continue;

					if(IsFreezeLikeAtIndex(NIdx))
					{
						if(IsDeepFreezeAtIndex(NIdx))
							continue; // can't carry momentum through deep freeze
						if(Dist + 1 < MaxFreezeCrossTiles)
							FreezeQ.emplace_back(NIdx, Dist + 1);
						continue;
					}

					// Non-freeze, non-blocked tile: a potential landing. Only count it
					// if the player can actually settle and thaw here, rather than fall
					// straight back into the freeze region (see CanSettleAfterCross).
					if(CanSettleAfterCross(Nx, Ny))
						OutExits.push_back(NIdx);
				}
			}
		};
		std::vector<int> FreezeExits;

		while(!Q.empty())
		{
			auto [X, Y, Cp, CrossedStart] = Q.front();
			Q.pop_front();

			const int CurIdx = ToIndex(X, Y);
			const CTeleTile *pTele = Data.m_TeleTiles.empty() ? nullptr : Data.m_TeleTiles.data();
			if(pTele)
			{
				const int CpHere = TeleCheckpointAtIndex(CurIdx);
				if(CpHere > 0)
					Cp = CpHere;
			}
			if(IsStartAtIndex(CurIdx))
				CrossedStart = true;

			const bool AllowCandidateNow = CrossedStart || !HasAnyStartTiles;

			if(AllowCandidateNow && SurroundedByAir(X, Y, 1) && !IsTeleTileAt(X, Y))
			{
				const vec2 Pos(X * 32.0f + 16.0f, Y * 32.0f + 16.0f);
				const int Cx = std::clamp((int)std::floor(Pos.x / 32.0f), 0, W - 1);
				const int Cy = std::clamp((int)std::floor(Pos.y / 32.0f), 0, H - 1);
				const int RadiusSquared = SolidRadius * SolidRadius;
				bool HasSolid = false;
				for(int Dy = -SolidRadius; Dy <= SolidRadius && !HasSolid; ++Dy)
				{
					for(int Dx = -SolidRadius; Dx <= SolidRadius; ++Dx)
					{
						if((Dx * Dx + Dy * Dy) > RadiusSquared)
							continue;

						const int Tx = Cx + Dx;
						const int Ty = Cy + Dy;
						if(!InBounds(Tx, Ty))
							continue;

						const int ProbeIdx = ToIndex(Tx, Ty);
						const int ProbeGame = Data.m_GameTiles[ProbeIdx];
						if(ProbeGame == TILE_SOLID || ProbeGame == TILE_NOHOOK)
						{
							HasSolid = true;
							break;
						}
					}
				}

				if(HasSolid && !CandidateAdded[CurIdx])
				{
					CandidateAdded[CurIdx] = 1;
					vSpawnCandidates.push_back(Pos);
				}
			}

			for(int k = 0; k < 4; ++k)
			{
				const int NextX = X + DirX[k];
				const int NextY = Y + DirY[k];
				if(!InBounds(NextX, NextY))
					continue;

				const int NextIdx = ToIndex(NextX, NextY);

				int NextCp = Cp;
				bool NextCrossedStart = CrossedStart;
				if(pTele)
				{
					const int CpNext = TeleCheckpointAtIndex(NextIdx);
					if(CpNext > 0)
						NextCp = CpNext;

					const unsigned char NextType = pTele[NextIdx].m_Type;
					const unsigned char NextNum = pTele[NextIdx].m_Number;
					if(NextNum > 0 && (IsTeleInRegular(NextType) || IsTeleInCheckpoint(NextType)))
					{
						const size_t NextVisitedIdx = VisitedIndex(NextIdx, NextCrossedStart);
						if(Visited[NextVisitedIdx])
							continue;
						Visited[NextVisitedIdx] = 1;
						if(IsTeleInCheckpoint(NextType))
						{
							if(NextCp > 0)
								EnqueueTeleCheckpointDestinationsByCp(NextCp, Visited, Q, NextCrossedStart);
						}
						else
						{
							EnqueueTeleDestinationsByTeleNum(NextNum - 1, Visited, Q, NextCp, NextCrossedStart);
						}
						continue;
					}
				}

				if(IsBlockedForSpawnNav(NextX, NextY))
					continue;

				NextCrossedStart = NextCrossedStart || IsStartAtIndex(NextIdx);
				const size_t NextVisitedIdx = VisitedIndex(NextIdx, NextCrossedStart);
				if(Visited[NextVisitedIdx])
					continue;

				if(IsFreezeLikeAtIndex(NextIdx))
				{
					if(IsDeepFreezeAtIndex(NextIdx))
					{
						// Deep freeze persists until a DUNFREEZE tile, so the far
						// side is only reachable if an unfreeze tile is close by.
						int UnfreezeIdx = -1;
						if(!FindNearbyUnfreezeIndex(NextX, NextY, NextCrossedStart, Visited, UnfreezeIdx))
							continue;

						const int UnfreezeX = UnfreezeIdx % W;
						const int UnfreezeY = UnfreezeIdx / W;
						const bool UnfreezeCrossedStart = NextCrossedStart || IsStartAtIndex(UnfreezeIdx);
						const size_t UnfreezeVisitedIdx = VisitedIndex(UnfreezeIdx, UnfreezeCrossedStart);
						if(!Visited[UnfreezeVisitedIdx])
						{
							Visited[UnfreezeVisitedIdx] = 1;
							Q.emplace_back(UnfreezeX, UnfreezeY, NextCp, UnfreezeCrossedStart);
						}
						continue;
					}

					// Normal/live freeze auto-thaws: a player can cross the whole
					// region by momentum and land on any platform on the far side,
					// with no unfreeze tile required.
					CollectMomentumFreezeExits(NextX, NextY, FreezeExits);
					for(const int ExitIdx : FreezeExits)
					{
						const int ExitX = ExitIdx % W;
						const int ExitY = ExitIdx / W;
						const bool ExitCrossedStart = NextCrossedStart || IsStartAtIndex(ExitIdx);
						const size_t ExitVisitedIdx = VisitedIndex(ExitIdx, ExitCrossedStart);
						if(!Visited[ExitVisitedIdx])
						{
							Visited[ExitVisitedIdx] = 1;
							Q.emplace_back(ExitX, ExitY, NextCp, ExitCrossedStart);
						}
					}
					continue;
				}

				Visited[NextVisitedIdx] = 1;
				Q.emplace_back(NextX, NextY, NextCp, NextCrossedStart);
			}

			if(pTele)
			{
				const unsigned char TeleType = pTele[CurIdx].m_Type;
				const unsigned char TeleNum = pTele[CurIdx].m_Number;
				if(TeleNum > 0 && (IsTeleInRegular(TeleType) || IsTeleInCheckpoint(TeleType)))
				{
					if(IsTeleInCheckpoint(TeleType))
					{
						if(Cp > 0)
							EnqueueTeleCheckpointDestinationsByCp(Cp, Visited, Q, CrossedStart);
					}
					else
					{
						EnqueueTeleDestinationsByTeleNum(TeleNum - 1, Visited, Q, Cp, CrossedStart);
					}
				}
			}
		}

		constexpr int SpawnExclusionRadiusTiles = 42;
		constexpr float ExclusionRadiusPx = SpawnExclusionRadiusTiles * 32.0f;
		if(!Data.m_Seeds.empty() && !vSpawnCandidates.empty())
		{
			std::erase_if(vSpawnCandidates, [&](const vec2 &Pos) {
				return std::ranges::any_of(Data.m_Seeds, [&](const vec2 &Seed) {
					return distance(Pos, Seed) <= ExclusionRadiusPx;
				});
			});
		}

		// Keep powerups strictly between the start and finish lines. The main flood
		// carries CrossedStart monotonically, so after crossing the start line it can
		// wander back into the spawn room and wrongly flag it as play area. Carve out
		// the pre-start region geometrically and drop candidates in it, then add a
		// buffer just inside the start/finish lines so powerups sit clearly within the
		// play area (and near-finish over-the-top leaks get trimmed too).
		if(!vSpawnCandidates.empty())
		{
			const auto IsFinishAt = [&](int X, int Y) -> bool {
				if(!InBounds(X, Y))
					return false;
				const int Idx = ToIndex(X, Y);
				return Data.m_GameTiles[Idx] == TILE_FINISH || Data.m_FrontTiles[Idx] == TILE_FINISH;
			};
			// Barrier for the pre-start flood: nav-blocks plus the start line itself.
			const auto IsPreStartBarrier = [&](int X, int Y) -> bool {
				if(!InBounds(X, Y))
					return true;
				return IsBlockedForSpawnNav(X, Y) || IsStartAtIndex(ToIndex(X, Y));
			};

			std::vector<uint8_t> PreStart((size_t)W * H, 0);
			bool PreStartTouchesFinish = false;
			if(HasAnyStartTiles)
			{
				std::deque<int> PreStartQ;
				for(const vec2 &Seed : Data.m_Seeds)
				{
					const int Sx = std::clamp((int)std::floor(Seed.x / 32.0f), 0, W - 1);
					const int Sy = std::clamp((int)std::floor(Seed.y / 32.0f), 0, H - 1);
					const int Idx = ToIndex(Sx, Sy);
					if(IsPreStartBarrier(Sx, Sy) || PreStart[Idx])
						continue;
					PreStart[Idx] = 1;
					PreStartQ.push_back(Idx);
				}
				while(!PreStartQ.empty())
				{
					const int Idx = PreStartQ.front();
					PreStartQ.pop_front();
					const int Cx = Idx % W;
					const int Cy = Idx / W;
					for(int k = 0; k < 4; ++k)
					{
						const int Nx = Cx + DirX[k];
						const int Ny = Cy + DirY[k];
						if(!InBounds(Nx, Ny))
							continue;
						if(IsFinishAt(Nx, Ny))
						{
							PreStartTouchesFinish = true;
							continue;
						}
						const int NIdx = ToIndex(Nx, Ny);
						if(PreStart[NIdx] || IsPreStartBarrier(Nx, Ny))
							continue;
						PreStart[NIdx] = 1;
						PreStartQ.push_back(NIdx);
					}
				}
			}
			// If the pre-start flood reaches the finish line, the spawns sit inside the
			// play area rather than a separate spawn room, so dropping that region would
			// wipe out real candidates. Only trust it as a spawn room otherwise.
			const bool UsePreStart = HasAnyStartTiles && !PreStartTouchesFinish;

			constexpr int BoundaryBufferTiles = 5;
			constexpr float BoundaryBufferPx = BoundaryBufferTiles * 32.0f;
			std::vector<vec2> BoundaryTiles;
			for(int Y = 0; Y < H; ++Y)
			{
				for(int X = 0; X < W; ++X)
				{
					if(IsStartAtIndex(ToIndex(X, Y)) || IsFinishAt(X, Y))
						BoundaryTiles.emplace_back(X * 32.0f + 16.0f, Y * 32.0f + 16.0f);
				}
			}

			std::erase_if(vSpawnCandidates, [&](const vec2 &Pos) {
				const int Px = std::clamp((int)std::floor(Pos.x / 32.0f), 0, W - 1);
				const int Py = std::clamp((int)std::floor(Pos.y / 32.0f), 0, H - 1);
				if(UsePreStart && PreStart[ToIndex(Px, Py)])
					return true;
				return std::ranges::any_of(BoundaryTiles, [&](const vec2 &Boundary) {
					return distance(Pos, Boundary) <= BoundaryBufferPx;
				});
			});
		}

		log_info("spawn-candidates", "found %d spawn point%s for map %" PRIzu, (int)vSpawnCandidates.size(), vSpawnCandidates.size() == 1 ? "" : "s", MapIdx);
		return vSpawnCandidates;
	}
}

CPowerUps::~CPowerUps()
{
	OnShutdown(nullptr);
}

void CPowerUps::QueueRebuildSnapshot(size_t MapIdx)
{
	auto pShared = m_pShared;
	CMultiMaps *pMultiMap = MultiMaps(MapIdx);
	CZoneManager *pZoneManager = &GameServer()->m_ZoneManager;
	if(!pMultiMap)
	{
		CLockScope Lock(pShared->m_CacheLock);
		pShared->m_RebuildBusy = false;
		return;
	}

	const CMultiMaps *pMultiMapKey = pMultiMap;
	uint64_t Generation = 0;
	{
		CLockScope Lock(pShared->m_CacheLock);
		auto It = pShared->m_RebuildGenerations.find(pMultiMapKey);
		if(It == pShared->m_RebuildGenerations.end())
		{
			pShared->m_RebuildBusy = false;
			return;
		}
		Generation = It->second;
	}

	auto pData = std::make_shared<SSpawnBuildData>(SnapshotBuildData(pMultiMap, pZoneManager));

	{
		CLockScope Lock(pShared->m_CacheLock);
		auto It = pShared->m_RebuildGenerations.find(pMultiMapKey);
		if(It == pShared->m_RebuildGenerations.end() || It->second != Generation)
		{
			pShared->m_RebuildBusy = false;
			return;
		}
	}

	std::thread([pShared, pMultiMapKey, MapIdx, Generation, pData]() {
		std::vector<vec2> vSpawnCandidates = BuildSpawnCandidates(*pData, MapIdx);

		CLockScope Lock(pShared->m_CacheLock);
		auto It = pShared->m_RebuildGenerations.find(pMultiMapKey);
		if(It != pShared->m_RebuildGenerations.end() && It->second == Generation)
			pShared->m_CachedCandidates[pMultiMapKey] = std::move(vSpawnCandidates);
		pShared->m_RebuildBusy = false;
	}).detach();
}

void CPowerUps::RebuildAsync(size_t MapIdx)
{
	auto pShared = m_pShared;
	CMultiMaps *pMultiMap = MultiMaps(MapIdx);
	if(!pMultiMap)
		return;

	{
		CLockScope Lock(pShared->m_CacheLock);
		++pShared->m_RebuildGenerations[pMultiMap];
		pShared->m_CachedCandidates.erase(pMultiMap);

		if(pShared->m_RebuildBusy)
		{
			pShared->m_RebuildDeferred = true;
			return;
		}

		pShared->m_RebuildBusy = true;
	}

	QueueRebuildSnapshot(MapIdx);
}

void CPowerUps::OnMapLoad(size_t MapIdx)
{
	if(MapIdx != DefaultMapIndex)
		return; // ignore other map indexes for now

	RebuildAsync(MapIdx);
}

void CPowerUps::OnMapUnload(size_t MapIdx)
{
	if(MapIdx != DefaultMapIndex)
		return; // ignore other map indexes for now

	auto pShared = m_pShared;
	const CMultiMaps *pMultiMap = MultiMaps(MapIdx);
	if(!pMultiMap)
		return;

	CLockScope Lock(pShared->m_CacheLock);
	pShared->m_CachedCandidates.erase(pMultiMap);
	++pShared->m_RebuildGenerations[pMultiMap];
	pShared->m_RebuildDeferred = false;
}

void CPowerUps::OnShutdown(void *pPersistentData)
{
	auto pOldShared = std::move(m_pShared);
	m_pShared = std::make_shared<SSharedState>();

	CLockScope Lock(pOldShared->m_CacheLock);
	pOldShared->m_CachedCandidates.clear();
	pOldShared->m_RebuildGenerations.clear();
	pOldShared->m_RebuildDeferred = false;
}

void CPowerUps::Rebuild(size_t MapIdx)
{
	RebuildAsync(MapIdx);
}

bool CPowerUps::TryPickCachedCandidate(size_t MapIdx, vec2 &Out) const
{
	auto pShared = m_pShared;
	const CMultiMaps *pMultiMap = MultiMaps(MapIdx);
	if(!pMultiMap)
		return false;

	CLockScope Lock(pShared->m_CacheLock);
	auto It = pShared->m_CachedCandidates.find(pMultiMap);
	if(It == pShared->m_CachedCandidates.end() || It->second.empty())
		return false;

	static thread_local std::mt19937 s_Rng{std::random_device{}()};
	std::uniform_int_distribution<size_t> Pick(0, It->second.size() - 1);
	Out = It->second[Pick(s_Rng)];
	return true;
}

size_t CPowerUps::SpawnCandidateCount(size_t MapIdx) const
{
	auto pShared = m_pShared;
	const CMultiMaps *pMultiMap = MultiMaps(MapIdx);
	if(!pMultiMap)
		return 0;

	CLockScope Lock(pShared->m_CacheLock);
	auto It = pShared->m_CachedCandidates.find(pMultiMap);
	return It != pShared->m_CachedCandidates.end() ? It->second.size() : 0;
}

void CPowerUps::OnTick()
{
	auto pShared = m_pShared;
	bool StartDeferredRebuild = false;
	{
		CLockScope Lock(pShared->m_CacheLock);
		if(pShared->m_RebuildDeferred && !pShared->m_RebuildBusy)
		{
			pShared->m_RebuildDeferred = false;
			pShared->m_RebuildBusy = true;
			StartDeferredRebuild = true;
		}
	}

	if(StartDeferredRebuild)
		QueueRebuildSnapshot(DefaultMapIndex);

	for(size_t i = 0; i < m_vPowerups.size();)
	{
		if(TickPowerup(m_vPowerups[i]))
		{
			i++;
			continue;
		}

		FreeIds(m_vPowerups[i]);
		m_vPowerups.erase(m_vPowerups.begin() + i);
	}

	TrySpawn();
}

std::optional<vec2> CPowerUps::GetRandomAccessiblePos()
{
	const auto Dist2 = [](const vec2 &a, const vec2 &b) {
		const float DistX = a.x - b.x;
		const float DistY = a.y - b.y;
		return DistX * DistX + DistY * DistY;
	};

	constexpr float TileSize = 32.0f;
	constexpr float MinPlayerDist = TileSize * 25.0f;
	// Keep a fresh powerup from stacking on / hugging an existing one, so on tight
	// maps with few candidates they spread out instead of repeating the same spot.
	constexpr float MinPowerupDist = TileSize * 12.0f;

	// Snapshot the positions of powerups already placed on this map.
	std::vector<vec2> vPowerupPositions;
	vPowerupPositions.reserve(m_vPowerups.size());
	for(const CPowerUp &Powerup : m_vPowerups)
	{
		if(Powerup.m_MultiMapIdx == DefaultMapIndex)
			vPowerupPositions.push_back(Powerup.m_Pos);
	}

	const auto NearestPowerupDist2 = [&](const vec2 &Pos) {
		float Min2 = std::numeric_limits<float>::infinity();
		for(const vec2 &Other : vPowerupPositions)
			Min2 = std::min(Min2, Dist2(Other, Pos));
		return Min2;
	};

	// Phase 1: try random candidates, accepting the first that is clear of both
	// live players and existing powerups.
	for(int Tries = 0; Tries < 16; ++Tries)
	{
		vec2 Pos;
		if(!TryPickCachedCandidate(DefaultMapIndex, Pos))
			return std::nullopt;

		if(NearestPowerupDist2(Pos) < MinPowerupDist * MinPowerupDist)
			continue;

		CEntity *apEnts[64] = {0};
		const int Num = GameServer()->m_World.FindEntities(Pos, MinPlayerDist, apEnts, std::size(apEnts), CGameWorld::ENTTYPE_CHARACTER, DefaultMapIndex);
		bool NearPlayer = false;
		for(int i = 0; i < Num; ++i)
		{
			auto *pChr = static_cast<CCharacter *>(apEnts[i]);
			if(pChr && pChr->IsAlive())
			{
				NearPlayer = true;
				break;
			}
		}
		if(NearPlayer)
			continue;

		return Pos;
	}

	// Fallback: on tight/crowded maps nothing clears the spacing above, so pick the
	// candidate sitting furthest from the nearest crowding thing -- any live player
	// or existing powerup. This spreads powerups apart and avoids overlaps even when
	// candidates are scarce.
	float BestScore = -1.0f;
	vec2 BestPos = vec2(0.0f, 0.0f);
	for(int k = 0; k < 48; ++k)
	{
		vec2 Pos;
		if(!TryPickCachedCandidate(DefaultMapIndex, Pos))
			break;

		float MinDist2 = NearestPowerupDist2(Pos);
		CEntity *apEnts[128] = {0};
		const int Num = GameServer()->m_World.FindEntities(Pos, 1024.0f, apEnts, std::size(apEnts), CGameWorld::ENTTYPE_CHARACTER, DefaultMapIndex);
		for(int i = 0; i < Num; ++i)
		{
			auto *pChr = static_cast<CCharacter *>(apEnts[i]);
			if(!pChr || !pChr->IsAlive())
				continue;
			MinDist2 = std::min(MinDist2, Dist2(pChr->m_Pos, Pos));
			if(MinDist2 == 0.0f)
				break;
		}
		if(MinDist2 > BestScore)
		{
			BestScore = MinDist2;
			BestPos = Pos;
		}
	}
	if(BestScore >= 0.0f)
		return BestPos;

	return std::nullopt;
}


void CPowerUps::TrySpawn()
{
	if(!g_Config.m_SvPowerUps)
		return;	
	if(!g_Config.m_SvAccounts)
		return; // Powerups require accounts to store the data
	if(GameServer()->GlobalTuning(DefaultMapIndex)->m_TeleGrenade)
		return; // nah, too much work to make them work with tele grenades
	if(m_vPowerups.size() >= (size_t)g_Config.m_SvPowerUpsMax)
		return;
	if(m_SpawnDelay > Server()->Tick())
		return;

	const std::optional<vec2> RandomPos = GetRandomAccessiblePos();
	if(!RandomPos.has_value())
	{
		m_SpawnDelay = Server()->Tick() + Server()->TickSpeed();
		return;
	}

	CPowerUp Powerup;
	Powerup.m_Pos = RandomPos.value();
	Powerup.m_MultiMapIdx = DefaultMapIndex;
	Powerup.m_StartTick = Server()->Tick();

	Powerup.m_PickupId = Server()->SnapNewId();
	for(CPowerUp::CSnapData &Snap : Powerup.m_aSnap)
		Snap.m_Id = Server()->SnapNewId();
	std::sort(Powerup.m_aSnap.begin(), Powerup.m_aSnap.end(),
		[](const auto &a, const auto &b) { return a.m_Id.value() < b.m_Id.value(); });

	SetData(Powerup);
	SetVisual(Powerup);

	m_vPowerups.push_back(Powerup);
	m_SpawnDelay = Server()->Tick() + Server()->TickSpeed() * (g_Config.m_SvPowerUpsSpawnDelay * 0.1f);
}

void CPowerUps::SetData(CPowerUp &Powerup)
{
	std::uniform_real_distribution<float> dis(0.0f, 100.0f);

	float RandomFloat = dis(Rng());

	if(RandomFloat < 1.5f)
		Powerup.m_Data.m_Type = EPowerUp::BOOST;
	else if(RandomFloat < 50.0f)
		Powerup.m_Data.m_Type = EPowerUp::XP;
	else
		Powerup.m_Data.m_Type = EPowerUp::MONEY;

	for(int i = 0; i < Server()->MaxClients(); i++)
	{
		if(!Server()->ClientIngame(i))
			continue;

		CPlayer *pPlayer = GameServer()->m_apPlayers[i];

		if(!pPlayer)
			continue;

		if(pPlayer->IsAfk())
			continue;

		CCharacter *pChr = pPlayer->GetCharacter();

		if(!pChr)
			continue;

		if(!GameServer()->m_aAccounts[i].m_LoggedIn)
			continue;

		if(pChr->Team() != TEAM_FLOCK)
			continue;

		Powerup.m_MaxCollections++;
	}
	if(Powerup.m_MaxCollections < 1)
		Powerup.m_MaxCollections = 1;

	constexpr int MinLifetime = 120;

	switch(Powerup.m_Data.m_Type)
	{
	case EPowerUp::XP:
		Powerup.m_Data.m_Value = GameServer()->RandGeometric(Rng(), 5, 50, 0.15);
		Powerup.m_Lifetime = MinLifetime + Powerup.m_Data.m_Value * 15;
		break;
	case EPowerUp::MONEY:
		Powerup.m_Data.m_Value = GameServer()->RandGeometric(Rng(), 3, 50, 0.19) * 25;
		Powerup.m_Lifetime = MinLifetime + Powerup.m_Data.m_Value * 0.45f;
		break;
	case EPowerUp::BOOST:
		Powerup.m_Data.m_Value = GameServer()->RandGeometric(Rng(), 10, 30, 0.22);
		Powerup.m_Lifetime = MinLifetime;
		Powerup.m_MaxCollections = 1;
		break;
	default:
		Powerup.m_Data.m_Value = 0;
		Powerup.m_Lifetime = 0;
	}
	Powerup.m_Lifetime *= Server()->TickSpeed();
}

void CPowerUps::SetVisual(CPowerUp &Powerup)
{
	const float Len = 28.0f;

	Powerup.m_aSnap.at(0).m_To = vec2(-Len, -Len);
	Powerup.m_aSnap.at(0).m_From = vec2(Len, -Len);

	Powerup.m_aSnap.at(1).m_To = vec2(Len, -Len);
	Powerup.m_aSnap.at(1).m_From = vec2(Len, Len);

	Powerup.m_aSnap.at(2).m_To = vec2(Len, Len);
	Powerup.m_aSnap.at(2).m_From = vec2(-Len, Len);

	Powerup.m_aSnap.at(3).m_To = vec2(-Len, Len);
	Powerup.m_aSnap.at(3).m_From = vec2(-Len, -Len);

	Powerup.m_aSnap.at(4).m_To = vec2(-Len, -Len);
	Powerup.m_aSnap.at(4).m_From = vec2(-Len, -Len);

	int LaserType;
	if(Powerup.m_Data.m_Type == EPowerUp::XP)
		LaserType = LASERTYPE_GUN;
	else if(Powerup.m_Data.m_Type == EPowerUp::MONEY)
		LaserType = LASERTYPE_SHOTGUN;
	else if(Powerup.m_Data.m_Type == EPowerUp::BOOST)
		LaserType = LASERTYPE_FREEZE;
	else
		LaserType = LASERTYPE_DOOR;

	for(CPowerUp::CSnapData &Snap : Powerup.m_aSnap)
		Snap.m_LaserType = LaserType;
}

void CPowerUps::FreeIds(CPowerUp &Powerup)
{
	if(g_Config.m_SvLogExtra >= 2)
		log_info("powerup", "Reset");

	if(Powerup.m_PickupId.has_value())
		Server()->SnapFreeId(Powerup.m_PickupId.value());
	Powerup.m_PickupId.reset();

	for(CPowerUp::CSnapData &Snap : Powerup.m_aSnap)
	{
		if(Snap.m_Id.has_value())
			Server()->SnapFreeId(Snap.m_Id.value());
		Snap.m_Id.reset();
	}
}

void CPowerUps::ClearPowerups()
{
	for(CPowerUp &Powerup : m_vPowerups)
		FreeIds(Powerup);
	m_vPowerups.clear();
	m_SpawnDelay = Server()->Tick() + Server()->TickSpeed() * 5;
}

static bool PointInSquare(vec2 Point, vec2 Center, float Size)
{
	return (Point.x > Center.x - Size && Point.x < Center.x + Size && Point.y > Center.y - Size && Point.y < Center.y + Size);
}

bool CPowerUps::TickPowerup(CPowerUp &Powerup)
{
	Powerup.m_Lifetime--;
	if(Powerup.m_Lifetime <= 0)
		return false;
	if(!g_Config.m_SvAccounts) // Powerups require accounts to store the data
		return false;

	IServer *pServer = Server();

	int NumCollected = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!pServer->ClientIngame(ClientId))
			continue;

		// Always run HandleClient for active clients so collection/address checks are evaluated
		// This ensures rejoined players with the same address are detected and prevented
		// from collecting repeatedly.
		HandleClient(Powerup, ClientId);

		if(Powerup.m_aClients[ClientId].m_Collected && Powerup.m_aClients[ClientId].m_WasLoggedIn)
			NumCollected++;

		if(NumCollected >= Powerup.m_MaxCollections)
			return false;
	}

	return true;
}

void CPowerUps::OnClientEnter(int ClientId)
{
	// A joining client may be a rejoin into a different slot, so settle their collected
	// flags once here instead of rediscovering them on every tick of every powerup.
	const int MaxClients = Server()->MaxClients();
	const NETADDR *pAddr = Server()->ClientAddr(ClientId);

	for(CPowerUp &Powerup : m_vPowerups)
	{
		if(Powerup.m_aClients[ClientId].m_Collected)
			continue;

		for(int i = 0; i < MaxClients; i++)
		{
			// Only slots that actually collected carry a meaningful stored address.
			if(i == ClientId || !Powerup.m_aClients[i].m_Collected)
				continue;
			if(net_addr_comp_noport(pAddr, &Powerup.m_aClients[i].m_Addr) != 0)
				continue;

			Powerup.m_aClients[ClientId].m_Collected = true;
			Powerup.m_aClients[ClientId].m_Addr = *pAddr;
			break;
		}
	}
}

void CPowerUps::HandleClient(CPowerUp &Powerup, int ClientId)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return;

	if((size_t)pChr->GetPlayer()->MultiMapIdx() != Powerup.m_MultiMapIdx)
		return; // Prevent collection across maps

	CGameTeams &Teams = GameServer()->m_pController->Teams();
	const int Team = pChr->Team();

	if((Team != TEAM_FLOCK && !g_Config.m_SvSoloServer) || Teams.IsPractice(Team))
		return;

	if(pPlayer->Acc()->m_Configs.m_HidePowerUps)
		return;

	if(Powerup.m_aClients[ClientId].m_Collected)
		return;
	if(!PointInSquare(Powerup.m_Pos, pChr->GetPos(), 54.0f))
		return;

	const int MaxClients = Server()->MaxClients();
	const NETADDR *pAddr = Server()->ClientAddr(ClientId);

	CClientMask TeamMask = pChr->TeamMask();
	for(int i = 0; i < MaxClients; i++)
	{
		CPlayer *pOtherPlayer = nullptr;
		if(Server()->ClientIngame(i))
			pOtherPlayer = GameServer()->m_apPlayers[i];

		if(pOtherPlayer && pOtherPlayer->Acc()->m_Configs.m_HidePowerUps)
			TeamMask.set(i, false);
	}

	CollectPowerup(Powerup, pPlayer);
	GameServer()->CreateSound(Powerup.m_Pos, SOUND_PICKUP_ARMOR, TeamMask);

	Powerup.m_aClients[ClientId].m_Collected = true;
	Powerup.m_aClients[ClientId].m_WasLoggedIn = pPlayer->Acc()->m_LoggedIn;
	Powerup.m_aClients[ClientId].m_Addr = *pAddr;

	for(int i = 0; i < MaxClients; i++)
	{
		if(i == ClientId || Powerup.m_aClients[i].m_Collected || !Server()->ClientIngame(i))
			continue;
		if(net_addr_comp_noport(pAddr, Server()->ClientAddr(i)) != 0)
			continue;

		Powerup.m_aClients[i].m_Collected = true;
		Powerup.m_aClients[i].m_Addr = *pAddr;
	}

	if(Powerup.m_Lifetime > Server()->TickSpeed() * 30)
		Powerup.m_Lifetime -= 10 * Server()->TickSpeed(); // Speed up disappearance after collection
}

void CPowerUps::CollectPowerup(const CPowerUp &Powerup, CPlayer *pPlayer)
{
	const bool HidePowerUps = pPlayer->Acc()->m_Configs.m_HidePowerUps;

	if(!pPlayer->Acc()->m_LoggedIn)
	{
		if(!HidePowerUps)
		{
			pPlayer->SendChat("You need to be logged in to collect Powerups");
			pPlayer->SendChat("/register <name> <pw>");
			pPlayer->SetHidePowerUps(true); // Only show powerups once
		}
		return;
	}

	const int ClientId = pPlayer->GetCid();
	const long Value = Powerup.m_Data.m_Value;
	const long MsgAmount = (long)(Value * pPlayer->StatMultiplier());

	switch(Powerup.m_Data.m_Type)
	{
	case EPowerUp::XP:
	{
		pPlayer->GiveXP(Value);
		if(!HidePowerUps)
			pPlayer->SendChatFmt("+%ldXP for collecting a PowerUp!", MsgAmount);
	}
	break;
	case EPowerUp::MONEY:
	{
		pPlayer->GiveMoney(Value);
		if(!HidePowerUps)
			pPlayer->SendChatFmt("+%ld%s for collecting a PowerUp!", MsgAmount, g_Config.m_SvCurrencyName);
	}
	break;
	case EPowerUp::BOOST:
	{
		constexpr int Minutes = 60;
		const int AddTicks = Server()->TickSpeed() * Minutes * 60;
		const float BoostAmount = Value * 0.1f;

		char aBuf[128];
		if(GameServer()->m_BoostData.m_Ticks > 0)
		{
			GameServer()->m_BoostData.m_Ticks += AddTicks;
			if(BoostAmount > GameServer()->m_BoostData.m_Boost)
			{
				GameServer()->m_BoostData.m_Boost = BoostAmount;
				str_format(aBuf, sizeof(aBuf), "Boost upgraded to %.1fx and extended by %d minutes by '%s'!", GameServer()->m_BoostData.m_Boost, Minutes, Server()->ClientName(ClientId));
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "+%d minutes of %.1fx Boost added by '%s'!", Minutes, GameServer()->m_BoostData.m_Boost, Server()->ClientName(ClientId));
			}
		}
		else
		{
			GameServer()->m_BoostData.m_Boost = BoostAmount;
			GameServer()->m_BoostData.m_Ticks = AddTicks;
			str_format(aBuf, sizeof(aBuf), "+%.1fx Boost for %d minutes collected by '%s'!", BoostAmount, Minutes, Server()->ClientName(ClientId));
		}
		GameServer()->SendChat(-1, 0, aBuf);
	}
	break;
	default:
		break;
	}
}


void CPowerUps::OnSnap(int ClientId, bool GlobalSnap, bool RecordingDemo)
{
	for(CPowerUp &Powerup : m_vPowerups)
	{
		// The blink state belongs to the powerup rather than to a viewer, but it only ever flips on
		// an exact tick boundary so doing it here stays in step for everyone
		if((Server()->Tick() - Powerup.m_StartTick) % Server()->TickSpeed() == 0)
			Powerup.m_Switch = !Powerup.m_Switch;

		SnapPowerup(Powerup, ClientId);
	}
}

void CPowerUps::SnapPowerup(const CPowerUp &Powerup, int SnappingClient)
{
	if(!Powerup.m_PickupId.has_value())
		return;

	if(NetworkClipped(GameServer(), SnappingClient, Powerup.m_Pos))
		return;

	if(SnappingClient != SERVER_DEMO_CLIENT)
	{
		CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
		if(!pSnapPlayer)
			return;

		if(pSnapPlayer->Acc()->m_Configs.m_HidePowerUps)
			return;

		const bool AuthedSpec = Server()->IsRconAuthed(SnappingClient) && pSnapPlayer->IsPaused();

		if(!AuthedSpec)
		{
			if(Powerup.m_aClients[SnappingClient].m_Collected)
			{
				return; // Hide already collected PowerUps
			}
			else
			{
				CCharacter *pChr = pSnapPlayer->GetCharacter();
				if(pChr && pChr->IsAlive())
				{
					CGameTeams &Teams = GameServer()->m_pController->Teams();
					const int Team = pChr->Team();
					if((Team != TEAM_FLOCK && !g_Config.m_SvSoloServer) || Teams.IsPractice(Team))
						return;
				}
			}
		}
	}

	// Make the powerup blink when about to disappear
	if(Powerup.m_Lifetime < Server()->TickSpeed() * 10 && (Server()->Tick() / (Server()->TickSpeed() / 4)) % 2 == 0)
		return;

	CGameTeams &Teams = GameServer()->m_pController->Teams();
	if(!Teams.SetMaskWithFlags(SnappingClient, Powerup.m_MultiMapIdx, TEAM_FLOCK, CGameTeams::IGNORE_SOLO))
		return;

	const int SnappingClientVersion = Server()->GetClientVersion(SnappingClient);
	const bool SixUp = Server()->IsSixup(SnappingClient);
	const CSnapContext Context(SnappingClientVersion, SixUp, SnappingClient);

	GameServer()->SnapPickup(Context, Powerup.m_PickupId.value(), Powerup.m_Pos, Powerup.m_Switch, 0, -1, PICKUPFLAG_NO_PREDICT);

	for(const CPowerUp::CSnapData &Snap : Powerup.m_aSnap)
	{
		if(!Snap.m_Id.has_value())
			continue;

		const vec2 To = Powerup.m_Pos + Snap.m_To;
		const vec2 From = Powerup.m_Pos + Snap.m_From;
		GameServer()->SnapLaserObject(Context, Snap.m_Id.value(), To, From, Server()->Tick(), -1, Snap.m_LaserType, -1, -1, LASERFLAG_NO_PREDICT);
	}
}
