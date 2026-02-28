/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

#include "teeinfo.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/alloc.h>
#include <game/server/save.h>

// <FoxNet
#include <memory>
#include <optional>
#include "foxnet/cosmetics/pickup_pet.h"
#include "foxnet/entities/pickupdrop.h"
#include "foxnet/shop.h"

class CAccountSession;

class CHeadItem;
// FoxNet>

class CCharacter;
class CGameContext;
class IServer;
struct CNetObj_PlayerInput;
struct CScorePlayerResult;

// <FoxNet
constexpr int LootBoxOpeningTicks = SERVER_TICK_SPEED * 6;

enum Areas
{
	AREA_GAME = 0,
	AREA_ROULETTE = 1,
	NUM_AREAS
};

enum HookTypes
{
	HOOKTYPE_NORMAL = 0,
	HOOKTYPE_BLOODY,
	HOOKTYPE_RAINBOW,
	NUM_HOOKTYPES
};

enum Indicators
{
	INDTYPE_NONE = 0,
	INDTYPE_CLOCKWISE,
	INDTYPE_COUNTERWISE,
	INDTYPE_INWARD,
	INDTYPE_OUTWARD,
	INDTYPE_LINE,
	INDTYPE_CRISSCROSS,
	NUM_INDTYPES
};

enum KillEffects
{
	DEATHTYPE_NONE = 0,
	DEATHTYPE_HAMMERHIT,
	DEATHTYPE_EXPLOSION,
	DEATHTYPE_DAMAGEIND,
	DEATHTYPE_LASER,
	NUM_DEATHTYPES
};

enum TrailTypes
{
	TRAILTYPE_NONE = 0,
	TRAILTYPE_STAR,
	TRAILTYPE_DOT,
	NUM_TRAILTYPES
};

enum GunTypes
{
	GUNTYPE_NONE = 0,
	GUNTYPE_HEART,
	GUNTYPE_MIXED,
	GUNTYPE_LASER,
	NUM_GUNTYPES
};

enum class EHatType
{
	None = 0,
	Hammer,
	Gun,
	Shotgun,
	Grenade,
	Laser,
	Ninja,
	Party,
	COUNT
};

class CCosmetics
{
public:
	int m_Ability = 0;
	bool m_EpicCircle = false;
	bool m_Lovely = false;
	bool m_RotatingBall = false;
	bool m_Sparkle = false;
	int m_HookPower = 0;
	bool m_Bloody = false;
	bool m_InverseAim = false;

	int m_DeathEffect = 0;
	int m_DamageIndType = 0;

	// Guns
	int m_EmoticonGun = 0;
	bool m_ConfettiGun = false;
	bool m_PhaseGun = false;

	int m_GunType = 0;

	// Trails
	int m_Trail = 0;

	// Hats
	bool m_HeartHat = false;
	EHatType m_HatType = EHatType::None;

	// Rainbow
	bool m_RainbowFeet = false;
	bool m_RainbowBody = false;
	int m_RainbowSpeed = 2; // Default speed

	bool m_StrongBloody = false;
	bool m_StaffInd = false;
	bool m_PickupPet = false;
	bool m_Lissajous = false;
	bool m_Halo = false;

	void Reset() { *this = CCosmetics(); }
};

class CInventoryEntry
{
public:
	int m_Quantity = 0;
	int m_Value = 0;  
	int64_t m_AcquiredAt = 0;
	int64_t m_ExpiresAt = -1;

	bool operator==(const CInventoryEntry &Other) const
	{
		return m_Quantity == Other.m_Quantity &&
		       m_Value == Other.m_Value &&
		       m_AcquiredAt == Other.m_AcquiredAt &&
		       m_ExpiresAt == Other.m_ExpiresAt;
	}
	bool operator!=(const CInventoryEntry &Other) const
	{
		return !(*this == Other);
	}
};

class CInventory
{
public:
	std::unordered_map<std::string, CInventoryEntry> m_Map;
	CCosmetics m_Cosmetics;

	bool Has(const std::string &Name) const
	{
		return m_Map.find(Name) != m_Map.end();
	}

	bool Owns(const char *pName) const
	{
		auto it = m_Map.find(std::string(pName));
		return it != m_Map.end() && it->second.m_Quantity > 0;
	}

	CInventoryEntry &Entry(const std::string &Name) { return m_Map[Name]; }
	void SetQuantity(const char *pName, int Quantity) { Entry(pName).m_Quantity = Quantity; }
	void SetValue(const char *pName, int Equipped) { Entry(pName).m_Value = Equipped; }
	void SetAcquiredAt(const char *pName, int64_t AcquiredAt) { Entry(pName).m_AcquiredAt = AcquiredAt; }
	void SetExpiresAt(const char *pName, int64_t ExpiredAt) { Entry(pName).m_ExpiresAt = ExpiredAt; }
	void AddToExpiry(const char *pName, int64_t delta) { Entry(pName).m_ExpiresAt += delta; }

