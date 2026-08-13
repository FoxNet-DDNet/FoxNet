// Made by qxdFox
#include "pickupdrop.h"

#include "foxnet_entity.h"

#include <base/log.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/entities/pickup.h>
#include <game/server/entity.h>
#include <game/server/foxnet/components/accounts/accounts.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

void CPickupDrop::SnapPickupDropPickup(int SnappingClient, int SnapId, int OldFlags, const vec2 &Pos, int Type, int SubType, int Rotation = 0, int Alpha = -1, int Flags = 0)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);
		GameServer()->SnapPickup(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, Pos, Type, SubType, -1, OldFlags);
		return;
	}

	CNetObj_CosmeticPickup Pickup = {};

	Pickup.m_X = (int)Pos.x;
	Pickup.m_Y = (int)Pos.y;
	Pickup.m_Type = Type;
	Pickup.m_Subtype = SubType;
	Pickup.m_Owner = -1;
	Pickup.m_Alpha = Alpha;
	Pickup.m_Rotation = Rotation;
	Pickup.m_Flags = Flags;
	Server()->SnapNewItem(SnapId, Pickup);
}

void CPickupDrop::SnapPickupDropLaser(int SnappingClient, int SnapId, const vec2 &From, const vec2 &To, int Type, int Alpha = -1, int Flags = 0)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || !GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps)
	{
		const int SnapVer = Server()->GetClientVersion(SnappingClient);
		const bool SixUp = Server()->IsSixup(SnappingClient);
		GameServer()->SnapLaserObject(CSnapContext(SnapVer, SixUp, SnappingClient), SnapId, From, To, Server()->Tick(), -1, LASERTYPE_GUN, -1, -1, LASERFLAG_NO_PREDICT);
		return;
	}

	CNetObj_CosmeticLaser Laser = {};

	Laser.m_FromX = (int)From.x;
	Laser.m_FromY = (int)From.y;
	Laser.m_ToX = (int)To.x;
	Laser.m_ToY = (int)To.y;
	Laser.m_TickOffset = 0;
	Laser.m_Type = Type;
	Laser.m_Owner = -1;
	Laser.m_Alpha = Alpha;
	Laser.m_Flags = Flags;
	Server()->SnapNewItem(SnapId, Laser);
}

void CPickupDrop::SnapPickupDropProjectile(int SnapId, int SnappingClient, int Type, vec2 Pos, vec2 Dir)
{
	const int Id = SnapId;

	const int StartTick = Server()->Tick() - 1;

	if(length(Dir) > 0.00001f)
		Dir = normalize(Dir);

	bool Supports = SnappingClient != SERVER_DEMO_CLIENT && GameServer()->m_apPlayers[SnappingClient]->m_SupportsCosmeticSnaps;
	if(Supports)
	{
		int Rotation = round_to_int(angle(Dir) * 180.0f / pi);
		Rotation = ((Rotation % 360) + 360) % 360;

		CNetObj_CosmeticProjectile Projectile = {};
		Projectile.m_X = (int)Pos.x;
		Projectile.m_Y = (int)Pos.y;
		Projectile.m_Type = Type;
		Projectile.m_Owner = -1;
		Projectile.m_Alpha = 100;
		Projectile.m_Rotation = Rotation;
		Projectile.m_Flags = 0;
		Server()->SnapNewItem(Id, Projectile);
		return;
	}

	const int Ver = GameServer()->GetClientVersion(SnappingClient);
	const float Factor = 0;
	const int MaxPos = 0x7fffffff / 100;
	bool LegacyCompatible = !(absolute((int)Pos.y) + 1 >= MaxPos || absolute((int)Pos.x) + 1 >= MaxPos);

	if(Ver >= VERSION_DDNET_ENTITY_NETOBJS)
	{
		CNetObj_DDNetProjectile Proj = {};
		Proj.m_X = round_to_int(Pos.x * 100.0f);
		Proj.m_Y = round_to_int(Pos.y * 100.0f);
		Proj.m_VelX = round_to_int(Dir.x * Factor * 1e6f);
		Proj.m_VelY = round_to_int(Dir.y * Factor * 1e6f);
		Proj.m_Type = Type;
		Proj.m_StartTick = StartTick;
		Proj.m_Owner = -1;
		Proj.m_SwitchNumber = 0;
		Proj.m_TuneZone = m_TuneZone;
		Proj.m_Flags = 0;
		Server()->SnapNewItem(Id, Proj);
	}
	else if(Ver >= VERSION_DDNET_ANTIPING_PROJECTILE && LegacyCompatible)
	{
		float Angle = -std::atan2(Dir.x, Dir.y);
		int Data = LEGACYPROJECTILEFLAG_IS_DDNET | LEGACYPROJECTILEFLAG_NO_OWNER;

		CNetObj_DDRaceProjectile Proj = {};
		Proj.m_X = (int)(Pos.x * 100.0f);
		Proj.m_Y = (int)(Pos.y * 100.0f);
		Proj.m_Angle = (int)(Angle * 1000000.0f);
		Proj.m_Data = Data;
		Proj.m_StartTick = StartTick;
		Proj.m_Type = Type;

		if(Ver >= VERSION_DDNET_MSG_LEGACY)
		{
			Server()->SnapNewItem(Id, Proj);
		}
		else
		{
			CNetObj_Projectile Projectile = {};
			static_assert(sizeof(Proj) == sizeof(Projectile));
			mem_copy(&Projectile, &Proj, sizeof(Projectile));
			Server()->SnapNewItem(Id, Projectile);
		}
	}
	else
	{
		CNetObj_Projectile Proj = {};
		Proj.m_X = (int)Pos.x;
		Proj.m_Y = (int)Pos.y;
		Proj.m_VelX = (int)(Dir.x * Factor * 100.0f);
		Proj.m_VelY = (int)(Dir.y * Factor * 100.0f);
		Proj.m_StartTick = StartTick;
		Proj.m_Type = Type;
		Server()->SnapNewItem(Id, Proj);
	}
}

