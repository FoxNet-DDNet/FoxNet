#ifndef GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_WHEELSPIN_H
#define GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_WHEELSPIN_H

#include <base/math.h>
#include <base/system.h>

#include <engine/shared/protocol.h>

#include <algorithm>
#include <cmath>
#include <random>

/*
 * The physics every spinning wheel game shares: it winds up, slows down, and comes to rest on a field
 * that was decided before the spin even started.
 *
 * It knows nothing about bets, money or players. An owner says how many fields the wheel has and how
 * field indices sit on it, then ticks it, draws from Rotation() and pays out on EndingField().
 *
 * The result being fixed up front is what lets the wheel ease onto the middle of the winning field
 * rather than resting on the seam between two: the gap to the centre is known when the spin starts,
 * so it can be spread across the slowdown instead of snapped at the end.
 */
class CWheelSpin
{
public:
	enum class EState
	{
		Idle = 0,
		Spinning,
		Stopping,
	};

	class CConfig
	{
	public:
		int m_NumFields = 0;

		/*
		 * How field indices sit on the wheel. The default is field 0 at the top with indices running
		 * one way; a game whose art or entity ring runs the other way sets m_Reverse, one whose
		 * field 0 is not at the top sets m_FieldOffset, and m_AngleOffset trims whatever is left.
		 */
		float m_AngleOffset = 0.0f;
		int m_FieldOffset = 0;
		bool m_Reverse = false;

		// Default: 2 * SERVER_TICK_SPEED
		int m_MinSpinTicks = 2 * SERVER_TICK_SPEED;
		// Default: 3 * SERVER_TICK_SPEED
		int m_MaxSpinTicks = 3 * SERVER_TICK_SPEED;

		// Default: 0.005f
		float m_Acceleration = 0.005f;
		// Default: 0.5f
		float m_MaxSpeed = 0.5f;
		// Default: 0.35f
		float m_MinSlowDown = 0.35f;
		// Default: 0.7f
		float m_MaxSlowDown = 0.70f;
	};

private:
	CConfig m_Config;

	float m_Rotation = 0.0f;
	float m_RotationSpeed = 0.0f;
	int m_SpinTicks = 0;
	float m_SlowDown = 1.0f;
	int m_EndingField = -1;
	// Per tick nudge during the slowdown, lands the wheel mid field instead of on an edge
	float m_LandingCorrection = 0.0f;
	EState m_State = EState::Idle;

	static constexpr float s_TwoPi = 2.f * pi;
	static constexpr float s_AngleTop = 3.f * pi / 2.f;

	int Wrap(int Index) const { return ((Index % m_Config.m_NumFields) + m_Config.m_NumFields) % m_Config.m_NumFields; }

	/*
	 * Replays the whole spin from where the wheel stands now. Deterministic, so the winner is known
	 * before anything moves. Also reports where it comes to rest and how long the slowdown runs.
	 */
	int Simulate(int SpinTicks, float SlowDown, float *pEndRotation, int *pStoppingTicks) const
	{
		float Rotation = m_Rotation;
		float Speed = m_RotationSpeed;
		EState State = EState::Spinning;
		int StoppingTicks = 0;

		// Step for step the same as Tick(), minus the landing correction that gets derived from it.
		// If the two ever drift apart the wheel stops somewhere other than the field it paid out on.
		while(true)
		{
			SpinTicks--;

			if(State == EState::Spinning)
			{
				Speed = std::min(Speed + m_Config.m_Acceleration, m_Config.m_MaxSpeed);
			}
			else
			{
				Speed -= m_Config.m_Acceleration * SlowDown;
				if(Speed < 0.0f)
				{
					if(pEndRotation)
						*pEndRotation = Rotation;
					if(pStoppingTicks)
						*pStoppingTicks = StoppingTicks;
					return GetField(Rotation);
				}
				// Only the ticks that still turn the wheel, the same ones Tick() corrects on
				StoppingTicks++;
			}

			Rotation += Speed;

			if(SpinTicks <= 0 && State == EState::Spinning)
				State = EState::Stopping;
		}
	}

public:
	void Init(const CConfig &Config)
	{
		m_Config = Config;
		PrepareNext();
	}

	[[nodiscard]] const CConfig &Config() const { return m_Config; }
	[[nodiscard]] EState State() const { return m_State; }
	[[nodiscard]] bool Idle() const { return m_State == EState::Idle; }
	[[nodiscard]] float Rotation() const { return m_Rotation; }
	[[nodiscard]] int EndingField() const { return m_EndingField; }
	[[nodiscard]] int NumFields() const { return m_Config.m_NumFields; }
	[[nodiscard]] float FieldAngle() const { return s_TwoPi / m_Config.m_NumFields; }

