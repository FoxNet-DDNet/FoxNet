#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include <game/collision.h>
#include <game/layers.h>
#include <game/quad_data.h>

#include <utility>
#include <vector>

class CGameContext;
class IServer;
class CQuad;
class CMapItemLayerQuads;
class CPlayer;
class CCharacter;

class IZone
{
	CGameContext *m_pGameContext = nullptr;
	size_t m_MultiMapIndex = 0;
	EZoneType m_QuadType = EZoneType::Num;
	std::vector<int> m_vAnimatedQuadIndices;

	class CAnimationTransformCache
	{
	public:
		vec2 m_Position = vec2(0.0f, 0.f);
		float m_Angle = 0;
		int m_PosEnv = -1;
		int m_PosEnvOffset = 0;
	};
	void GetAnimationTransform(int MultiMapIndex, float GlobalTime, int Env, vec2 &Position, float &Angle) const;

protected:
	void ReserveQuads(int AdditionalQuads);
	void InitQuadData(CQuadData &QuadData, CQuad *pQuad) const;
	void AddQuad(const CQuadData &QuadData);

public:
	std::vector<CQuadData> m_vQuads;

	CGameContext *GameServer() const { return m_pGameContext; }
	IServer *Server() const;
	CCollision *Collision() const;

	[[nodiscard]] const std::vector<CQuadData> &Quads() const { return m_vQuads; }
	[[nodiscard]] size_t MultiMapIndex() const { return m_MultiMapIndex; }
	[[nodiscard]] bool HasAnimatedQuads() const { return !m_vAnimatedQuadIndices.empty(); }
	[[nodiscard]] bool InsideQuad(const vec2 &Pos, const CQuadData &QuadData, const vec2 &Size = vec2(0, 0)) const;
	void UpdateCache();

	IZone(CGameContext *pGameContext, size_t MapIndex, EZoneType QuadType = EZoneType::Num) :
		m_pGameContext(pGameContext), m_MultiMapIndex(MapIndex), m_QuadType(QuadType) {}

	virtual void Init(CMapItemLayerQuads *pQuadsLayer);
	virtual void OnTick() {}

	virtual void OnClientDrop(int ClientId, const char *pReason) {}

	virtual void OnGameInfoSnap(int ClientId, CNetObj_GameInfo *pGameInfoObj, CNetObj_GameInfoEx *pGameInfoEx) {}

	virtual int ShowOthers(CPlayer *pPlayer) { return -1; }
	virtual bool CanUseCommand(CPlayer *pPlayer, const char *pCommand) { return true; }
	virtual bool CanSpectateId(CPlayer *pPlayer, CPlayer *pTarget) { return true; }
	virtual bool CanSnapCharacter(CCharacter *pChr, int SnappingClient) { return true; }
	virtual bool CanDropWeapon(CCharacter *pChr, int Weapon) { return true; }
	virtual void OnCharacterDie(int ClientId, int Killer, int Weapon, bool SendKillMsg) {}
	virtual void OnCharacterSpawn(int ClientId, vec2 Pos) {}
	virtual bool OnCharacterFire(int ClientId, int Weapon) { return true; }
	virtual void OnCharacterHammerHit(int ClientId, int Target) {}
	virtual bool SetMask(int ClientId, int MultiMapIdx, int Team, int ExceptId, int Asker, int VersionFlags, int Flags) { return true; }

	virtual void OnPlayerSnap(CPlayer *pPlayer, int SnappingClient, CNetObj_ClientInfo *pClientInfo, int *pTeam, int *pLatency, int *pScore) {}

	virtual ~IZone() = default;
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_ZONE_H
