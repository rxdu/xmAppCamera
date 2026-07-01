#include "xmcam/core/util/rate_counter.hpp"

#include <gtest/gtest.h>

using namespace xmotion;
using Clock = RateCounter::Clock;

TEST(RateCounter, ConvergesToSteadyRate) {
  RateCounter rc(0.5);
  auto t = Clock::time_point{};  // epoch
  const auto step = std::chrono::milliseconds(20);  // 50 Hz

  double rate = 0.0;
  for (int i = 0; i < 200; ++i) {
    t += step;
    rate = rc.Tick(t);
  }
  EXPECT_NEAR(rate, 50.0, 1.0);
  EXPECT_EQ(rc.count(), 200u);
}

TEST(RateCounter, ResetClears) {
  RateCounter rc;
  auto t = Clock::time_point{};
  t += std::chrono::milliseconds(33);
  rc.Tick(t);
  rc.Reset();
  EXPECT_EQ(rc.count(), 0u);
  EXPECT_DOUBLE_EQ(rc.rate(), 0.0);
}