	void Reset()
	{
		m_Map.clear();
		m_Cosmetics.Reset();
	}
};
// FoxNet>

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, uint32_t UniqueClientId, int ClientId, int Team);
	~CPlayer();

	void Reset();

	void TryRespawn();
	void Respawn(bool WeakHook = false); // with WeakHook == true the character will be spawned after all calls of Tick from other Players
	CCharacter *ForceSpawn(vec2 Pos); // required for loading savegames
	void SetTeam(int Team, bool DoChatMsg = true);
	int GetTeam() const { return m_Team; }
	int GetCid() const { return m_ClientId; }
	uint32_t GetUniqueCid() const { return m_UniqueClientId; }
	int GetClientVersion() const;
	bool SetTimerType(int TimerType);

	void Tick();
	void PostTick();

	// will be called after all Tick and PostTick calls from other players
	void PostPostTick();
	void Snap(int SnappingClient);
	void FakeSnap();

	void OnDirectInput(const CNetObj_PlayerInput *pNewInput);
	void OnPredictedInput(const CNetObj_PlayerInput *pNewInput);
	void OnPredictedEarlyInput(const CNetObj_PlayerInput *pNewInput);
	void OnDisconnect();

	void KillCharacter(int Weapon = WEAPON_GAME, bool SendKillMsg = true);
	CCharacter *GetCharacter();
	const CCharacter *GetCharacter() const;

	void SpectatePlayerName(const char *pName);

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;
	int m_TuneZone;
	int m_TuneZoneOld;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aCurLatency[MAX_CLIENTS];

	int m_SentSnaps = 0;

	int SpectatorId() const { return m_SpectatorId; }
	void SetSpectatorId(int Id);

	bool m_IsReady;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastEmoteGlobal;
	int m_LastKill;
	int m_aLastCommands[4];
	int m_LastCommandPos;
	int m_LastWhisperTo;
	int m_LastInvited;

	CTeeInfo m_TeeInfos;

	int m_DieTick;
	int m_PreviousDieTick;
	int m_JoinTick;
	int m_LastActionTick;
	int m_TeamChangeTick;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

private:
	const uint32_t m_UniqueClientId;
	CCharacter *m_pCharacter;
	int m_NumInputs;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	bool m_WeakHookSpawn;
	int m_ClientId;
	int m_Team;

	// used for spectator mode
	int m_SpectatorId;

	int m_Paused;
	int64_t m_ForcePauseTime;
	int64_t m_LastPause;
	bool m_Afk;

	int m_DefEmote;
	int m_OverrideEmote;
	int m_OverrideEmoteReset;
	bool m_Halloween;

public:
	enum
	{
		PAUSE_NONE = 0,
		PAUSE_PAUSED,
		PAUSE_SPEC
	};

	enum
	{
		TIMERTYPE_DEFAULT = -1,
		TIMERTYPE_GAMETIMER,
		TIMERTYPE_BROADCAST,
		TIMERTYPE_GAMETIMER_AND_BROADCAST,
		TIMERTYPE_SIXUP,
		TIMERTYPE_NONE,
	};

	bool m_DND;
	bool m_Whispers;
	int64_t m_FirstVoteTick;
	char m_aTimeoutCode[64];

	void ProcessPause();
	int Pause(int State, bool Force);
	int ForcePause(int Time);
	int IsPaused() const;
	bool CanSpec() const;

	bool IsPlaying() const;
	int64_t m_LastKickVote;
	int64_t m_LastDDRaceTeamChange;
	int m_ShowOthers;
	bool m_ShowAll;
	bool m_EnableSpectatorCount;
	vec2 m_ShowDistance;
	bool m_SpecTeam;
	bool m_NinjaJetpack;

	// camera info is used sparingly for converting aim target to absolute world coordinates
	class CCameraInfo
	{
		friend class CPlayer;
		bool m_HasCameraInfo;
		float m_Zoom;
		int m_Deadzone;
		int m_FollowFactor;

	public:
		vec2 ConvertTargetToWorld(vec2 Position, vec2 Target) const;
		void Write(const CNetMsg_Cl_CameraInfo *pMsg);
		void Reset();
		// <FoxNet
		float GetZoom() const { return m_Zoom; }
		// FoxNet>

	} m_CameraInfo;

	int m_ChatScore;

	bool m_Moderating;

	void UpdatePlaytime();
	void AfkTimer();
	void SetAfk(bool Afk);
	void SetInitialAfk(bool Afk);
	bool IsAfk() const { return m_Afk; }

	int64_t m_LastPlaytime;
	int64_t m_LastEyeEmote;
	int64_t m_LastBroadcast;
	bool m_LastBroadcastImportance;

	CNetObj_PlayerInput *m_pLastTarget;
	bool m_LastTargetInit;

	bool m_EyeEmoteEnabled;
	int m_TimerType;

	int GetDefaultEmote() const;
	void OverrideDefaultEmote(int Emote, int Tick);
	bool CanOverrideDefaultEmote() const;

	bool m_FirstPacket;
	int64_t m_LastSqlQuery;
	void ProcessScoreResult(CScorePlayerResult &Result);
	std::shared_ptr<CScorePlayerResult> m_ScoreQueryResult;
	std::shared_ptr<CScorePlayerResult> m_ScoreFinishResult;
	bool m_NotEligibleForFinish;
	int64_t m_EligibleForFinishCheck;
	bool m_VotedForPractice;
	int m_SwapTargetsClientId; // Client Id of the swap target for the given player
	bool m_BirthdayAnnounced;

	int m_RescueMode;

	CSaveTee m_LastTeleTee;
	std::optional<CSaveTee> m_LastDeath;

	// <FoxNet