	// Which field the wheel points at for a given rotation
	[[nodiscard]] int GetField(float Rotation) const
	{
		if(m_Config.m_NumFields <= 0)
			return -1;

		// Nearest field rather than the one we happen to be past, so a field owns the angles around
		// its middle. Flooring a half field shifted angle is the same thing and matches the inverse.
		int Index = (int)std::floor((s_AngleTop + m_Config.m_AngleOffset - Rotation) / FieldAngle() + 0.5f);
		Index = Wrap(Index + m_Config.m_FieldOffset);
		if(m_Config.m_Reverse)
			Index = Wrap(m_Config.m_NumFields - Index);

		return Index;
	}

	// The rotation that puts the middle of a field at the top. The exact inverse of GetField.
	[[nodiscard]] float FieldCenterRotation(int Field) const
	{
		if(m_Config.m_NumFields <= 0)
			return 0.0f;

		int Index = m_Config.m_Reverse ? Wrap(m_Config.m_NumFields - Field) : Field;
		Index = Wrap(Index - m_Config.m_FieldOffset);

		float Rotation = std::fmod(s_AngleTop + m_Config.m_AngleOffset - Index * FieldAngle(), s_TwoPi);
		if(Rotation < 0.f)
			Rotation += s_TwoPi;

		return Rotation;
	}

	/*
	 * Where a field sits right now, for games that draw something per field instead of a pointer.
	 * The winning field always ends up at the top.
	 */
	[[nodiscard]] float FieldDrawAngle(int Field) const
	{
		return s_AngleTop + m_Rotation - FieldCenterRotation(Field);
	}

	// Rolls the next spin and works out where it will stop, without moving anything yet
	void PrepareNext()
	{
		if(m_Config.m_NumFields <= 0)
			return;

		std::uniform_int_distribution<int> TicksDist(m_Config.m_MinSpinTicks, m_Config.m_MaxSpinTicks);
		std::uniform_real_distribution<float> SlowDist(m_Config.m_MinSlowDown, m_Config.m_MaxSlowDown);

		m_SpinTicks = TicksDist(Rng());
		m_SlowDown = SlowDist(Rng());

		float EndRotation = 0.0f;
		int StoppingTicks = 0;
		m_EndingField = Simulate(m_SpinTicks, m_SlowDown, &EndRotation, &StoppingTicks);

		// The physics stops wherever it stops, often right on the seam between two fields. Spread the
		// gap to the middle of the winner across the slowdown so it eases onto the centre instead.
		float Offset = std::fmod(FieldCenterRotation(m_EndingField) - EndRotation, s_TwoPi);
		if(Offset > pi)
			Offset -= s_TwoPi;
		else if(Offset < -pi)
			Offset += s_TwoPi;

		// Never more than half a field: whatever happens the nudge must not walk off the winner
		const float HalfField = FieldAngle() * 0.5f;
		Offset = std::clamp(Offset, -HalfField, HalfField);

		m_LandingCorrection = StoppingTicks > 0 ? Offset / StoppingTicks : 0.0f;
	}

	void Start()
	{
		if(m_State == EState::Idle)
			m_State = EState::Spinning;
	}

	/*
	 * Advances one tick. Returns true on the tick the wheel comes to rest, which is when to pay out:
	 * EndingField() still holds the winner at that point, the next spin is rolled straight after.
	 */
	bool Tick()
	{
		if(m_State == EState::Idle)
			return false;

		m_SpinTicks--;

		bool Landed = false;
		if(m_State == EState::Spinning)
		{
			m_RotationSpeed = std::min(m_RotationSpeed + m_Config.m_Acceleration, m_Config.m_MaxSpeed);
		}
		else
		{
			m_RotationSpeed -= m_Config.m_Acceleration * m_SlowDown;
			if(m_RotationSpeed < 0.0f)
			{
				m_RotationSpeed = 0.0f;
				m_State = EState::Idle;
				Landed = true;
			}
			else
			{
				m_Rotation += m_LandingCorrection;
			}
		}

		m_Rotation += m_RotationSpeed;

		if(m_SpinTicks <= 0 && m_State == EState::Spinning)
			m_State = EState::Stopping;

		return Landed;
	}
};

#endif // GAME_SERVER_FOXNET_COMPONENTS_ZONES_GAMBLING_WHEELSPIN_H