CPickupDrop::CPickupDrop(CGameWorld *pGameWorld, int MultiMapIndex, int LastOwner, vec2 Pos, int Team, int TeleCheckpoint, vec2 Dir, int Lifetime, int Type) :
	CEntity(pGameWorld, MultiMapIndex, CGameWorld::ENTTYPE_PICKUPDROP, true, Pos, 28)
{
	m_StartTick = Server()->Tick();
	m_LastOwner = LastOwner;

	m_PrevPos = m_Pos;
	m_Pos = Pos;
	m_Team = Team;
	m_Vel = Dir;
	m_Lifetime = Lifetime * Server()->TickSpeed();
	m_Type = std::clamp(Type, (int)WEAPON_NONE + 1, (int)NUM_EXTRA_WEAPONS);
	m_TuneZone = -1;
	m_TeleCheckpoint = TeleCheckpoint;

	m_PickupDelay = Server()->TickSpeed() * 1.5f;
	m_GroundElasticity = vec2(0.5f, 0.5f);

	m_Sleeping = false;
	m_RestTicks = 0;
	m_SleepRecheck = 0;

	for(size_t i = 0; i < std::size(m_aIds); i++)
		m_aIds[i] = Server()->SnapNewId();

	GameWorld()->InsertEntity(this);
}

void CPickupDrop::RemoveFromOwner()
{
	if(m_LastOwner < 0 || m_LastOwner >= MAX_CLIENTS)
		return;
	CPlayer *pPlayer = GameServer()->m_apPlayers[m_LastOwner];
	if(!pPlayer)
		return;
	auto &vDrops = pPlayer->m_vPickupDrops;
	vDrops.erase(std::remove(vDrops.begin(), vDrops.end(), this), vDrops.end());
}

void CPickupDrop::Reset(bool PickedUp)
{
	if(m_MarkedForDestroy)
		return;

	if(g_Config.m_SvLogExtra >= 2)
		log_info("pickupdrop", "Reset");

	for(size_t i = 0; i < std::size(m_aIds); i++)
	{
		if(m_aIds[i].has_value())
			Server()->SnapFreeId(m_aIds[i].value());
	}

	RemoveFromOwner();

	if(!PickedUp)
		GameServer()->CreateDeath(m_Pos, -1, PickupMask(-1));

	m_MarkedForDestroy = true;
}

