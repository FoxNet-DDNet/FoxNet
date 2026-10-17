#ifndef GAME_SERVER_FOXNET_INTEGRATION_MOTION_ABI_H
#define GAME_SERVER_FOXNET_INTEGRATION_MOTION_ABI_H

// Optional, size-checked V1 observation API. All arrays use X/Y order.
// Kept identical in AntiBot and FoxNet; never append fields to V1.
enum
{
	ANTIBOB_MOTION_TICKS = 4,
	ANTIBOB_MOTION_FROZEN = 1 << 0,
	ANTIBOB_MOTION_SPECIAL = 1 << 1,
};

struct CAntibobMotionInputV1
{
	int m_Direction;
	int m_Jump;
	int m_Hook;
	int m_TargetX;
	int m_TargetY;
};

struct CAntibobMotionStateV1
{
	int m_Tick;
	int m_SpawnTick;
	int m_MapIndex;
	int m_TuneZone;
	unsigned int m_TuningHash;
	unsigned int m_Flags;
	CAntibobMotionInputV1 m_Input;
	float m_Pos[2];
	float m_Vel[2];
	int m_HookState;
	int m_HookLaunched;
	int m_Jumped;
	int m_JumpedTotal;
	int m_Jumps;
	int m_ActiveWeapon;
};

struct CAntibobMotionForecastV1
{
	float m_aPos[ANTIBOB_MOTION_TICKS][2];
	float m_aVel[ANTIBOB_MOTION_TICKS][2];
};

static_assert(sizeof(CAntibobMotionInputV1) == 20);
static_assert(sizeof(CAntibobMotionStateV1) == 84);
static_assert(sizeof(CAntibobMotionForecastV1) == 64);

#endif