private:
	void FoxNetReset();
	void Overriddename(int SnappingClient, CNetObj_ClientInfo *pClientInfo);

	int m_RainbowColor = 0;
	void OverrideSnap(int SnappingClient, CNetObj_ClientInfo *pClientInfo);
	void RainbowSnap(int SnappingClient, CNetObj_ClientInfo *pClientInfo);
	void RainbowTick();
	void ExpireItems();
	void FoxNetTick();

	void LootBoxTick();

	int m_Area = 0;
	void SendAreaMotd(int Area);

	void SendBroadcast(const char *pText);

	class CLootBoxData
	{
	public:
		CLootBoxData() = default;

		bool m_Opening = false;
		const CItemConfig *m_pGotItem = nullptr;
		const CItemConfig *m_pLootBox = nullptr;
		int m_Days = 0;

		int m_Ticks = 0;

	} m_LootBoxData;

	bool OpenLootCase(const CItemConfig &CaseCfg);	

	bool HasImportantBroadcast() const;

public:

	int64_t m_LastReport = 0;

	int m_BetAmount = -1;
	int64_t m_LastBet = 0;

	void SetArea(int Area);
	int GetArea() const { return m_Area; }

	bool m_WeaponIndicator = true;

	int m_AccLoginAttempts;
	int m_AccRegisters;

	CAccountSession *Acc();
	CInventory *Inv();
	CCosmetics *Cosmetics();

	bool m_Invisible = false;
	bool m_Vanish = false;
	int m_ExtraPing = 0;
	bool m_IgnoreGamelayer = false;
	bool m_TelekinesisImmunity = false;
	bool m_IncludeServerInfo;

	bool m_Obfuscated = false;
	bool m_SpiderHook = false;
	bool m_Spazzing = false;

	bool CanUseMoney();

	void GivePlaytime(long Amount);
	void GiveXP(long Amount, const char *pMessage = "", bool Multiplier = true);
	bool CheckLevelUp(long Amount, bool Silent = false);

	void GiveMoney(long Amount, bool Multiplier = true, bool Silent = false);
	void TakeMoney(long Amount, bool Silent = false) { GiveMoney(-Amount, false, Silent); }

	long GetDiscountedPrice(long Price);

	bool OwnsItem(const char *pItemName);
	bool OwnsItem(const EItemId ItemId);

	void UnequipExclusiveGroup(EExclusiveGroup Group, const CItemConfig *pExcept);
	bool UseItem(const char *pName, int OverrideValue, bool Force = false);

	bool ReachedItemLimit(const CItemConfig *Cfg);

	bool ItemEnabled(const char *pItemName);

	void HookPower(int Extra);
	void SetEmoticonGun(int Type);
	void SetExtraPing(int Type);
	void SetIgnoreGameLayer(bool Active);
	void SetObfuscated(bool Active);
	void SetInvisible(bool Active);
	void SetTelekinesisImmunity(bool Active);
	void SetAbility(int Type);

	// Player Settings
	// void SetHideCosmetics(bool Set);
	void SetHidePowerUps(bool Set);

	// Death Effect
	void SetDeathEffect(int Type);

	void SetHeartHat(bool Active);
	void SetHatType(EHatType Type);

	void SetStaffInd(bool Active);
	void SetPickupPet(bool Active);
	void SetLissajous(bool Active);
	void SetHalo(bool Active);

	void SetRainbowBody(bool Active);
	void SetRainbowFeet(bool Active);
	void SetSparkle(bool Active);
	void SetInverseAim(bool Active);
	void SetLovely(bool Active);
	void SetRotatingBall(bool Active);
	void SetEpicCircle(bool Active);
	void SetBloody(bool Active);
	void SetStrongBloody(bool Active);

	// Trails
	void SetTrail(int Type);

	// Gun effects
	void SetConfettiGun(bool Active);
	void SetPhaseGun(bool Active);
	void SetDamageIndType(int Type);
	void SetGunType(int Type);

	void DisableAllCosmetics();

	CPickupPet *m_pPickupPet;

	std::vector<CPickupDrop *> m_vPickupDrops;

	class CBroadcastData
	{
	public:
		char m_aMessage[1024];
		int64_t m_Time;
	} m_BroadcastData;

	int NumDDraceHudRows();
	void SendBroadcastHud(std::vector<std::string> pMessages, int Offset = -1);
	void ClearBroadcast() { return SendBroadcast(""); };

	float m_PredMargin;
	void Repredict(int PredMargin) { m_PredMargin = PredMargin / 10.0; };
	float GetClientPred();

	int GetSubPage();
	int GetPage();
	void SetPage(int Page);
	void SetSubPage(int SubPage);

	float StatMultiplier();

	bool m_MapOverridden = false;
	// FoxNet>
};
#endif
