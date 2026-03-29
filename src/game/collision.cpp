/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "gamecore.h"

#include <antibot/antibot_data.h>

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/map.h>
#include <engine/shared/config.h>

#include <game/collision.h>
#include <game/envelopeaccess.h>
#include <game/layers.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <random>
#include <utility>
#include <vector>

vec2 ClampVel(int MoveRestriction, vec2 Vel)
{
	if(Vel.x > 0 && (MoveRestriction & CANTMOVE_RIGHT))
	{
		Vel.x = 0;
	}
	if(Vel.x < 0 && (MoveRestriction & CANTMOVE_LEFT))
	{
		Vel.x = 0;
	}
	if(Vel.y > 0 && (MoveRestriction & CANTMOVE_DOWN))
	{
		Vel.y = 0;
	}
	if(Vel.y < 0 && (MoveRestriction & CANTMOVE_UP))
	{
		Vel.y = 0;
	}
	return Vel;
}

static int QuadTypeToTile(EZoneType QuadType)
{
	switch(QuadType)
	{
	case EZoneType::StopA: return TILE_STOPA;
	case EZoneType::Hookable: return TILE_SOLID;
	case EZoneType::Unhookable: return TILE_NOHOOK;
	default: return 0;
	}
}

CCollision::CCollision()
{
	m_pDoor = nullptr;
	Unload();
}

CCollision::~CCollision()
{
	Unload();
}

void CCollision::Init(class CLayers *pLayers)
{
	Unload();

	m_pLayers = pLayers;
	m_Width = m_pLayers->GameLayer()->m_Width;
	m_Height = m_pLayers->GameLayer()->m_Height;
	m_pTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->GameLayer()->m_Data));

	if(m_pLayers->TeleLayer())
	{
		unsigned int Size = m_pLayers->Map()->GetDataSize(m_pLayers->TeleLayer()->m_Tele);
		if(Size >= (size_t)m_Width * m_Height * sizeof(CTeleTile))
			m_pTele = static_cast<CTeleTile *>(m_pLayers->Map()->GetData(m_pLayers->TeleLayer()->m_Tele));
	}

	if(m_pLayers->SpeedupLayer())
	{
		unsigned int Size = m_pLayers->Map()->GetDataSize(m_pLayers->SpeedupLayer()->m_Speedup);
		if(Size >= (size_t)m_Width * m_Height * sizeof(CSpeedupTile))
			m_pSpeedup = static_cast<CSpeedupTile *>(m_pLayers->Map()->GetData(m_pLayers->SpeedupLayer()->m_Speedup));
	}

	if(m_pLayers->SwitchLayer())
	{
		unsigned int Size = m_pLayers->Map()->GetDataSize(m_pLayers->SwitchLayer()->m_Switch);
		if(Size >= (size_t)m_Width * m_Height * sizeof(CSwitchTile))
			m_pSwitch = static_cast<CSwitchTile *>(m_pLayers->Map()->GetData(m_pLayers->SwitchLayer()->m_Switch));

		m_pDoor = new CDoorTile[m_Width * m_Height];
		mem_zero(m_pDoor, (size_t)m_Width * m_Height * sizeof(CDoorTile));
	}

	if(m_pLayers->TuneLayer())
	{
		unsigned int Size = m_pLayers->Map()->GetDataSize(m_pLayers->TuneLayer()->m_Tune);
		if(Size >= (size_t)m_Width * m_Height * sizeof(CTuneTile))
			m_pTune = static_cast<CTuneTile *>(m_pLayers->Map()->GetData(m_pLayers->TuneLayer()->m_Tune));
	}

	if(m_pLayers->FrontLayer())
	{
		unsigned int Size = m_pLayers->Map()->GetDataSize(m_pLayers->FrontLayer()->m_Front);
		if(Size >= (size_t)m_Width * m_Height * sizeof(CTile))
			m_pFront = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->FrontLayer()->m_Front));
	}

	for(int i = 0; i < m_Width * m_Height; i++)
	{
		int Index;
		if(m_pSwitch)
		{
			if(m_pSwitch[i].m_Number > m_HighestSwitchNumber)
				m_HighestSwitchNumber = m_pSwitch[i].m_Number;

			if(m_pSwitch[i].m_Number)
				m_pDoor[i].m_Number = m_pSwitch[i].m_Number;
			else
				m_pDoor[i].m_Number = 0;

			Index = m_pSwitch[i].m_Type;

			if(Index <= TILE_NPH_ENABLE)
			{
				if((Index >= TILE_JUMP && Index <= TILE_SUBTRACT_TIME) || Index == TILE_ALLOW_TELE_GUN || Index == TILE_ALLOW_BLUE_TELE_GUN)
					m_pSwitch[i].m_Type = Index;
				else
					m_pSwitch[i].m_Type = 0;
			}
		}
	}

	if(m_pTele)
	{
		for(int i = 0; i < m_Width * m_Height; i++)
		{
			int Number = m_pTele[i].m_Number;
			int Type = m_pTele[i].m_Type;
			if(Number > 0)
			{
				if(Type == TILE_TELEIN)
				{
					m_TeleIns[Number - 1].emplace_back(i % m_Width * 32.0f + 16.0f, i / m_Width * 32.0f + 16.0f);
				}
				else if(Type == TILE_TELEOUT)
				{
					m_TeleOuts[Number - 1].emplace_back(i % m_Width * 32.0f + 16.0f, i / m_Width * 32.0f + 16.0f);
				}
				else if(Type == TILE_TELECHECKOUT)
				{
					m_TeleCheckOuts[Number - 1].emplace_back(i % m_Width * 32.0f + 16.0f, i / m_Width * 32.0f + 16.0f);
				}
				else if(Type)
				{
					m_TeleOthers[Number - 1].emplace_back(i % m_Width * 32.0f + 16.0f, i / m_Width * 32.0f + 16.0f);
				}
			}
		}
	}
}

void CCollision::InitSpawnCandidates()
{
	dbg_assert(m_pLayers, "m_pLayers must be valid");
	BuildSpawnCandidates();
	if(m_SpawnCandidates.size() < 500)
		m_SpawnCandidates.clear(); // Too few spawn points
	log_info("spawn-candidates", "found %d spawn point%s", (int)m_SpawnCandidates.size(), m_SpawnCandidates.size() == 1 ? "" : "s");
}

void CCollision::Unload()
{
	m_pTiles = nullptr;
	m_Width = 0;
	m_Height = 0;
	m_pLayers = nullptr;

	m_HighestSwitchNumber = 0;

	m_TeleIns.clear();
	m_TeleOuts.clear();
	m_TeleCheckOuts.clear();
	m_TeleOthers.clear();

	m_pTele = nullptr;
	m_pSpeedup = nullptr;
	m_pFront = nullptr;
	m_pSwitch = nullptr;
	m_pTune = nullptr;
	delete[] m_pDoor;
	m_pDoor = nullptr;
	// <FoxNet
	m_SpawnCandidates.clear();
	UnloadQuads();
	// FoxNet>
}

void CCollision::FillAntibot(CAntibotMapData *pMapData) const
{
	pMapData->m_Width = m_Width;
	pMapData->m_Height = m_Height;
	pMapData->m_pTiles = (unsigned char *)malloc((size_t)m_Width * m_Height);
	for(int i = 0; i < m_Width * m_Height; i++)
	{
		pMapData->m_pTiles[i] = 0;
		if(m_pTiles[i].m_Index >= TILE_SOLID && m_pTiles[i].m_Index <= TILE_NOLASER)
		{
			pMapData->m_pTiles[i] = m_pTiles[i].m_Index;
		}
	}
}

enum
{
	MR_DIR_HERE = 0,
	MR_DIR_RIGHT,
	MR_DIR_DOWN,
	MR_DIR_LEFT,
	MR_DIR_UP,
	NUM_MR_DIRS
};

static int GetMoveRestrictionsRaw(int Direction, int Tile, int Flags)
{
	Flags = Flags & (TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE);
	switch(Tile)
	{
	case TILE_STOP:
		switch(Flags)
		{
		case ROTATION_0: return CANTMOVE_DOWN;
		case ROTATION_90: return CANTMOVE_LEFT;
		case ROTATION_180: return CANTMOVE_UP;
		case ROTATION_270: return CANTMOVE_RIGHT;

		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_0): return CANTMOVE_UP;
		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_90): return CANTMOVE_RIGHT;
		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_180): return CANTMOVE_DOWN;
		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_270): return CANTMOVE_LEFT;
		}
		break;
	case TILE_STOPS:
		switch(Flags)
		{
		case ROTATION_0:
		case ROTATION_180:
		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_0):
		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_180):
			return CANTMOVE_DOWN | CANTMOVE_UP;
		case ROTATION_90:
		case ROTATION_270:
		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_90):
		case static_cast<int>(TILEFLAG_YFLIP) ^ static_cast<int>(ROTATION_270):
			return CANTMOVE_LEFT | CANTMOVE_RIGHT;
		}
		break;
	case TILE_STOPA:
		return CANTMOVE_LEFT | CANTMOVE_RIGHT | CANTMOVE_UP | CANTMOVE_DOWN;
	}
	return 0;
}

