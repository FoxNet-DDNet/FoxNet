#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_HIDENSEEK_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_HIDENSEEK_H

#include "zone.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>

#include <cstdint>
#include <vector>

class CHideAndSeekZone : public IZone
{
	enum class ESubType : uint8_t
	{
		Area = 0,
		Spawn,
		Hidden,
	};

	enum class EState
	{
		BadMap,
		WaitingForPlayers,
		Warmup,
		DoWarmup,
		Playing,
		Finished,
	} m_State = EState::WaitingForPlayers;

	enum class EWinState
	{
		None,
		Hiders,
		Seeker,
	};

	int64_t m_FinishedDelay = 0;

	int64_t m_WarmUpTime = 0;

	int m_SeekTimeTotal = 0;
	int m_SeekTimeRemaining = 0; // In Ticks

	int m_GameInfoTimeLimit = 0;
	int m_GameInfoRoundStartTick = 0;

	class CClientData
	{
	public:
		int64_t m_JoinedAt = 0;
		int64_t m_LastMovement = 0; // to prevent afks

		bool m_MarkedAfk = false;

		bool m_Alive = false;

		vec2 m_LastKnownPos = vec2(0, 0); // only gets set if Ghost Mode is off
		int m_GhostCooldown = 0; // In Ticks
		int m_GhostDuration = 0; // In Ticks

		int m_NumHiddenTicks = 0; // In Ticks

		bool m_InHiddenZone = false;

		bool m_IsSeeker = false;
		int m_GunReloadTimer = 0;
		int m_NumKills = 0;

		int m_NumWins = 0;

		void Reset()
		{
			m_JoinedAt = 0;
			m_LastMovement = 0;
			m_Alive = false;
			m_LastKnownPos = vec2(0, 0);
			m_GhostCooldown = 0;
			m_GhostDuration = 0;
			m_NumHiddenTicks = 0;
			m_InHiddenZone = false;
			m_IsSeeker = false;
			m_GunReloadTimer = 0;
			m_NumKills = 0;
		}

	} m_aClientData[MAX_CLIENTS];

	void ClientTick(int ClientId);

	// when m_State gets set to Playing, this function is called to move players and set them up for the game
	void StartGame();
	void EndGame(EWinState WinState);

	std::vector<vec2> m_vSpawnPoints;

	int m_SeekerTuneZone = -1;
	int m_HiderTuneZone = -1;
	int m_GhostTuneZone = -1;
	bool InitTuning();

	void SetGhost(int ClientId, int Duration);

	std::vector<CCharacter *> m_vCandidates;
	int GetNumCandidates();
	bool IsCandidate(int ClientId) const;
	vec2 GetRandomSpawnPos();
	int GetClosestHiderId(int SeekerId);
	bool TryReplaceAfkSeeker(int ClientId);

	void SetDead(int ClientId, bool SendKillMsg = true);

public:
	CHideAndSeekZone(CGameContext *pGameContext, size_t MapIndex) :
		IZone(pGameContext, MapIndex) {}

	void Init(CMapItemLayerQuads *pQuadsLayer) override;
	void OnTick() override;

	void OnClientDrop(int ClientId, const char *pReason) override;

	void OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx) override;

	int ShowOthers(CPlayer *pPlayer) override;
	bool CanUseCommand(CPlayer *pPlayer, const char *pCommand) override;
	bool CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget) override;
	bool CanSnapCharacter(CCharacter *pChr, int SnappingClient) override;
	bool CanDropWeapon(CCharacter *pChr, int Weapon) override;
	void OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg) override;
	bool OnCharacterFire(int ClientId, int Weapon) override;
	void OnCharacterHammerHit(int ClientId, int Target) override;
	bool SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags) override;

	void OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo *pClientInfo, int *pTeam, int *pLatency, int *pScore) override;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_HIDENSEEK_H