bool CPickupDrop::IsSwitchActiveCb(unsigned char Number, void *pUser)
{
	CPickupDrop *pThis = (CPickupDrop *)pUser;
	auto &aSwitchers = pThis->Switchers();
	return !aSwitchers.empty() && pThis->m_Team != TEAM_SUPER && aSwitchers[Number].m_aStatus[pThis->m_Team];
}

void CPickupDrop::Tick()
{
	if(m_MarkedForDestroy)
		return;

	if(m_LastOwner >= 0 && (!GameServer()->m_apPlayers[m_LastOwner] && g_Config.m_SvWeaponDropsResetOnLeave))
	{
		Reset();
		return;
	}

	m_Lifetime--;
	if(m_Lifetime <= 0)
	{
		Reset();
		return;
	}
	if(CheckArmor())
		return;
	if(CollectItem())
		return;

	if(m_Team != TEAM_SUPER && m_Team != TEAM_FLOCK)
	{
		int NumInTeam = GameServer()->NumPlayersInTeam(m_Team);
		if(NumInTeam == 0)
		{
			Reset();
			return;
		}
	}

	if(m_Sleeping)
	{
		// The world can change under a resting drop (a door opening takes the floor
		// away), so fall through to a full tick every so often to notice it.
		if(--m_SleepRecheck > 0)
			return;
		m_Sleeping = false;
	}

	int CurrentIndex = Collision()->GetMapIndex(m_Pos);
	m_TuneZone = Collision()->IsTune(CurrentIndex);

	m_MoveRestrictions = Collision()->GetMoveRestrictions(IsSwitchActiveCb, this, m_Pos, 18.0f, CurrentIndex);

	if(!m_TuneZone)
		m_Vel.y += GlobalTuning()->m_Gravity;
	else
		m_Vel.y += TuningList()[m_TuneZone].m_Gravity;

	HandleSkippableTiles(CurrentIndex);

	// tiles
	std::vector<int> vIndices = Collision()->GetMapIndices(m_PrevPos, m_Pos);
	if(!vIndices.empty())
	{
		for(int &Index : vIndices)
		{
			HandleTiles(Index);
		}
	}
	else
	{
		HandleTiles(CurrentIndex);
	}

	bool Grounded = false;
	// A true return means a solid quad was involved; those can move, so a drop
	// riding one must keep simulating or it would detach from the platform.
	const bool TouchedQuad = Collision()->MoveBox(&m_Pos, &m_Vel, CCharacterCore::PhysicalSizeVec2(), m_GroundElasticity, &Grounded);
	if(Grounded)
		m_Vel.x *= 0.88f;
	m_Vel.x *= 0.98f;

	const bool WasInFreeze = m_InsideFreeze;
	if(m_InsideFreeze)
	{
		m_Vel.y -= 0.05f; // slowly float up
		if(!m_TuneZone)
			m_Vel.y -= GlobalTuning()->m_Gravity;
		else
			m_Vel.y -= TuningList()[m_TuneZone].m_Gravity;
		m_InsideFreeze = false; // Reset for the next tick
	}

	// Snap sends (int)m_Pos, so a drop that keeps landing on the same pixel is
	// already frozen as far as every client is concerned, however much sub-pixel
	// velocity is left to bleed off.
	const bool SamePixel = (int)m_Pos.x == (int)m_PrevPos.x && (int)m_Pos.y == (int)m_PrevPos.y;
	if(Grounded && !WasInFreeze && !TouchedQuad && SamePixel)
	{
		if(++m_RestTicks >= SLEEP_AFTER_RESTING_TICKS)
		{
			m_Sleeping = true;
			m_SleepRecheck = SLEEP_RECHECK_TICKS;
			m_Vel = vec2(0.0f, 0.0f);
		}
	}
	else
	{
		m_RestTicks = 0;
	}

	m_PrevPos = m_Pos;
}

