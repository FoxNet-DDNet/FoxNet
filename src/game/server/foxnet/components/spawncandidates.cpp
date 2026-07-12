#include "spawncandidates.h"

#include "zones/zone.h"
#include "zones/zonemanager.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/quad_data.h>
#include <game/server/entities/character.h>
#include <game/server/entity.h>
#include <game/server/foxnet/component.h>
#include <game/server/foxnet/entities/powerup.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

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

	struct SSpawnBuildData
	{
		int m_Width = 0;
		int m_Height = 0;
		std::vector<int> m_GameTiles;
		std::vector<int> m_FrontTiles;
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
		Data.m_SwitchTiles.resize(Size);

		const CTeleTile *pTele = pCollision->TeleLayer();
		if(pTele)
			Data.m_TeleTiles.assign(pTele, pTele + Size);

		for(int i = 0; i < Size; ++i)
		{
			Data.m_GameTiles[i] = pCollision->GetTileIndex(i);
			Data.m_FrontTiles[i] = pCollision->GetFrontTileIndex(i);
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
			for(const IZone *pZone : pZoneManager->Zones(EZoneType::Unfreeze))
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

					// Non-freeze, non-blocked tile: the player thaws and lands here.
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

CSpawnCandidates::~CSpawnCandidates()
{
	OnShutdown(nullptr);
}

void CSpawnCandidates::QueueRebuildSnapshot(size_t MapIdx)
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

void CSpawnCandidates::RebuildAsync(size_t MapIdx)
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

void CSpawnCandidates::OnMapLoad(size_t MapIdx)
{
	if(MapIdx != DefaultMapIndex)
		return; // ignore other map indexes for now

	RebuildAsync(MapIdx);
}

void CSpawnCandidates::OnMapUnload(size_t MapIdx)
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

void CSpawnCandidates::OnShutdown(void *pPersistentData)
{
	auto pOldShared = std::move(m_pShared);
	m_pShared = std::make_shared<SSharedState>();

	CLockScope Lock(pOldShared->m_CacheLock);
	pOldShared->m_CachedCandidates.clear();
	pOldShared->m_RebuildGenerations.clear();
	pOldShared->m_RebuildDeferred = false;
}

void CSpawnCandidates::Rebuild(size_t MapIdx)
{
	RebuildAsync(MapIdx);
}

bool CSpawnCandidates::TryPickCachedCandidate(size_t MapIdx, vec2 &Out) const
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

size_t CSpawnCandidates::SpawnCandidateCount(size_t MapIdx) const
{
	auto pShared = m_pShared;
	const CMultiMaps *pMultiMap = MultiMaps(MapIdx);
	if(!pMultiMap)
		return 0;

	CLockScope Lock(pShared->m_CacheLock);
	auto It = pShared->m_CachedCandidates.find(pMultiMap);
	return It != pShared->m_CachedCandidates.end() ? It->second.size() : 0;
}

void CSpawnCandidates::OnTick()
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

	if(!g_Config.m_SvPowerUps)
		return;
	if(GameServer()->GlobalTuning(DefaultMapIndex)->m_TeleGrenade)
		return; // nah, too much work to make them work with tele grenades
	if(!g_Config.m_SvAccounts)
		return; // Powerups require accounts to store the data
	if(GameServer()->m_vPowerups.size() >= g_Config.m_SvPowerUpsMax)
		return;
	if(GameServer()->m_PowerUpDelay > Server()->Tick())
		return;

	const std::optional<vec2> RandomPos = GetRandomAccessiblePos();
	if(!RandomPos.has_value())
	{
		GameServer()->m_PowerUpDelay = Server()->Tick() + Server()->TickSpeed();
		return;
	}

	CPowerUp *NewPowerUp = new CPowerUp(&GameServer()->m_World, DefaultMapIndex, RandomPos.value());

	GameServer()->m_vPowerups.push_back(NewPowerUp);
	GameServer()->m_PowerUpDelay = Server()->Tick() + Server()->TickSpeed() * (g_Config.m_SvPowerUpsSpawnDelay * 0.1f);
}

std::optional<vec2> CSpawnCandidates::GetRandomAccessiblePos()
{
	const auto Dist2 = [](const vec2 &a, const vec2 &b) {
		const float DistX = a.x - b.x;
		const float DistY = a.y - b.y;
		return DistX * DistX + DistY * DistY;
	};

	constexpr float TileSize = 32.0f;
	constexpr float MinPlayerDist = TileSize * 25.0f;

	for(int Tries = 0; Tries < 16; ++Tries)
	{
		vec2 Pos;
		if(!TryPickCachedCandidate(DefaultMapIndex, Pos))
			return std::nullopt;

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

	float BestScore = -1.0f;
	vec2 BestPos = vec2(0.0f, 0.0f);
	for(int k = 0; k < 32; ++k)
	{
		vec2 Pos;
		if(!TryPickCachedCandidate(DefaultMapIndex, Pos))
			break;

		float MinDist2 = std::numeric_limits<float>::infinity();
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