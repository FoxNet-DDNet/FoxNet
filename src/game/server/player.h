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

struct CAccountSession;
// FoxNet>

class CCharacter;
class CGameContext;
class IServer;
struct CNetObj_PlayerInput;
struct CScorePlayerResult;

// <FoxNet
enum Areas
{
	AREA_GAME = 0,
	AREA_ROULETTE = 1,
	NUM_AREAS
};

enum HookTypes
{
	HOOK_NORMAL = 0,
	HOOK_BLOODY,
	HOOK_RAINBOW,
	NUM_HOOKS
};

enum Indicators
{
	IND_NONE = 0,
	IND_CLOCKWISE,
	IND_COUNTERWISE,
	IND_INWARD,
	IND_OUTWARD,
	IND_LINE,
	IND_CRISSCROSS,
	NUM_DAMAGE_IND
};

enum KillEffects
{
	DEATH_NONE = 0,
	DEATH_HAMMERHIT,
	DEATH_EXPLOSION,
	DEATH_DAMAGEIND,
	DEATH_LASER,
	NUM_DEATHS
};

enum TrailTypes
{
	TRAIL_NONE = 0,
	TRAIL_STAR,
	TRAIL_DOT,
	NUM_TRAILS
};

enum GunTypes
{
	GUN_NONE = 0,
	GUN_HEART,
	GUN_MIXED,
	GUN_LASER,
	NUM_GUNS
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
	bool m_HeartHat = false;

	int m_DeathEffect = 0;
	int m_DamageIndType = 0;

	// Guns
	int m_EmoticonGun = 0;
	bool m_ConfettiGun = false;
	bool m_PhaseGun = false;

	int m_GunType = 0;

	// Trails
	int m_Trail = 0;

	// Rainbow
	bool m_RainbowFeet = false;
	bool m_RainbowBody = false;
	int m_RainbowSpeed = 2; // Default speed

	bool m_StrongBloody = false;
	bool m_StaffInd = false;
	bool m_PickupPet = false;

	void Reset() { *this = CCosmetics(); }
};

class CInventory
{
public:
	CCosmetics m_Cosmetics;

	bool m_aOwned[NUM_ITEMS] = {false};
	int m_aEquipped[NUM_ITEMS];

	int m_AcquiredAt = 0;
	int m_ExpiresAt = -1; // -1 means never expires

	void Reset()
	{
		mem_zero(m_aOwned, sizeof(m_aOwned));
		mem_zero(m_aEquipped, sizeof(m_aEquipped));
		m_Cosmetics = CCosmetics();
	}

	static int IndexOfName(const char *pName)
	{
		for(int i = 0; i < NUM_ITEMS; i++)
			if(!str_comp_nocase(Items[i], pName))
				return i;
		return -1;
	}
	static int IndexOfShortcut(const char *pShortcut)
	{
		for(int i = 0; i < NUM_ITEMS; i++)
			if(!str_comp_nocase(ItemShortcuts[i], pShortcut))
				return i;
		return -1;
	}
	static int IndexOf(const char *pNameOrShortcut)
	{
		int Idx = IndexOfName(pNameOrShortcut);
		if(Idx >= 0)
			return Idx;
		return IndexOfShortcut(pNameOrShortcut);
	}

	void SetOwnedIndex(int Index, bool Owned)
	{
		if(Index >= 0 && Index < NUM_ITEMS)
			m_aOwned[Index] = Owned;
	}
	bool OwnsIndex(int Index) const { return Index >= 0 && Index < NUM_ITEMS ? m_aOwned[Index] : false; }
	bool Owns(const char *pNameOrShortcut) const { return OwnsIndex(IndexOf(pNameOrShortcut)); }

	// Equipped
	void SetEquippedIndex(int Index, int Value)
	{
		if(Index >= 0 && Index < NUM_ITEMS)
			m_aEquipped[Index] = maximum(0, Value);
	}
	int EquippedIndex(int Index) const { return Index >= 0 && Index < NUM_ITEMS ? m_aEquipped[Index] : 0; }
	int Equipped(const char *pNameOrShortcut) const { return EquippedIndex(IndexOf(pNameOrShortcut)); }
	
	void SetAcquiredAt(int Index, int64_t _AcquiredAt)
	{
		if(Index >= 0 && Index < NUM_ITEMS)
			m_AcquiredAt = _AcquiredAt;
	}
	void SetExpiresAt(int Index, int64_t ExpiresAt)
	{
		if(Index >= 0 && Index < NUM_ITEMS)
			m_ExpiresAt = ExpiresAt;
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

	int m_SendVoteIndex;

	CTeeInfo m_TeeInfos;

	int m_DieTick;
	int m_PreviousDieTick;
	std::optional<int> m_Score;
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
	void OverrideName(int SnappingClient, CNetObj_ClientInfo *pClientInfo);

	int m_RainbowColor = 0;
	void RainbowSnap(int SnappingClient, CNetObj_ClientInfo *pClientInfo);
	void RainbowTick();
	void FoxNetTick();

	int m_Area = 0;
	void SendAreaMotd(int Area);

	void SendBroadcast(const char *pText);
public:
	int m_BetAmount = -1;
	int64_t m_LastBet = 0;

	void SetArea(int Area);
	int GetArea() const { return m_Area; }

	bool m_WeaponIndicator = true;
	bool m_HideCosmetics = false;
	bool m_HidePowerUps = false;

	CAccountSession *Acc();
	CInventory *Inv();
	CCosmetics *Cosmetics();

	bool m_Invisible = false;
	bool m_Vanish = false;
	int m_ExtraPing = 0;
	bool m_IgnoreGamelayer = false;
	bool m_TelekinesisImmunity = false;
	int m_IncludeServerInfo;

	bool m_Obfuscated = false;
	bool m_SpiderHook = false;
	bool m_Spazzing = false;

	void GivePlaytime(int Amount);
	void GiveXP(long Amount, const char *pMessage = "");
	bool CheckLevelUp(long Amount, bool Silent = false);
	void GiveMoney(long Amount, const char *pMessage = "", bool Multiplier = true);
	void TakeMoney(long Amount, bool Silent = true, const char *pMessage = "");

	bool OwnsItem(const char *pItemName);
	bool ToggleItem(const char *pItemName, int Set, bool IgnoreAccount = false);
	bool ReachedItemLimit(const char *pItem, int Set, int Value);

	int GetItemToggle(const char *pItemName);
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
	void SetHideCosmetics(bool Set);
	void SetHidePowerUps(bool Set);

	// Death Effect
	void SetDeathEffect(int Type);

	void SetPickupPet(bool Active);
	void SetHeartHat(bool Active);
	void SetStaffInd(bool Active);

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
	// FoxNet>
};

#endif