void CPickupDrop::HandleSkippableTiles(int Index)
{
	// handle death-tiles and leaving gamelayer
	if((Collision()->GetCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) == TILE_DEATH ||
		   Collision()->GetCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) == TILE_DEATH ||
		   Collision()->GetCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) == TILE_DEATH ||
		   Collision()->GetCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) == TILE_DEATH ||
		   Collision()->GetFrontCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) == TILE_DEATH ||
		   Collision()->GetFrontCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) == TILE_DEATH ||
		   Collision()->GetFrontCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) == TILE_DEATH ||
		   Collision()->GetFrontCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) == TILE_DEATH))
	{
		if(GameServer()->m_pController->Teams().IsPractice(Team()))
		{
			if(g_Config.m_SvWeaponDropsInFreezeFloat)
				m_InsideFreeze = true;
		}
		else
		{
			Reset();
			return;
		}
	}

	// <FoxNet
	if(GameLayerClipped(m_Pos))
	{
		Reset();
		return;
	}

	if(Index < 0)
		return;

	// handle speedup tiles
	if(Collision()->IsSpeedup(Index))
	{
		vec2 Direction, TempVel = m_Vel;
		int Force, Type, MaxSpeed = 0;
		Collision()->GetSpeedup(Index, &Direction, &Force, &MaxSpeed, &Type);

		if(Type == TILE_SPEED_BOOST_OLD)
		{
			float TeeAngle, SpeederAngle, DiffAngle, SpeedLeft, TeeSpeed;
			if(Force == 255 && MaxSpeed)
			{
				m_Vel = Direction * (MaxSpeed / 5);
			}
			else
			{
				if(MaxSpeed > 0 && MaxSpeed < 5)
					MaxSpeed = 5;
				if(MaxSpeed > 0)
				{
					if(Direction.x > 0.0000001f)
						SpeederAngle = -std::atan(Direction.y / Direction.x);
					else if(Direction.x < 0.0000001f)
						SpeederAngle = std::atan(Direction.y / Direction.x) + 2.0f * std::asin(1.0f);
					else if(Direction.y > 0.0000001f)
						SpeederAngle = std::asin(1.0f);
					else
						SpeederAngle = std::asin(-1.0f);

					if(SpeederAngle < 0)
						SpeederAngle = 4.0f * std::asin(1.0f) + SpeederAngle;

					if(TempVel.x > 0.0000001f)
						TeeAngle = -std::atan(TempVel.y / TempVel.x);
					else if(TempVel.x < 0.0000001f)
						TeeAngle = std::atan(TempVel.y / TempVel.x) + 2.0f * std::asin(1.0f);
					else if(TempVel.y > 0.0000001f)
						TeeAngle = std::asin(1.0f);
					else
						TeeAngle = std::asin(-1.0f);

					if(TeeAngle < 0)
						TeeAngle = 4.0f * std::asin(1.0f) + TeeAngle;

					TeeSpeed = std::sqrt(std::pow(TempVel.x, 2) + std::pow(TempVel.y, 2));

					DiffAngle = SpeederAngle - TeeAngle;
					SpeedLeft = MaxSpeed / 5.0f - std::cos(DiffAngle) * TeeSpeed;
					if(absolute((int)SpeedLeft) > Force && SpeedLeft > 0.0000001f)
						TempVel += Direction * Force;
					else if(absolute((int)SpeedLeft) > Force)
						TempVel += Direction * -Force;
					else
						TempVel += Direction * SpeedLeft;
				}
				else
					TempVel += Direction * Force;

				m_Vel = ClampVel(m_MoveRestrictions, TempVel);
			}
		}
		else if(Type == TILE_SPEED_BOOST)
		{
			constexpr float MaxSpeedScale = 5.0f;
			if(MaxSpeed == 0)
			{
				float MaxRampSpeed = GetTuning(m_TuneZone)->m_VelrampRange / (50 * log(std::max((float)GetTuning(m_TuneZone)->m_VelrampCurvature, 1.01f)));
				MaxSpeed = std::max(MaxRampSpeed, GetTuning(m_TuneZone)->m_VelrampStart / 50) * MaxSpeedScale;
			}

			// (signed) length of projection
			float CurrentDirectionalSpeed = dot(Direction, m_Vel);
			float TempMaxSpeed = MaxSpeed / MaxSpeedScale;
			if(CurrentDirectionalSpeed + Force > TempMaxSpeed)
				TempVel += Direction * (TempMaxSpeed - CurrentDirectionalSpeed);
			else
				TempVel += Direction * Force;

			m_Vel = ClampVel(m_MoveRestrictions, TempVel);
		}
	}
}