static int GetMoveRestrictionsMask(int Direction)
{
	switch(Direction)
	{
	case MR_DIR_HERE: return 0;
	case MR_DIR_RIGHT: return CANTMOVE_RIGHT;
	case MR_DIR_DOWN: return CANTMOVE_DOWN;
	case MR_DIR_LEFT: return CANTMOVE_LEFT;
	case MR_DIR_UP: return CANTMOVE_UP;
	default: dbg_assert_failed("Invalid Direction: %d", Direction);
	}
}

static int GetMoveRestrictions(int Direction, int Tile, int Flags)
{
	int Result = GetMoveRestrictionsRaw(Direction, Tile, Flags);
	// Generally, stoppers only have an effect if they block us from moving
	// *onto* them. The one exception is one-way blockers, they can also
	// block us from moving if we're on top of them.
	if(Direction == MR_DIR_HERE && Tile == TILE_STOP)
	{
		return Result;
	}
	return Result & GetMoveRestrictionsMask(Direction);
}

int CCollision::GetMoveRestrictions(CALLBACK_SWITCHACTIVE pfnSwitchActive, void *pUser, vec2 Pos, float Distance, int OverrideCenterTileIndex) const
{
	static const vec2 DIRECTIONS[NUM_MR_DIRS] =
		{
			vec2(0, 0),
			vec2(1, 0),
			vec2(0, 1),
			vec2(-1, 0),
			vec2(0, -1)};
	dbg_assert(0.0f <= Distance && Distance <= 32.0f, "Invalid Distance: %f", Distance);
	int Restrictions = 0;
	for(int d = 0; d < NUM_MR_DIRS; d++)
	{
		vec2 ModPos = Pos + DIRECTIONS[d] * Distance;
		int ModMapIndex = GetPureMapIndex(ModPos);
		if(d == MR_DIR_HERE && OverrideCenterTileIndex >= 0)
		{
			ModMapIndex = OverrideCenterTileIndex;
		}
		for(int Front = 0; Front < 2; Front++)
		{
			int Tile;
			int Flags;
			if(!Front)
			{
				Tile = GetTileIndex(ModMapIndex);
				Flags = GetTileFlags(ModMapIndex);
			}
			else
			{
				Tile = GetFrontTileIndex(ModMapIndex);
				Flags = GetFrontTileFlags(ModMapIndex);
			}
			Restrictions |= ::GetMoveRestrictions(d, Tile, Flags);
		}
		if(pfnSwitchActive)
		{
			CDoorTile DoorTile;
			GetDoorTile(ModMapIndex, &DoorTile);
			if(in_range(DoorTile.m_Number, 0, m_HighestSwitchNumber) &&
				pfnSwitchActive(DoorTile.m_Number, pUser))
			{
				Restrictions |= ::GetMoveRestrictions(d, DoorTile.m_Index, DoorTile.m_Flags);
			}
		}
	}
	return Restrictions;
}

int CCollision::GetTile(int x, int y) const
{
	if(!m_pTiles)
		return 0;

	int Nx = std::clamp(x / 32, 0, m_Width - 1);
	int Ny = std::clamp(y / 32, 0, m_Height - 1);
	const int Index = Ny * m_Width + Nx;

	if(m_pTiles[Index].m_Index >= TILE_SOLID && m_pTiles[Index].m_Index <= TILE_NOLASER)
		return m_pTiles[Index].m_Index;
	return 0;
}

