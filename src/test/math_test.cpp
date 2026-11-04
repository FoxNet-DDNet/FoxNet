#include "test.h"

#include <base/math.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

TEST(Math, FixedPointRoundtrip)
{
	for(int i = 0; i < 100000; ++i)
	{
		const float Number = i / 1000.0f;
		EXPECT_NEAR(Number, fx2f(f2fx(Number)), 0.0005f);
		EXPECT_EQ(i, f2fx(fx2f(i)));
	}
}

TEST(Math, SaturatingMul)
{
	constexpr int64_t Max = std::numeric_limits<int64_t>::max();
	constexpr int64_t Min = std::numeric_limits<int64_t>::lowest();

	EXPECT_EQ(saturating_mul(0, Max), 0);
	EXPECT_EQ(saturating_mul(Min, 0), 0);
	EXPECT_EQ(saturating_mul(7, 6), 42);
	EXPECT_EQ(saturating_mul(-7, 6), -42);
	EXPECT_EQ(saturating_mul(-7, -6), 42);

	// Exact at the boundary, clamped one step past it
	EXPECT_EQ(saturating_mul(Max / 36, 36), (Max / 36) * 36);
	EXPECT_EQ(saturating_mul(Max, 2), Max);
	EXPECT_EQ(saturating_mul(Max / 36 + 1, 36), Max);
	EXPECT_EQ(saturating_mul(Max, -2), Min);
	EXPECT_EQ(saturating_mul(Min, -1), Max);
	EXPECT_EQ(saturating_mul(Min, 1), Min);
}