bool CPickupDrop::CheckArmor()
{
	// Every case below requires m_Type to be a vanilla weapon above the gun, so
	// custom weapon drops would scan every pickup on the map for nothing.
	if(m_Type <= WEAPON_GUN || m_Type >= NUM_WEAPONS)
		return false;

	CPickup *apEnts[9];
	int Num = GameWorld()->FindEntities(m_Pos, 34, (CEntity **)apEnts, 9, CGameWorld::ENTTYPE_PICKUP, MultiMapIdx());

	for(int i = 0; i < Num; i++)
	{
		switch(apEnts[i]->Type())
		{
		case POWERUP_ARMOR:
		{
			if(m_Type > WEAPON_GUN && m_Type < NUM_WEAPONS && apEnts[i]->GetOwnerId() < 0)
			{
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, PickupMask(-1));
				Reset(false);
				return true;
			}
		}
		break;

		case POWERUP_ARMOR_SHOTGUN:
		{
			if(m_Type == WEAPON_SHOTGUN && apEnts[i]->GetOwnerId() < 0)
			{
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, PickupMask(-1));
				Reset(false);
				return true;
			}
		}
		break;

		case POWERUP_ARMOR_GRENADE:
		{
			if(m_Type == WEAPON_GRENADE && apEnts[i]->GetOwnerId() < 0)
			{
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, PickupMask(-1));
				Reset();
				return true;
			}
		}
		break;

		case POWERUP_ARMOR_NINJA:
		{
			if(m_Type == WEAPON_NINJA && apEnts[i]->GetOwnerId() < 0)
			{
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, PickupMask(-1));
				Reset();
				return true;
			}
		}
		break;

		case POWERUP_ARMOR_LASER:
		{
			if(m_Type == WEAPON_LASER && apEnts[i]->GetOwnerId() < 0)
			{
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, PickupMask(-1));
				Reset();
				return true;
			}
		}
		break;

		default:
			break;
		}
	}
	return false;
}

bool CPickupDrop::CanBeSeenBy(int ClientId, int Asker)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || Server()->ClientSlotEmpty(ClientId))
		return false;

	CGameTeams &Teams = GameServer()->m_pController->Teams();
	if(!Teams.SetMask(ClientId, MultiMapIdx(), m_Team, -1, Asker, CGameContext::FLAG_SIX | CGameContext::FLAG_SIXUP, CGameTeams::IGNORE_SOLO))
		return false;

	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
	if(!pPlayer)
		return false;

	return pPlayer->Acc()->m_Configs.m_ShowWeaponDrops;
}

// m_SnappingClient starts at a value no real client can take, so the first lookup of
// the server's life always misses and initialises m_aVisible rather than reading zeros.
CPickupDrop::SSnapVisibility CPickupDrop::ms_SnapVisibility = {-1, -2, {}, {}};

bool CPickupDrop::CanSnapTo(int SnappingClient)
{
	const int MapIdx = MultiMapIdx();
	const bool Cacheable = m_Team >= 0 && m_Team < NUM_DDRACE_TEAMS;

	if(Cacheable)
	{
		if(ms_SnapVisibility.m_Tick != Server()->Tick() || ms_SnapVisibility.m_SnappingClient != SnappingClient)
		{
			ms_SnapVisibility.m_Tick = Server()->Tick();
			ms_SnapVisibility.m_SnappingClient = SnappingClient;
			for(signed char &Visible : ms_SnapVisibility.m_aVisible)
				Visible = -1;
		}
		else if(ms_SnapVisibility.m_aVisible[m_Team] >= 0 && ms_SnapVisibility.m_aMultiMapIdx[m_Team] == MapIdx)
		{
			return ms_SnapVisibility.m_aVisible[m_Team] != 0;
		}
	}

	const bool Visible =
		(SnappingClient < 0 || CanBeSeenBy(SnappingClient, SnappingClient)) &&
		GameServer()->m_pController->Teams().SetMaskWithFlags(SnappingClient, MapIdx, m_Team, CGameTeams::IGNORE_SOLO);

	if(Cacheable)
	{
		ms_SnapVisibility.m_aVisible[m_Team] = Visible ? 1 : 0;
		ms_SnapVisibility.m_aMultiMapIdx[m_Team] = MapIdx;
	}

	return Visible;
}