// TODO: rewrite this smarter!
int CCollision::IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
{
	float Distance = distance(Pos0, Pos1);
	int End(Distance + 1);
	vec2 Last = Pos0;
	for(int i = 0; i <= End; i++)
	{
		float a = i / (float)End;
		vec2 Pos = mix(Pos0, Pos1, a);
		// Temporary position for checking collision
		int ix = round_to_int(Pos.x);
		int iy = round_to_int(Pos.y);

		const CQuadData *pHitQuad = nullptr;
		if(CheckPoint(ix, iy, &pHitQuad))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;

			int Tile = GetCollisionAt(ix, iy);
			if(pHitQuad)
				Tile = QuadTypeToTile(pHitQuad->m_Type);
			return Tile;
		}

		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

int CCollision::IntersectLineTeleHook(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision, int *pTeleNr, const CQuadData **ppOutQuad) const
{
	if(ppOutQuad)
		*ppOutQuad = nullptr;

	float Distance = distance(Pos0, Pos1);
	int End(Distance + 1);
	vec2 Last = Pos0;
	int dx = 0, dy = 0; // Offset for checking the "through" tile
	ThroughOffset(Pos0, Pos1, &dx, &dy);
	for(int i = 0; i <= End; i++)
	{
		float a = i / (float)End;
		vec2 Pos = mix(Pos0, Pos1, a);
		// Temporary position for checking collision
		int ix = round_to_int(Pos.x);
		int iy = round_to_int(Pos.y);

		int Index = GetPureMapIndex(Pos);
		if(pTeleNr)
		{
			if(g_Config.m_SvOldTeleportHook)
				*pTeleNr = IsTeleport(Index);
			else
				*pTeleNr = IsTeleportHook(Index);
		}
		if(pTeleNr && *pTeleNr)
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return TILE_TELEINHOOK;
		}

		int Hit = 0;
		const CQuadData *pHitQuad = nullptr;
		if(CheckPoint(ix, iy, &pHitQuad))
		{
			if(!IsThrough(ix, iy, dx, dy, Pos0, Pos1))
			{
				Hit = GetCollisionAt(ix, iy);

				if(pHitQuad)
				{
					if(ppOutQuad)
						*ppOutQuad = pHitQuad;
					Hit = QuadTypeToTile(pHitQuad->m_Type);
				}
			}
		}
		else if(IsHookBlocker(ix, iy, Pos0, Pos1))
		{
			Hit = TILE_NOHOOK;
		}

		if(Hit)
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return Hit;
		}

		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

int CCollision::IntersectLineTeleWeapon(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision, int *pTeleNr) const
{
	float Distance = distance(Pos0, Pos1);
	int End(Distance + 1);
	vec2 Last = Pos0;
	for(int i = 0; i <= End; i++)
	{
		float a = i / (float)End;
		vec2 Pos = mix(Pos0, Pos1, a);
		// Temporary position for checking collision
		int ix = round_to_int(Pos.x);
		int iy = round_to_int(Pos.y);

		int Index = GetPureMapIndex(Pos);
		if(pTeleNr)
		{
			if(g_Config.m_SvOldTeleportWeapons)
				*pTeleNr = IsTeleport(Index);
			else
				*pTeleNr = IsTeleportWeapon(Index);
		}
		if(pTeleNr && *pTeleNr)
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return TILE_TELEINWEAPON;
		}

		if(CheckPoint(ix, iy))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return GetCollisionAt(ix, iy);
		}

		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

// TODO: OPT: rewrite this smarter!
void CCollision::MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces) const
{
	if(pBounces)
		*pBounces = 0;

	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	if(CheckPoint(Pos + Vel))
	{
		int Affected = 0;
		if(CheckPoint(Pos.x + Vel.x, Pos.y))
		{
			pInoutVel->x *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(CheckPoint(Pos.x, Pos.y + Vel.y))
		{
			pInoutVel->y *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(Affected == 0)
		{
			pInoutVel->x *= -Elasticity;
			pInoutVel->y *= -Elasticity;
		}
	}
	else
	{
		*pInoutPos = Pos + Vel;
	}
}

bool CCollision::TestBox(vec2 Pos, vec2 Size, const CQuadData **ppHitQuad) const
{
	Size *= 0.5f;
	if(CheckPoint(Pos.x - Size.x, Pos.y - Size.y, ppHitQuad))
		return true;
	if(CheckPoint(Pos.x + Size.x, Pos.y - Size.y, ppHitQuad))
		return true;
	if(CheckPoint(Pos.x - Size.x, Pos.y + Size.y, ppHitQuad))
		return true;
	if(CheckPoint(Pos.x + Size.x, Pos.y + Size.y, ppHitQuad))
		return true;
	return false;
}

bool CCollision::IsOnGround(vec2 Pos, float Size, const CQuadData **ppHitQuad) const
{
	if(CheckPoint(Pos.x + Size / 2, Pos.y + Size / 2 + 5, ppHitQuad))
		return true;
	if(CheckPoint(Pos.x - Size / 2, Pos.y + Size / 2 + 5, ppHitQuad))
		return true;

	return false;
}

bool CCollision::MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, vec2 Elasticity, bool *pGrounded) const
{
	// do the move
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;

	float Distance = length(Vel);
	int Max = (int)Distance;

	bool ReturnValue = false;

	auto QuadStepDeltaAt = [&](vec2 Probe, float StepFraction, const CQuadData **ppHitQuad) -> vec2 {
		vec2 Delta = vec2(0, 0);
		//if(!g_Config.m_SvMovingTiles)
		//	return Delta;
		if(!m_HasSolidQuads)
			return Delta;
		if(m_vQuads.empty() || m_vNextQuads.size() != m_vQuads.size())
			return Delta;

		vec2 FeetPos = vec2(Probe.x, Probe.y + Size.y * 0.55f);

		const CQuadData *pQuad = GetQuadAt(FeetPos);
		if(!pQuad)
			return Delta;

		if(ppHitQuad)
			*ppHitQuad = pQuad;

		const CQuadData *pBase = m_vQuads.data();
		if(pQuad - pBase < 0 || (size_t)(pQuad - pBase) >= m_vNextQuads.size())
			return Delta;
		const size_t Idx = pQuad - pBase;

		const vec2 CurCenter = vec2(round_to_int(m_vQuads[Idx].m_Pos[4].x), round_to_int(m_vQuads[Idx].m_Pos[4].y));
		const vec2 NextCenter = vec2(round_to_int(m_vNextQuads[Idx].m_Pos[4].x), round_to_int(m_vNextQuads[Idx].m_Pos[4].y));

		// Keep this only for left/right motion while standing on top.
		if(distance(NextCenter, CurCenter) > 32.0f)
			return Delta;
		if(absolute(NextCenter.y - CurCenter.y) > 1.0f)
			return Delta;

		Delta.x += (NextCenter.x - CurCenter.x) * StepFraction;
		return Delta;
	};

	const CQuadData *HitQuad = nullptr;

	// If we are not moving ourselves but are on a moving quad, we still want to be pushed along.
	// Peek full-frame quad displacement to decide whether to run at least one step.
	const vec2 FullQuadDelta = QuadStepDeltaAt(Pos, 1.0f, &HitQuad);
	if(HitQuad)
		ReturnValue = true;

	if(Distance > 0.00001f || FullQuadDelta != vec2(0, 0))
	{
		float Fraction = 1.f / (float)(Max + 1);
		float ElasticityX = std::clamp(Elasticity.x, -1.0f, 1.0f);
		float ElasticityY = std::clamp(Elasticity.y, -1.0f, 1.0f);

		for(int i = 0; i <= Max; i++)
		{
			if(Vel == vec2(0, 0) && FullQuadDelta == vec2(0, 0))
				break;

			const vec2 QuadDelta = QuadStepDeltaAt(Pos, Fraction, &HitQuad);
			if(HitQuad)
				ReturnValue = true;
			vec2 NewPos = Pos + Vel * Fraction;
			NewPos += QuadDelta;

			if(NewPos == Pos)
				break;

			if(TestBox(vec2(NewPos.x, NewPos.y), Size, &HitQuad))
			{
				if(HitQuad)
					ReturnValue = true;
				int Hits = 0;
				if(TestBox(vec2(Pos.x, NewPos.y), Size, &HitQuad))
				{
					if(HitQuad)
						ReturnValue = true;
					if(pGrounded && (ElasticityY > 0 || HitQuad) && Vel.y > 0)
						*pGrounded = true;
					NewPos.y = Pos.y;
					Vel.y *= -ElasticityY;
					Hits++;
				}

				if(TestBox(vec2(NewPos.x, Pos.y), Size, &HitQuad))
				{
					if(HitQuad)
						ReturnValue = true;
					NewPos.x = Pos.x;
					Vel.x *= -ElasticityX;
					Hits++;
				}

				// neither of the tests got a collision.
				// this is a real _corner case_!
				if(Hits == 0)
				{
					if(pGrounded && (ElasticityY > 0 || HitQuad) && Vel.y > 0)
						*pGrounded = true;
					NewPos.y = Pos.y;
					Vel.y *= -ElasticityY;
					NewPos.x = Pos.x;
					Vel.x *= -ElasticityX;
				}
			}

			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;

	return ReturnValue;
}

// DDRace
int CCollision::IsSolid(int x, int y, const CQuadData **ppHitQuad) const
{
	if(ppHitQuad)
		*ppHitQuad = nullptr;
	if(m_HasSolidQuads)
	{
		const CQuadData *pQuad = GetQuadAt(vec2((float)x, (float)y));
		if(pQuad)
		{
			if(ppHitQuad)
				*ppHitQuad = pQuad;

			if(pQuad->m_Type == EZoneType::Hookable)
				return TILE_SOLID;
			else if(pQuad->m_Type == EZoneType::Unhookable)
				return TILE_NOHOOK;
		}
	}
	int Index = GetTile(x, y);
	return Index == TILE_SOLID || Index == TILE_NOHOOK;
}

bool CCollision::IsThrough(int x, int y, int OffsetX, int OffsetY, vec2 Pos0, vec2 Pos1) const
{
	const int Index = GetPureMapIndex(x, y);
	if(m_pFront && (m_pFront[Index].m_Index == TILE_THROUGH_ALL || m_pFront[Index].m_Index == TILE_THROUGH_CUT))
		return true;
	if(m_pFront && m_pFront[Index].m_Index == TILE_THROUGH_DIR && ((m_pFront[Index].m_Flags == ROTATION_0 && Pos0.y > Pos1.y) || (m_pFront[Index].m_Flags == ROTATION_90 && Pos0.x < Pos1.x) || (m_pFront[Index].m_Flags == ROTATION_180 && Pos0.y < Pos1.y) || (m_pFront[Index].m_Flags == ROTATION_270 && Pos0.x > Pos1.x)))
		return true;
	const int OffsetIndex = GetPureMapIndex(x + OffsetX, y + OffsetY);
	return m_pTiles[OffsetIndex].m_Index == TILE_THROUGH || (m_pFront && m_pFront[OffsetIndex].m_Index == TILE_THROUGH);
}

bool CCollision::IsHookBlocker(int x, int y, vec2 Pos0, vec2 Pos1) const
{
	const int Index = GetPureMapIndex(x, y);
	if(m_pTiles[Index].m_Index == TILE_THROUGH_ALL || (m_pFront && m_pFront[Index].m_Index == TILE_THROUGH_ALL))
		return true;
	if(m_pTiles[Index].m_Index == TILE_THROUGH_DIR && ((m_pTiles[Index].m_Flags == ROTATION_0 && Pos0.y < Pos1.y) ||
								  (m_pTiles[Index].m_Flags == ROTATION_90 && Pos0.x > Pos1.x) ||
								  (m_pTiles[Index].m_Flags == ROTATION_180 && Pos0.y > Pos1.y) ||
								  (m_pTiles[Index].m_Flags == ROTATION_270 && Pos0.x < Pos1.x)))
		return true;
	if(m_pFront && m_pFront[Index].m_Index == TILE_THROUGH_DIR && ((m_pFront[Index].m_Flags == ROTATION_0 && Pos0.y < Pos1.y) || (m_pFront[Index].m_Flags == ROTATION_90 && Pos0.x > Pos1.x) || (m_pFront[Index].m_Flags == ROTATION_180 && Pos0.y > Pos1.y) || (m_pFront[Index].m_Flags == ROTATION_270 && Pos0.x < Pos1.x)))
		return true;
	return false;
}

int CCollision::IsWallJump(int Index) const
{
	if(Index < 0)
		return 0;

	return m_pTiles[Index].m_Index == TILE_WALLJUMP;
}

int CCollision::IsNoLaser(int x, int y) const
{
	return (CCollision::GetTile(x, y) == TILE_NOLASER);
}

int CCollision::IsFrontNoLaser(int x, int y) const
{
	return (CCollision::GetFrontTile(x, y) == TILE_NOLASER);
}

int CCollision::IsTeleport(int Index) const
{
	if(Index < 0 || !m_pTele)
		return 0;

	if(m_pTele[Index].m_Type == TILE_TELEIN)
		return m_pTele[Index].m_Number;

	return 0;
}

int CCollision::IsEvilTeleport(int Index) const
{
	if(Index < 0)
		return 0;
	if(!m_pTele)
		return 0;

	if(m_pTele[Index].m_Type == TILE_TELEINEVIL)
		return m_pTele[Index].m_Number;

	return 0;
}

bool CCollision::IsCheckTeleport(int Index) const
{
	if(Index < 0 || !m_pTele)
		return false;
	return m_pTele[Index].m_Type == TILE_TELECHECKIN;
}

bool CCollision::IsCheckEvilTeleport(int Index) const
{
	if(Index < 0 || !m_pTele)
		return false;
	return m_pTele[Index].m_Type == TILE_TELECHECKINEVIL;
}

int CCollision::IsTeleCheckpoint(int Index) const
{
	if(Index < 0)
		return 0;

	if(!m_pTele)
		return 0;

	if(m_pTele[Index].m_Type == TILE_TELECHECK)
		return m_pTele[Index].m_Number;

	return 0;
}

int CCollision::IsTeleportWeapon(int Index) const
{
	if(Index < 0 || !m_pTele)
		return 0;

	if(m_pTele[Index].m_Type == TILE_TELEINWEAPON)
		return m_pTele[Index].m_Number;

	return 0;
}

int CCollision::IsTeleportHook(int Index) const
{
	if(Index < 0 || !m_pTele)
		return 0;

	if(m_pTele[Index].m_Type == TILE_TELEINHOOK)
		return m_pTele[Index].m_Number;

	return 0;
}

int CCollision::IsSpeedup(int Index) const
{
	if(Index < 0 || !m_pSpeedup)
		return 0;

	if(m_pSpeedup[Index].m_Force > 0)
		return Index;

	return 0;
}

int CCollision::IsTune(int Index) const
{
	if(Index < 0 || !m_pTune)
		return 0;

	if(m_pTune[Index].m_Type)
		return m_pTune[Index].m_Number;

	return 0;
}

void CCollision::GetSpeedup(int Index, vec2 *pDir, int *pForce, int *pMaxSpeed, int *pType) const
{
	if(Index < 0 || !m_pSpeedup)
		return;
	float Angle = m_pSpeedup[Index].m_Angle * (pi / 180.0f);
	*pForce = m_pSpeedup[Index].m_Force;
	*pType = m_pSpeedup[Index].m_Type;
	*pDir = direction(Angle);
	if(pMaxSpeed)
		*pMaxSpeed = m_pSpeedup[Index].m_MaxSpeed;
}

int CCollision::GetSwitchType(int Index) const
{
	if(Index < 0 || !m_pSwitch)
		return 0;

	if(m_pSwitch[Index].m_Type > 0)
		return m_pSwitch[Index].m_Type;

	return 0;
}

int CCollision::GetSwitchNumber(int Index) const
{
	if(Index < 0 || !m_pSwitch)
		return 0;

	if(m_pSwitch[Index].m_Type > 0 && m_pSwitch[Index].m_Number > 0)
		return m_pSwitch[Index].m_Number;

	return 0;
}

int CCollision::GetSwitchDelay(int Index) const
{
	if(Index < 0 || !m_pSwitch)
		return 0;

	if(m_pSwitch[Index].m_Type > 0)
		return m_pSwitch[Index].m_Delay;

	return 0;
}

int CCollision::MoverSpeed(int x, int y, vec2 *pSpeed) const
{
	int Nx = std::clamp(x / 32, 0, m_Width - 1);
	int Ny = std::clamp(y / 32, 0, m_Height - 1);
	int Index = m_pTiles[Ny * m_Width + Nx].m_Index;

	if(Index != TILE_CP && Index != TILE_CP_F)
	{
		return 0;
	}

	vec2 Target;
	switch(m_pTiles[Ny * m_Width + Nx].m_Flags)
	{
	case ROTATION_0:
		Target.x = 0.0f;
		Target.y = -4.0f;
		break;
	case ROTATION_90:
		Target.x = 4.0f;
		Target.y = 0.0f;
		break;
	case ROTATION_180:
		Target.x = 0.0f;
		Target.y = 4.0f;
		break;
	case ROTATION_270:
		Target.x = -4.0f;
		Target.y = 0.0f;
		break;
	default:
		Target = vec2(0.0f, 0.0f);
		break;
	}
	if(Index == TILE_CP_F)
	{
		Target *= 4.0f;
	}
	*pSpeed = Target;
	return Index;
}

int CCollision::GetPureMapIndex(float x, float y) const
{
	int Nx = std::clamp(round_to_int(x) / 32, 0, m_Width - 1);
	int Ny = std::clamp(round_to_int(y) / 32, 0, m_Height - 1);
	return Ny * m_Width + Nx;
}

bool CCollision::TileExists(int Index) const
{
	if(Index < 0)
		return false;

	if((m_pTiles[Index].m_Index >= TILE_FREEZE && m_pTiles[Index].m_Index <= TILE_TELE_LASER_DISABLE) || (m_pTiles[Index].m_Index >= TILE_LFREEZE && m_pTiles[Index].m_Index <= TILE_LUNFREEZE))
		return true;
	if(m_pFront && ((m_pFront[Index].m_Index >= TILE_FREEZE && m_pFront[Index].m_Index <= TILE_TELE_LASER_DISABLE) || (m_pFront[Index].m_Index >= TILE_LFREEZE && m_pFront[Index].m_Index <= TILE_LUNFREEZE)))
		return true;
	if(m_pTele && (m_pTele[Index].m_Type == TILE_TELEIN || m_pTele[Index].m_Type == TILE_TELEINEVIL || m_pTele[Index].m_Type == TILE_TELECHECKINEVIL || m_pTele[Index].m_Type == TILE_TELECHECK || m_pTele[Index].m_Type == TILE_TELECHECKIN))
		return true;
	if(m_pSpeedup && m_pSpeedup[Index].m_Force > 0)
		return true;
	if(m_pDoor && m_pDoor[Index].m_Index)
		return true;
	if(m_pSwitch && m_pSwitch[Index].m_Type)
		return true;
	if(m_pTune && m_pTune[Index].m_Type)
		return true;
	return TileExistsNext(Index);
}

bool CCollision::TileExistsNext(int Index) const
{
	if(Index < 0)
		return false;
	int TileOnTheLeft = (Index - 1 > 0) ? Index - 1 : Index;
	int TileOnTheRight = (Index + 1 < m_Width * m_Height) ? Index + 1 : Index;
	int TileBelow = (Index + m_Width < m_Width * m_Height) ? Index + m_Width : Index;
	int TileAbove = (Index - m_Width > 0) ? Index - m_Width : Index;

	if((m_pTiles[TileOnTheRight].m_Index == TILE_STOP && m_pTiles[TileOnTheRight].m_Flags == ROTATION_270) || (m_pTiles[TileOnTheLeft].m_Index == TILE_STOP && m_pTiles[TileOnTheLeft].m_Flags == ROTATION_90))
		return true;
	if((m_pTiles[TileBelow].m_Index == TILE_STOP && m_pTiles[TileBelow].m_Flags == ROTATION_0) || (m_pTiles[TileAbove].m_Index == TILE_STOP && m_pTiles[TileAbove].m_Flags == ROTATION_180))
		return true;
	if(m_pTiles[TileOnTheRight].m_Index == TILE_STOPA || m_pTiles[TileOnTheLeft].m_Index == TILE_STOPA || ((m_pTiles[TileOnTheRight].m_Index == TILE_STOPS || m_pTiles[TileOnTheLeft].m_Index == TILE_STOPS)))
		return true;
	if(m_pTiles[TileBelow].m_Index == TILE_STOPA || m_pTiles[TileAbove].m_Index == TILE_STOPA || ((m_pTiles[TileBelow].m_Index == TILE_STOPS || m_pTiles[TileAbove].m_Index == TILE_STOPS) && m_pTiles[TileBelow].m_Flags | ROTATION_180 | ROTATION_0))
		return true;
	if(m_pFront)
	{
		if(m_pFront[TileOnTheRight].m_Index == TILE_STOPA || m_pFront[TileOnTheLeft].m_Index == TILE_STOPA || ((m_pFront[TileOnTheRight].m_Index == TILE_STOPS || m_pFront[TileOnTheLeft].m_Index == TILE_STOPS)))
			return true;
		if(m_pFront[TileBelow].m_Index == TILE_STOPA || m_pFront[TileAbove].m_Index == TILE_STOPA || ((m_pFront[TileBelow].m_Index == TILE_STOPS || m_pFront[TileAbove].m_Index == TILE_STOPS) && m_pFront[TileBelow].m_Flags | ROTATION_180 | ROTATION_0))
			return true;
		if((m_pFront[TileOnTheRight].m_Index == TILE_STOP && m_pFront[TileOnTheRight].m_Flags == ROTATION_270) || (m_pFront[TileOnTheLeft].m_Index == TILE_STOP && m_pFront[TileOnTheLeft].m_Flags == ROTATION_90))
			return true;
		if((m_pFront[TileBelow].m_Index == TILE_STOP && m_pFront[TileBelow].m_Flags == ROTATION_0) || (m_pFront[TileAbove].m_Index == TILE_STOP && m_pFront[TileAbove].m_Flags == ROTATION_180))
			return true;
	}
	if(m_pDoor)
	{
		if(m_pDoor[TileOnTheRight].m_Index == TILE_STOPA || m_pDoor[TileOnTheLeft].m_Index == TILE_STOPA || ((m_pDoor[TileOnTheRight].m_Index == TILE_STOPS || m_pDoor[TileOnTheLeft].m_Index == TILE_STOPS)))
			return true;
		if(m_pDoor[TileBelow].m_Index == TILE_STOPA || m_pDoor[TileAbove].m_Index == TILE_STOPA || ((m_pDoor[TileBelow].m_Index == TILE_STOPS || m_pDoor[TileAbove].m_Index == TILE_STOPS) && m_pDoor[TileBelow].m_Flags | ROTATION_180 | ROTATION_0))
			return true;
		if((m_pDoor[TileOnTheRight].m_Index == TILE_STOP && m_pDoor[TileOnTheRight].m_Flags == ROTATION_270) || (m_pDoor[TileOnTheLeft].m_Index == TILE_STOP && m_pDoor[TileOnTheLeft].m_Flags == ROTATION_90))
			return true;
		if((m_pDoor[TileBelow].m_Index == TILE_STOP && m_pDoor[TileBelow].m_Flags == ROTATION_0) || (m_pDoor[TileAbove].m_Index == TILE_STOP && m_pDoor[TileAbove].m_Flags == ROTATION_180))
			return true;
	}
	return false;
}

int CCollision::GetMapIndex(vec2 Pos) const
{
	int Nx = std::clamp((int)Pos.x / 32, 0, m_Width - 1);
	int Ny = std::clamp((int)Pos.y / 32, 0, m_Height - 1);
	int Index = Ny * m_Width + Nx;

	if(TileExists(Index))
		return Index;
	else
		return -1;
}

std::vector<int> CCollision::GetMapIndices(vec2 PrevPos, vec2 Pos, unsigned MaxIndices) const
{
	std::vector<int> vIndices;
	float d = distance(PrevPos, Pos);
	int End(d + 1);
	if(!d)
	{
		int Nx = std::clamp((int)Pos.x / 32, 0, m_Width - 1);
		int Ny = std::clamp((int)Pos.y / 32, 0, m_Height - 1);
		int Index = Ny * m_Width + Nx;

		if(TileExists(Index))
		{
			vIndices.push_back(Index);
			return vIndices;
		}
		else
			return vIndices;
	}
	else
	{
		int LastIndex = 0;
		for(int i = 0; i < End; i++)
		{
			float a = i / d;
			vec2 Tmp = mix(PrevPos, Pos, a);
			int Nx = std::clamp((int)Tmp.x / 32, 0, m_Width - 1);
			int Ny = std::clamp((int)Tmp.y / 32, 0, m_Height - 1);
			int Index = Ny * m_Width + Nx;
			if(TileExists(Index) && LastIndex != Index)
			{
				if(MaxIndices && vIndices.size() > MaxIndices)
					return vIndices;
				vIndices.push_back(Index);
				LastIndex = Index;
			}
		}

		return vIndices;
	}
}

vec2 CCollision::GetPos(int Index) const
{
	if(Index < 0)
		return vec2(0, 0);

	int x = Index % m_Width;
	int y = Index / m_Width;
	return vec2(x * 32 + 16, y * 32 + 16);
}

int CCollision::GetTileIndex(int Index) const
{
	if(Index < 0)
		return 0;
	return m_pTiles[Index].m_Index;
}

int CCollision::GetFrontTileIndex(int Index) const
{
	if(Index < 0 || !m_pFront)
		return 0;
	return m_pFront[Index].m_Index;
}

int CCollision::GetTileFlags(int Index) const
{
	if(Index < 0)
		return 0;
	return m_pTiles[Index].m_Flags;
}

int CCollision::GetFrontTileFlags(int Index) const
{
	if(Index < 0 || !m_pFront)
		return 0;
	return m_pFront[Index].m_Flags;
}

int CCollision::GetIndex(int Nx, int Ny) const
{
	return m_pTiles[Ny * m_Width + Nx].m_Index;
}

int CCollision::GetIndex(vec2 PrevPos, vec2 Pos) const
{
	float Distance = distance(PrevPos, Pos);

	if(!Distance)
	{
		int Nx = std::clamp((int)Pos.x / 32, 0, m_Width - 1);
		int Ny = std::clamp((int)Pos.y / 32, 0, m_Height - 1);

		if((m_pTele) ||
			(m_pSpeedup && m_pSpeedup[Ny * m_Width + Nx].m_Force > 0))
		{
			return Ny * m_Width + Nx;
		}
	}

	const int DistanceRounded = std::ceil(Distance);
	for(int i = 0; i < DistanceRounded; i++)
	{
		float a = (float)i / Distance;
		vec2 Tmp = mix(PrevPos, Pos, a);
		int Nx = std::clamp((int)Tmp.x / 32, 0, m_Width - 1);
		int Ny = std::clamp((int)Tmp.y / 32, 0, m_Height - 1);
		if((m_pTele) ||
			(m_pSpeedup && m_pSpeedup[Ny * m_Width + Nx].m_Force > 0))
		{
			return Ny * m_Width + Nx;
		}
	}

	return -1;
}

int CCollision::GetFrontIndex(int Nx, int Ny) const
{
	if(!m_pFront)
		return 0;
	return m_pFront[Ny * m_Width + Nx].m_Index;
}

int CCollision::GetFrontTile(int x, int y) const
{
	if(!m_pFront)
		return 0;
	int Nx = std::clamp(x / 32, 0, m_Width - 1);
	int Ny = std::clamp(y / 32, 0, m_Height - 1);
	if(m_pFront[Ny * m_Width + Nx].m_Index == TILE_DEATH || m_pFront[Ny * m_Width + Nx].m_Index == TILE_NOLASER)
		return m_pFront[Ny * m_Width + Nx].m_Index;
	else
		return 0;
}

int CCollision::Entity(int x, int y, int Layer) const
{
	if(x < 0 || x >= m_Width || y < 0 || y >= m_Height)
		return 0;

	const int Index = y * m_Width + x;
	switch(Layer)
	{
	case LAYER_GAME:
		return m_pTiles[Index].m_Index - ENTITY_OFFSET;
	case LAYER_FRONT:
		return m_pFront[Index].m_Index - ENTITY_OFFSET;
	case LAYER_SWITCH:
		return m_pSwitch[Index].m_Type - ENTITY_OFFSET;
	case LAYER_TELE:
		return m_pTele[Index].m_Type - ENTITY_OFFSET;
	case LAYER_SPEEDUP:
		return m_pSpeedup[Index].m_Type - ENTITY_OFFSET;
	case LAYER_TUNE:
		return m_pTune[Index].m_Type - ENTITY_OFFSET;
	default:
		dbg_assert_failed("Invalid Layer: %d", Layer);
	}
}

void CCollision::SetCollisionAt(float x, float y, int Index)
{
	int Nx = std::clamp(round_to_int(x) / 32, 0, m_Width - 1);
	int Ny = std::clamp(round_to_int(y) / 32, 0, m_Height - 1);

	m_pTiles[Ny * m_Width + Nx].m_Index = Index;
}

void CCollision::SetDoorCollisionAt(float x, float y, int Type, int Flags, int Number)
{
	if(!m_pDoor)
		return;
	int Nx = std::clamp(round_to_int(x) / 32, 0, m_Width - 1);
	int Ny = std::clamp(round_to_int(y) / 32, 0, m_Height - 1);

	m_pDoor[Ny * m_Width + Nx].m_Index = Type;
	m_pDoor[Ny * m_Width + Nx].m_Flags = Flags;
	m_pDoor[Ny * m_Width + Nx].m_Number = Number;
}

void CCollision::GetDoorTile(int Index, CDoorTile *pDoorTile) const
{
	if(!m_pDoor || Index < 0 || !m_pDoor[Index].m_Index)
	{
		pDoorTile->m_Index = 0;
		pDoorTile->m_Flags = 0;
		pDoorTile->m_Number = 0;
		return;
	}
	*pDoorTile = m_pDoor[Index];
}

void ThroughOffset(vec2 Pos0, vec2 Pos1, int *pOffsetX, int *pOffsetY)
{
	float x = Pos0.x - Pos1.x;
	float y = Pos0.y - Pos1.y;
	if(absolute(x) > absolute(y))
	{
		if(x < 0)
		{
			*pOffsetX = -32;
			*pOffsetY = 0;
		}
		else
		{
			*pOffsetX = 32;
			*pOffsetY = 0;
		}
	}
	else
	{
		if(y < 0)
		{
			*pOffsetX = 0;
			*pOffsetY = -32;
		}
		else
		{
			*pOffsetX = 0;
			*pOffsetY = 32;
		}
	}
}

int CCollision::IntersectNoLaser(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
{
	float Distance = distance(Pos0, Pos1);
	vec2 Last = Pos0;

	const int DistanceRounded = std::ceil(Distance);
	for(int i = 0; i < DistanceRounded; i++)
	{
		float a = i / Distance;
		vec2 Pos = mix(Pos0, Pos1, a);
		int Nx = std::clamp(round_to_int(Pos.x) / 32, 0, m_Width - 1);
		int Ny = std::clamp(round_to_int(Pos.y) / 32, 0, m_Height - 1);
		if(GetIndex(Nx, Ny) == TILE_SOLID || GetIndex(Nx, Ny) == TILE_NOHOOK || GetIndex(Nx, Ny) == TILE_NOLASER || GetFrontIndex(Nx, Ny) == TILE_NOLASER)
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			if(GetFrontIndex(Nx, Ny) == TILE_NOLASER)
				return GetFrontCollisionAt(Pos.x, Pos.y);
			else
				return GetCollisionAt(Pos.x, Pos.y);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

int CCollision::IntersectNoLaserNoWalls(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
{
	float Distance = distance(Pos0, Pos1);
	vec2 Last = Pos0;

	const int DistanceRounded = std::ceil(Distance);
	for(int i = 0; i < DistanceRounded; i++)
	{
		float a = (float)i / Distance;
		vec2 Pos = mix(Pos0, Pos1, a);
		if(IsNoLaser(round_to_int(Pos.x), round_to_int(Pos.y)) || IsFrontNoLaser(round_to_int(Pos.x), round_to_int(Pos.y)))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			if(IsNoLaser(round_to_int(Pos.x), round_to_int(Pos.y)))
				return GetCollisionAt(Pos.x, Pos.y);
			else
				return GetFrontCollisionAt(Pos.x, Pos.y);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

int CCollision::IntersectAir(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
{
	float Distance = distance(Pos0, Pos1);
	vec2 Last = Pos0;

	const int DistanceRounded = std::ceil(Distance);
	for(int i = 0; i < DistanceRounded; i++)
	{
		float a = (float)i / Distance;
		vec2 Pos = mix(Pos0, Pos1, a);
		if(IsSolid(round_to_int(Pos.x), round_to_int(Pos.y)) || (!GetTile(round_to_int(Pos.x), round_to_int(Pos.y)) && !GetFrontTile(round_to_int(Pos.x), round_to_int(Pos.y))))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			if(!GetTile(round_to_int(Pos.x), round_to_int(Pos.y)) && !GetFrontTile(round_to_int(Pos.x), round_to_int(Pos.y)))
				return -1;
			else if(!GetTile(round_to_int(Pos.x), round_to_int(Pos.y)))
				return GetTile(round_to_int(Pos.x), round_to_int(Pos.y));
			else
				return GetFrontTile(round_to_int(Pos.x), round_to_int(Pos.y));
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

int CCollision::IsTimeCheckpoint(int Index) const
{
	if(Index < 0)
		return -1;

	int z = m_pTiles[Index].m_Index;
	if(z >= TILE_TIME_CHECKPOINT_FIRST && z <= TILE_TIME_CHECKPOINT_LAST)
		return z - TILE_TIME_CHECKPOINT_FIRST;
	return -1;
}

int CCollision::IsFrontTimeCheckpoint(int Index) const
{
	if(Index < 0 || !m_pFront)
		return -1;

	int z = m_pFront[Index].m_Index;
	if(z >= TILE_TIME_CHECKPOINT_FIRST && z <= TILE_TIME_CHECKPOINT_LAST)
		return z - TILE_TIME_CHECKPOINT_FIRST;
	return -1;
}

vec2 CCollision::TeleAllGet(int Number, size_t Offset)
{
	if(m_TeleIns.contains(Number))
	{
		if(m_TeleIns[Number].size() > Offset)
			return m_TeleIns[Number][Offset];
		else
			Offset -= m_TeleIns[Number].size();
	}
	if(m_TeleOuts.contains(Number))
	{
		if(m_TeleOuts[Number].size() > Offset)
			return m_TeleOuts[Number][Offset];
		else
			Offset -= m_TeleOuts[Number].size();
	}
	if(m_TeleCheckOuts.contains(Number))
	{
		if(m_TeleCheckOuts[Number].size() > Offset)
			return m_TeleCheckOuts[Number][Offset];
		else
			Offset -= m_TeleCheckOuts[Number].size();
	}
	if(m_TeleOthers.contains(Number))
	{
		if(m_TeleOthers[Number].size() > Offset)
			return m_TeleOthers[Number][Offset];
	}
	return vec2(-1, -1);
}

size_t CCollision::TeleAllSize(int Number)
{
	size_t Total = 0;
	if(m_TeleIns.contains(Number))
		Total += m_TeleIns[Number].size();
	if(m_TeleOuts.contains(Number))
		Total += m_TeleOuts[Number].size();
	if(m_TeleCheckOuts.contains(Number))
		Total += m_TeleCheckOuts[Number].size();
	if(m_TeleOthers.contains(Number))
		Total += m_TeleOthers[Number].size();
	return Total;
}

// <FoxNet

//void CCollision::UpdateUnfreezeQuadMask()
//{
//	// Ensure mask matches current map size before filling
//	const size_t expectedSize = (size_t)m_Width * (size_t)m_Height;
//	if(m_UnfreezeQuadMask.size() != expectedSize)
//	{
//		m_UnfreezeQuadMask.assign(expectedSize, 0);
//	}
//	else
//	{
//		std::fill(m_UnfreezeQuadMask.begin(), m_UnfreezeQuadMask.end(), 0);
//	}
//
//	if(m_Width > 0 && m_Height > 0)
//	{
//		for(const auto &QuadData : m_vQuads)
//		{
//			if(QuadData.m_Type != QUADTYPE_UNFREEZE)
//				continue;
//
//			const int minTx = std::clamp((int)std::floor(QuadData.m_TopLeft.x / 32.0f), 0, m_Width - 1);
//			const int maxTx = std::clamp((int)std::floor(QuadData.m_BottomRight.x / 32.0f), 0, m_Width - 1);
//			const int minTy = std::clamp((int)std::floor(QuadData.m_TopLeft.y / 32.0f), 0, m_Height - 1);
//			const int maxTy = std::clamp((int)std::floor(QuadData.m_BottomRight.y / 32.0f), 0, m_Height - 1);
//
//			for(int ty = minTy; ty <= maxTy; ++ty)
//			{
//				for(int tx = minTx; tx <= maxTx; ++tx)
//				{
//					const int idx = ty * m_Width + tx;
//					// extra safety
//					if(idx < 0 || (size_t)idx >= m_UnfreezeQuadMask.size())
//						continue;
//
//					const vec2 Center(tx * 32.0f + 16.0f, ty * 32.0f + 16.0f);
//					// Fast center-in-quad check (no radius work)
//					if(InsideQuadCenterFast(Center, QuadData.m_Pos[0], QuadData.m_Pos[1], QuadData.m_Pos[2], QuadData.m_Pos[3]))
//					{
//						m_UnfreezeQuadMask[(size_t)idx] = 1;
//					}
//				}
//			}
//		}
//	}
//}

void CCollision::CollectMapSpawnPoints(std::vector<vec2> &OutSeeds) const
{
	const int W = GetWidth();
	const int H = GetHeight();
	OutSeeds.clear();
	OutSeeds.reserve(16);

	for(int y = 0; y < H; ++y)
	{
		for(int x = 0; x < W; ++x)
		{
			// Game layer
			{
				const int Ent = Entity(x, y, LAYER_GAME);
				if(Ent >= ENTITY_SPAWN && Ent <= ENTITY_SPAWN_BLUE)
					OutSeeds.emplace_back(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
			}
			// Front layer (if present)
			if(m_pFront)
			{
				const int FrontEnt = Entity(x, y, LAYER_FRONT);
				if(FrontEnt >= ENTITY_SPAWN && FrontEnt <= ENTITY_SPAWN_BLUE)
					OutSeeds.emplace_back(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
			}
		}
	}
}

int CCollision::CountSolidTilesInRadius(vec2 Pos, int TileRadius, bool Circle) const
{
	if(TileRadius < 0)
		return 0;

	const int W = GetWidth();
	const int H = GetHeight();

	const int Cx = std::clamp((int)std::floor(Pos.x / 32.0f), 0, W - 1);
	const int Cy = std::clamp((int)std::floor(Pos.y / 32.0f), 0, H - 1);

	const int RadiusSquared = TileRadius * TileRadius;
	int Count = 0;

	for(int Dy = -TileRadius; Dy <= TileRadius; ++Dy)
	{
		for(int Dx = -TileRadius; Dx <= TileRadius; ++Dx)
		{
			if(Circle && (Dx * Dx + Dy * Dy) > RadiusSquared)
				continue;

			const int Tx = Cx + Dx;
			const int Ty = Cy + Dy;
			if(Tx < 0 || Ty < 0 || Tx >= W || Ty >= H)
				continue;

			const int Px = Tx * 32 + 16;
			const int Py = Ty * 32 + 16;
			if(IsSolid(Px, Py))
				++Count;
		}
	}
	return Count;
}

bool CCollision::HasSolidInRadius(vec2 Pos, int TileRadius, int MinCount, bool Circle) const
{
	return CountSolidTilesInRadius(Pos, TileRadius, Circle) >= MinCount;
}

void CCollision::InitQuads()
{
	dbg_assert(m_pLayers, "m_pLayers must be valid");

	for(auto *const pQuadLayers : m_pLayers->QuadLayers())
	{
		CQuad *pQuads = (CQuad *)m_pLayers->Map()->GetDataSwapped(pQuadLayers->m_Data);
		for(int i = 0; i < pQuadLayers->m_NumQuads; i++)
		{
			char QuadName[30] = "";
			IntsToStr(pQuadLayers->m_aName, std::size(pQuadLayers->m_aName), QuadName, std::size(QuadName));

			CQuadData QuadData;
			QuadData.m_pQuad = &pQuads[i];
			QuadData.m_pLayer = pQuadLayers;
			if(!str_comp(QuadName, "QHook"))
				QuadData.m_Type = EZoneType::Hookable;
			else if(!str_comp(QuadName, "QUnHook"))
				QuadData.m_Type = EZoneType::Unhookable;
			else
				continue;
			m_HasSolidQuads = true;
			for(int j = 0; j < 5; j++)
				QuadData.m_Pos[j] = vec2(fx2f(QuadData.m_pQuad->m_aPoints[j].x), fx2f(QuadData.m_pQuad->m_aPoints[j].y));

			m_vNextQuads.push_back(QuadData);
		}
	}
	m_vQuads = m_vNextQuads;
}

void CCollision::UnloadQuads()
{
	m_vQuads.clear();
	m_vNextQuads.clear();
}

void CCollision::UpdateQuads(float Time)
{
	//if(!g_Config.m_SvMovingTiles)
	//	return;

	auto GetAnimationTransform = [this, Time](float Offset, int Env, vec2 &Position, float &Angle) {
		Position.x = 0.0f;
		Position.y = 0.0f;
		Angle = 0.0f;

		if(Env < 0)
			return;

		int Start, Num;
		m_pLayers->Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		if(Env >= Num)
			return;

		CMapItemEnvelope *pItem = (CMapItemEnvelope *)m_pLayers->Map()->GetItem(Start + Env, 0, 0);
		if(pItem->m_NumPoints == 0)
			return;

		IMap *pMap = m_pLayers->Map();
		CMapBasedEnvelopePointAccess EnvelopePoints(pMap);
		EnvelopePoints.SetPointsRange(pItem->m_StartPoint, pItem->m_NumPoints);
		if(EnvelopePoints.NumPoints() == 0)
			return;

		// Single point shortcut
		if(EnvelopePoints.NumPoints() == 1)
		{
			const CEnvPoint *pOnly = EnvelopePoints.GetPoint(0);
			Position.x = fx2f(pOnly->m_aValues[0]);
			Position.y = fx2f(pOnly->m_aValues[1]);
			Angle = fx2f(pOnly->m_aValues[2]) / 360.0f * pi * 2.0f;
			return;
		}

		const int NumPoints = EnvelopePoints.NumPoints();
		const CEnvPoint *pLastPoint = EnvelopePoints.GetPoint(NumPoints - 1);

		// Convert GlobalTime (seconds) to milliseconds like RenderEvalEnvelope logic
		double GlobalMillis = (double)(Time + Offset) * 1000.0;
		const int64_t LoopMillis = (int64_t)pLastPoint->m_Time.GetInternal();
		if(LoopMillis > 0)
			GlobalMillis = std::fmod(GlobalMillis, (double)LoopMillis);
		else
			GlobalMillis = 0.0; // degenerate envelope

		// Locate current segment
		int FoundIndex = EnvelopePoints.FindPointIndex(CFixedTime(GlobalMillis));
		if(FoundIndex == -1)
		{
			// After last point
			Position.x = fx2f(pLastPoint->m_aValues[0]);
			Position.y = fx2f(pLastPoint->m_aValues[1]);
			Angle = fx2f(pLastPoint->m_aValues[2]) / 360.0f * pi * 2.0f;
			return;
		}

		const CEnvPoint *pCur = EnvelopePoints.GetPoint(FoundIndex);
		const CEnvPoint *pNext = EnvelopePoints.GetPoint(FoundIndex + 1);
		CFixedTime Delta = pNext->m_Time - pCur->m_Time;
		if(Delta <= CFixedTime(0))
		{
			Position.x = fx2f(pCur->m_aValues[0]);
			Position.y = fx2f(pCur->m_aValues[1]);
			Angle = fx2f(pCur->m_aValues[2]) / 360.0f * pi * 2.0f;
			return;
		}

		float a = (float)(GlobalMillis - pCur->m_Time.GetInternal()) / (float)Delta.GetInternal();
		switch(pCur->m_Curvetype)
		{
		case CURVETYPE_STEP:
			a = 0.0f;
			break;
		case CURVETYPE_SLOW:
			a = a * a * a;
			break;
		case CURVETYPE_FAST:
			a = 1.0f - a;
			a = 1.0f - a * a * a;
			break;
		case CURVETYPE_SMOOTH:
			a = -2.0f * a * a * a + 3.0f * a * a;
			break;
		case CURVETYPE_BEZIER:
		{
			const CEnvPointBezier *pCurBez = EnvelopePoints.GetBezier(FoundIndex);
			const CEnvPointBezier *pNextBez = EnvelopePoints.GetBezier(FoundIndex + 1);
			if(pCurBez && pNextBez)
			{
				float Channels[3] = {0.f, 0.f, 0.f};
				for(size_t c = 0; c < 3; ++c)
				{
					// 2D cubic bezier in (time,value) space (time in ms)
					vec2 P0 = vec2(pCur->m_Time.GetInternal(), fx2f(pCur->m_aValues[c]));
					vec2 P3 = vec2(pNext->m_Time.GetInternal(), fx2f(pNext->m_aValues[c]));
					vec2 OutTang = vec2(pCurBez->m_aOutTangentDeltaX[c].GetInternal(), fx2f(pCurBez->m_aOutTangentDeltaY[c]));
					vec2 InTang = vec2(pNextBez->m_aInTangentDeltaX[c].GetInternal(), fx2f(pNextBez->m_aInTangentDeltaY[c]));
					vec2 P1 = P0 + OutTang;
					vec2 P2 = P3 + InTang;
					P1.x = std::clamp(P1.x, P0.x, P3.x);
					P2.x = std::clamp(P2.x, P0.x, P3.x);
					float t = std::clamp(SolveBezier((float)GlobalMillis, P0.x, P1.x, P2.x, P3.x), 0.0f, 1.0f);
					Channels[c] = bezier(P0.y, P1.y, P2.y, P3.y, t);
				}
				Position.x = Channels[0];
				Position.y = Channels[1];
				Angle = Channels[2] / 360.0f * pi * 2.0f;
				return; // Bezier done
			}
			// fallthrough to linear if bezier data missing
			break;
		}
		case CURVETYPE_LINEAR:
		default:
			break; // linear handled below
		}

		// Linear interpolation (or shaped 'a')
		const float X0 = fx2f(pCur->m_aValues[0]);
		const float X1 = fx2f(pNext->m_aValues[0]);
		const float Y0 = fx2f(pCur->m_aValues[1]);
		const float Y1 = fx2f(pNext->m_aValues[1]);
		const float R0 = fx2f(pCur->m_aValues[2]);
		const float R1 = fx2f(pNext->m_aValues[2]);
		Position.x = X0 + (X1 - X0) * a;
		Position.y = Y0 + (Y1 - Y0) * a;
		Angle = (R0 + (R1 - R0) * a) / 360.0f * pi * 2.0f;
	};

	//m_vQuads = m_vNextQuads;
	std::swap(m_vQuads, m_vNextQuads);

	for(auto &QuadData : m_vNextQuads)
	{
		vec2 Position = vec2(0, 0);
		GetAnimationTransform(QuadData.m_pQuad->m_PosEnvOffset / 1000.0, QuadData.m_pQuad->m_PosEnv, Position, QuadData.m_Angle);
		for(int i = 0; i < 5; i++)
			QuadData.m_Pos[i] = Position + vec2(fx2f(QuadData.m_pQuad->m_aPoints[i].x), fx2f(QuadData.m_pQuad->m_aPoints[i].y));

		if(QuadData.m_Angle != 0)
		{
			for(int i = 0; i < 4; i++)
				Rotate(QuadData.m_Pos[4], &QuadData.m_Pos[i], QuadData.m_Angle);
		}
	}
}

const CQuadData *CCollision::GetQuadAt(vec2 Pos) const
{
	for(const CQuadData &Quad : m_vQuads)
	{
		if(InsideQuadrilateral(Pos, Quad.m_Pos[0], Quad.m_Pos[1], Quad.m_Pos[3], Quad.m_Pos[2]))
			return &Quad;
	}
	return nullptr;
}

const CQuadData *CCollision::ResolveCurrentQuad(const CQuadData *pQuad) const
{
	if(!pQuad || !pQuad->m_pQuad)
		return nullptr;

	for(const CQuadData &QuadData : m_vQuads)
	{
		if(!QuadData.m_pQuad)
			continue;

		if(QuadData.m_pQuad == pQuad->m_pQuad)
			return &QuadData;
	}

	return nullptr;
}

void CCollision::BuildSpawnCandidates()
{
	m_SpawnCandidates.clear();

	std::vector<vec2> Seeds;
	CollectMapSpawnPoints(Seeds);
	if(Seeds.empty())
		return;

	const int W = GetWidth();
	const int H = GetHeight();

	const auto ToIndex = [&](int X, int Y) { return Y * W + X; };
	const auto InBounds = [&](int X, int Y) { return X >= 0 && X < W && Y >= 0 && Y < H; };

	// Detect whether the map contains any start tiles at all (game or front layer).
	bool HasAnyStartTiles = false;
	for(int Y = 0; Y < H && !HasAnyStartTiles; ++Y)
	{
		for(int X = 0; X < W && !HasAnyStartTiles; ++X)
		{
			const int Idx = ToIndex(X, Y);
			if(GetTileIndex(Idx) == TILE_START || GetFrontTileIndex(Idx) == TILE_START)
				HasAnyStartTiles = true;
		}
	}

	const auto IsAirAt = [&](int Tx, int Ty) -> bool {
		if(!InBounds(Tx, Ty))
			return false;
		const int Idx = ToIndex(Tx, Ty);
		return GetTileIndex(Idx) == TILE_AIR && GetFrontTileIndex(Idx) == TILE_AIR;
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
		if(!m_pTele || !InBounds(Tx, Ty))
			return false;
		const int Idx = ToIndex(Tx, Ty);
		return m_pTele[Idx].m_Type != 0;
	};

	const auto IsFreezeLikeAtIndex = [&](int Idx) -> bool {
		const int Game = GetTileIndex(Idx);
		const int Front = GetFrontTileIndex(Idx);
		const int Sw = GetSwitchType(Idx);
		return Game == TILE_FREEZE || Game == TILE_DFREEZE || Game == TILE_LFREEZE ||
		       Front == TILE_FREEZE || Front == TILE_DFREEZE || Front == TILE_LFREEZE ||
		       Sw == TILE_FREEZE || Sw == TILE_DFREEZE || Sw == TILE_LFREEZE;
	};

	const auto IsUnfreezeLikeAtIndex = [&](int Idx) -> bool {
		const int Game = GetTileIndex(Idx);
		const int Front = GetFrontTileIndex(Idx);
		const int Sw = GetSwitchType(Idx);

		const bool QuadUnfreeze = false; //!m_UnfreezeQuadMask.empty() && Idx >= 0 && (size_t)Idx < m_UnfreezeQuadMask.size() && m_UnfreezeQuadMask[(size_t)Idx] != 0;

		return Game == TILE_UNFREEZE || Game == TILE_DUNFREEZE || Game == TILE_LUNFREEZE ||
		       Front == TILE_UNFREEZE || Front == TILE_DUNFREEZE || Front == TILE_LUNFREEZE ||
		       Sw == TILE_UNFREEZE || Sw == TILE_DUNFREEZE || Sw == TILE_LUNFREEZE ||
		       QuadUnfreeze;
	};

	const auto IsBlockedForSpawnNav = [&](int Tx, int Ty) -> bool {
		if(!InBounds(Tx, Ty))
			return true;
		const int Idx = ToIndex(Tx, Ty);
		const int Game = GetTileIndex(Idx);
		const int Front = GetFrontTileIndex(Idx);
		const bool Solid = Game == TILE_SOLID || Game == TILE_NOHOOK;
		const bool Finish = Game == TILE_FINISH || Front == TILE_FINISH;
		const bool Kill = Game == TILE_DEATH || Front == TILE_DEATH;
		const bool StopA = Game == TILE_STOPA || Front == TILE_STOPA;
		return Solid || Finish || Kill || StopA;
	};

	const auto IsStartAtIndex = [&](int Idx) -> bool {
		if(Idx < 0)
			return false;
		return GetTileIndex(Idx) == TILE_START || GetFrontTileIndex(Idx) == TILE_START;
	};

	// Tele helpers
	const auto IsTeleInRegular = [](unsigned char Type) {
		return Type == TILE_TELEIN || Type == TILE_TELEINEVIL;
	};
	const auto IsTeleInCheckpoint = [](unsigned char Type) {
		return Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL;
	};
	const auto TeleCheckpointAtIndex = [&](int Idx) -> int {
		if(!m_pTele || Idx < 0)
			return 0;
		if(m_pTele[Idx].m_Type == TILE_TELECHECK)
			return (int)m_pTele[Idx].m_Number;
		return 0;
	};

	// Freeze -> Unfreeze "skip" search window
	constexpr int UnfreezeRadiusX = 13;
	constexpr int UnfreezeRadiusUp = 7;
	constexpr int UnfreezeRadiusDown = 50;

	// Returns true and outputs (outIdx) if an unfreeze-like tile is found near (tx,ty).
	// Prefers the closest one (Manhattan distance) inside that rectangle.
	const auto FindNearbyUnfreezeIndex = [&](int Tx, int Ty, const std::vector<uint8_t> &Visited, int &OutIdx) -> bool {
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

				const bool CandidateUnvisited = !Visited[Idx];
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

				const bool BestUnvisited = !Visited[BestIdx];
				const bool BestBelow = BestY >= Ty;
				const int BestDy = std::abs(BestY - Ty);
				const int BestDx = std::abs(BestX - Tx);

				// Compare (unvisited desc, below desc, dy asc, dx asc)
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

	// Helper: enqueue all valid destinations for a given tele number and type (regular tele outs)
	auto EnqueueTeleDestinationsByTeleNum = [&](int TeleNumMinusOne, std::vector<uint8_t> &Visited, std::deque<std::tuple<int, int, int, bool>> &Q, int Cp, bool CrossedStart) {
		auto It = m_TeleOuts.find(TeleNumMinusOne);
		if(It != m_TeleOuts.end())
		{
			for(const vec2 &OutPos : It->second)
			{
				const int OutX = std::clamp((int)std::floor(OutPos.x / 32.0f), 0, W - 1);
				const int OutY = std::clamp((int)std::floor(OutPos.y / 32.0f), 0, H - 1);
				const int OutIdx = ToIndex(OutX, OutY);
				if(Visited[OutIdx])
					continue;
				if(!IsBlockedForSpawnNav(OutX, OutY))
				{
					Visited[OutIdx] = 1;
					const bool NextCrossedStart = CrossedStart || IsStartAtIndex(OutIdx);
					Q.emplace_back(OutX, OutY, Cp, NextCrossedStart);
				}
			}
		}
	};

	// Helper: enqueue all valid checkpoint destinations using current cp number (checkpoint tele outs)
	auto EnqueueTeleCheckpointDestinationsByCp = [&](int CpNumber, std::vector<uint8_t> &Visited, std::deque<std::tuple<int, int, int, bool>> &Q, bool CrossedStart) {
		if(CpNumber <= 0)
			return;
		const int Key = CpNumber;
		auto It = m_TeleCheckOuts.find(Key);
		if(It != m_TeleCheckOuts.end())
		{
			for(const vec2 &OutPos : It->second)
			{
				const int OutX = std::clamp((int)std::floor(OutPos.x / 32.0f), 0, W - 1);
				const int OutY = std::clamp((int)std::floor(OutPos.y / 32.0f), 0, H - 1);
				const int OutIdx = ToIndex(OutX, OutY);
				if(Visited[OutIdx])
					continue;
				if(!IsBlockedForSpawnNav(OutX, OutY))
				{
					Visited[OutIdx] = 1;
					const bool NextCrossedStart = CrossedStart || IsStartAtIndex(OutIdx);
					Q.emplace_back(OutX, OutY, CpNumber, NextCrossedStart);
				}
			}
		}
	};

	// BFS carrying checkpoint number (cp) and whether we've crossed start line
	std::deque<std::tuple<int, int, int, bool>> Q; // x, y, cp, crossedStart
	std::vector<uint8_t> Visited((size_t)W * H, 0);

	for(const vec2 &Seed : Seeds)
	{
		const int SeedX = std::clamp((int)std::floor(Seed.x / 32.0f), 0, W - 1);
		const int SeedY = std::clamp((int)std::floor(Seed.y / 32.0f), 0, H - 1);
		const int SeedIdx = ToIndex(SeedX, SeedY);
		if(!Visited[SeedIdx])
		{
			Visited[SeedIdx] = 1;
			const bool InitialCrossedStart = HasAnyStartTiles ? IsStartAtIndex(SeedIdx) : true;
			Q.emplace_back(SeedX, SeedY, 0, InitialCrossedStart);
		}
	}

	constexpr int SolidRadius = 6;
	const int DirX[4] = {1, -1, 0, 0};
	const int DirY[4] = {0, 0, 1, -1};

	while(!Q.empty())
	{
		auto [X, Y, Cp, CrossedStart] = Q.front();
		Q.pop_front();

		const int CurIdx = ToIndex(X, Y);
		if(m_pTele)
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
			if(HasSolidInRadius(Pos, SolidRadius, 1, true))
				m_SpawnCandidates.push_back(Pos);
		}

		for(int k = 0; k < 4; ++k)
		{
			const int NextX = X + DirX[k], NextY = Y + DirY[k];
			if(!InBounds(NextX, NextY))
				continue;

			const int NextIdx = ToIndex(NextX, NextY);
			if(Visited[NextIdx])
				continue;

			int NextCp = Cp;
			bool NextCrossedStart = CrossedStart;
			if(m_pTele)
			{
				const int CpNext = TeleCheckpointAtIndex(NextIdx);
				if(CpNext > 0)
					NextCp = CpNext;

				const unsigned char NextType = m_pTele[NextIdx].m_Type;
				const unsigned char NextNum = m_pTele[NextIdx].m_Number;

				if(NextNum > 0 && (IsTeleInRegular(NextType) || IsTeleInCheckpoint(NextType)))
				{
					Visited[NextIdx] = 1; // we don't stay on tele-in tiles
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

			// If we would step into freeze, try to "skip" to a nearby unfreeze.
			if(IsFreezeLikeAtIndex(NextIdx))
			{
				int UnfreezeIdx = -1;
				if(!FindNearbyUnfreezeIndex(NextX, NextY, Visited, UnfreezeIdx))
					continue;

				const int UnfreezeX = UnfreezeIdx % W;
				const int UnfreezeY = UnfreezeIdx / W;

				if(!Visited[UnfreezeIdx])
				{
					Visited[UnfreezeIdx] = 1;
					const bool UnfreezeCrossedStart = NextCrossedStart || IsStartAtIndex(UnfreezeIdx);
					Q.emplace_back(UnfreezeX, UnfreezeY, NextCp, UnfreezeCrossedStart);
				}
				continue;
			}

			Visited[NextIdx] = 1;
			NextCrossedStart = NextCrossedStart || IsStartAtIndex(NextIdx);
			Q.emplace_back(NextX, NextY, NextCp, NextCrossedStart);
		}

		// Also process tele-in at current tile (if we happen to be standing on one)
		if(m_pTele)
		{
			const unsigned char TeleType = m_pTele[CurIdx].m_Type;
			const unsigned char TeleNum = m_pTele[CurIdx].m_Number;
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
	const float ExclusionRadiusPx = SpawnExclusionRadiusTiles * 32.0f;

	if(!Seeds.empty() && !m_SpawnCandidates.empty())
	{
		std::erase_if(m_SpawnCandidates, [&](const vec2 &Pos) {
			return std::ranges::any_of(Seeds, [&](const vec2 &Seed) {
				return distance(Pos, Seed) <= ExclusionRadiusPx;
			});
		});
	}
}

bool CCollision::TryPickCachedCandidate(vec2 &Out) const
{
	if(m_SpawnCandidates.empty())
		return false;
	static thread_local std::mt19937 s_rng{std::random_device{}()};
	std::uniform_int_distribution<size_t> Pick(0, m_SpawnCandidates.size() - 1);
	Out = m_SpawnCandidates[Pick(s_rng)];
	return true;
}

int CCollision::PosToIndex(vec2 Pos) const
{
	const int X = (int)(Pos.x / 32.0f);
	const int Y = (int)(Pos.y / 32.0f);
	if(X < 0 || X >= GetWidth() || Y < 0 || Y >= GetHeight())
		return -1;
	return Y * GetWidth() + X;
}

vec2 CCollision::IndexToPos(int Index) const
{
	const int W = GetWidth();
	const int X = Index % W;
	const int Y = Index / W;
	return vec2(X * 32.0f + 16.0f, Y * 32.0f + 16.0f);
}
// FoxNet>