CClientMask CPickupDrop::PickupMask(int Asker)
{
	CClientMask Mask;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!CanBeSeenBy(ClientId, Asker))
			continue;

		Mask.set(ClientId);
	}
	return Mask;
}

bool CPickupDrop::IsGrounded()
{
	if(Collision()->CheckPoint(m_Pos.x + GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;
	if(Collision()->CheckPoint(m_Pos.x - GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;

	int MoveRestrictionsBelow = Collision()->GetMoveRestrictions(m_Pos + vec2(0, GetProximityRadius() / 2 + 4), 0.0f);
	return (MoveRestrictionsBelow & CANTMOVE_DOWN) != 0;
}

bool CPickupDrop::CollectItem()
{
	if(m_PickupDelay > 0)
	{
		m_PickupDelay--;
		return false;
	}

	float Radius = 32.0f;

	const int Max = 8;
	CCharacter *pClosest[Max];
	int Num = GameWorld()->FindCharacters(m_Pos, Radius, (CEntity **)pClosest, Max, MultiMapIdx());
	// find closest valid character
	for(int i = 0; i < Num; i++)
	{
		if(m_Team != TEAM_SUPER && pClosest[i]->Team() != TEAM_SUPER && m_Team != pClosest[i]->Team())
			continue;
		if(pClosest[i]->Core()->m_aWeapons[m_Type].m_Got)
			continue;

		pClosest[i]->GiveWeapon(m_Type);
		pClosest[i]->SetActiveWeapon(m_Type);
		const int PickerId = pClosest[i]->GetPlayer() ? pClosest[i]->GetPlayer()->GetCid() : -1;
		GameServer()->CreateSound(pClosest[i]->m_Pos, SOUND_PICKUP_HEALTH, pClosest[i]->TeamMask() & PickupMask(PickerId));

		if(pClosest[i]->GetPlayer())
			GameServer()->SendWeaponPickup(pClosest[i]->GetPlayer()->GetCid(), m_Type);

		Reset(true);
		return true;
	}
	return false;
}

void CPickupDrop::HandleTiles(int Index)
{
	int MultiMapIdx = Index;
	// int PureMapIndex = Collision()->GetPureMapIndex(m_Pos);
	m_TileIndex = Collision()->GetTileIndex(MultiMapIdx);
	m_TileFIndex = Collision()->GetFrontTileIndex(MultiMapIdx);

	int TeleCheckpoint = Collision()->IsTeleCheckpoint(MultiMapIdx);
	if(TeleCheckpoint)
		m_TeleCheckpoint = TeleCheckpoint;

	m_Vel = ClampVel(m_MoveRestrictions, m_Vel);
	if(m_TileIndex == TILE_FREEZE || m_TileFIndex == TILE_FREEZE)
	{
		if(g_Config.m_SvWeaponDropsInFreezeFloat)
			m_InsideFreeze = true;
	}

	// teleporters
	int z = Collision()->IsTeleport(MultiMapIdx);
	if(!g_Config.m_SvOldTeleportHook && !g_Config.m_SvOldTeleportWeapons && z && !Collision()->TeleOuts(z - 1).empty())
	{
		int TeleOut = GameWorld()->m_Core.RandomOr0(Collision()->TeleOuts(z - 1).size());
		m_Pos = Collision()->TeleOuts(z - 1)[TeleOut];
		return;
	}
	int evilz = Collision()->IsEvilTeleport(MultiMapIdx);
	if(evilz && !Collision()->TeleOuts(evilz - 1).empty())
	{
		int TeleOut = GameWorld()->m_Core.RandomOr0(Collision()->TeleOuts(evilz - 1).size());
		m_Pos = Collision()->TeleOuts(evilz - 1)[TeleOut];
		if(!g_Config.m_SvOldTeleportHook && !g_Config.m_SvOldTeleportWeapons)
		{
			m_Vel = vec2(0, 0);
		}
		return;
	}
	if(Collision()->IsCheckEvilTeleport(MultiMapIdx))
	{
		// first check if there is a TeleCheckOut for the current recorded checkpoint, if not check previous checkpoints
		for(int k = m_TeleCheckpoint - 1; k >= 0; k--)
		{
			if(!Collision()->TeleCheckOuts(k).empty())
			{
				int TeleOut = GameWorld()->m_Core.RandomOr0(Collision()->TeleCheckOuts(k).size());
				m_Pos = Collision()->TeleCheckOuts(k)[TeleOut];
				m_Vel = vec2(0, 0);

				return;
			}
		}
		// if no checkpointout have been found (or if there no recorded checkpoint), teleport to start
		vec2 SpawnPos;
		if(GameServer()->m_pController->CanSpawn(0, &SpawnPos, m_Team))
		{
			m_Pos = SpawnPos;
			m_Vel = vec2(0, 0);
		}
		return;
	}
	if(Collision()->IsCheckTeleport(MultiMapIdx))
	{
		// first check if there is a TeleCheckOut for the current recorded checkpoint, if not check previous checkpoints
		for(int k = m_TeleCheckpoint - 1; k >= 0; k--)
		{
			if(!Collision()->TeleCheckOuts(k).empty())
			{
				int TeleOut = GameWorld()->m_Core.RandomOr0(Collision()->TeleCheckOuts(k).size());
				m_Pos = Collision()->TeleCheckOuts(k)[TeleOut];
				return;
			}
		}
		// if no checkpointout have been found (or if there no recorded checkpoint), teleport to start
		vec2 SpawnPos;
		if(GameServer()->m_pController->CanSpawn(0, &SpawnPos, m_Team))
		{
			m_Pos = SpawnPos;
		}
		return;
	}
}

void CPickupDrop::TakeDamage(vec2 Force)
{
	vec2 Temp = m_Vel + Force;
	m_Vel = ClampVel(m_MoveRestrictions, Temp);
	WakeUp();
}

void CPickupDrop::ForceSetPos(vec2 Pos)
{
	m_Pos = Pos;
	m_PrevPos = Pos;
	WakeUp();
}

void CPickupDrop::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(!CanSnapTo(SnappingClient))
		return;

	int Tick = Server()->Tick() - m_StartTick;

	// Make the pickup blink when about to disappear
	if(m_Lifetime < Server()->TickSpeed() * 10 && (Tick / (Server()->TickSpeed() / 4)) % 2 == 0)
		return;

	const int SubType = GameServer()->GetWeaponType(m_Type);

	const int Alpha = 100;

	if(GetId().has_value())
		SnapPickupDropPickup(SnappingClient, GetId().value(), PICKUPFLAG_NO_PREDICT, m_Pos, POWERUP_WEAPON, SubType, 0, Alpha);

	vec2 Offset = vec2(0.0f, -32.0f);
	if(m_Type == WEAPON_HEARTGUN)
	{
		if(m_aIds[0].has_value())
			SnapPickupDropPickup(SnappingClient, m_aIds[0].value(), PICKUPFLAG_NO_PREDICT, m_Pos + Offset, POWERUP_HEALTH, SubType, 0, Alpha, 0);
	}
	else if(m_Type == WEAPON_LIGHTSABER)
	{
		if(m_aIds[0].has_value())
			SnapPickupDropLaser(SnappingClient, m_aIds[0].value(), m_Pos + Offset, m_Pos + Offset, LASERTYPE_GUN, Alpha, COSMETIC_LASER_FLAG_FROM_HEAD);
	}
	else if(m_Type == WEAPON_PORTALGUN)
	{
		if(m_aIds[0].has_value())
			SnapPickupDropLaser(SnappingClient, m_aIds[0].value(), m_Pos + Offset, m_Pos + Offset, LASERTYPE_GUN, Alpha, COSMETIC_LASER_FLAG_FROM_HEAD);
		const vec2 Spin = vec2(cos(Tick / 5.0f), sin(Tick / 5.0f)) * 17.0f + Offset;


		SnapPickupDropProjectile(m_aIds[1].value(), SnappingClient, WEAPON_HAMMER, m_Pos + Spin, vec2(0, 0));
	}
	else if(m_Type == WEAPON_METEOR)
	{
		const float Time = Tick * 0.25f;
		const vec2 Spin = normalize(vec2(cos(Time), sin(Time)));

		SnapPickupDropProjectile(m_aIds[0].value(), SnappingClient, WEAPON_GRENADE, m_Pos + Offset, Spin);
	}
}
