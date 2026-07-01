// Enumeration is hardware-dependent: skip cleanly when no camera is present so
// the suite stays green in CI, but assert real invariants when one exists.
#include "xmcam/pipeline/v4l2_source.hpp"

#include <gtest/gtest.h>

using namespace xmotion;

TEST(V4l2Enumerate, DevicesHaveNonEmptyCaps) {
  auto devices = V4l2Source::Enumerate();
  if (devices.empty()) GTEST_SKIP() << "no V4L2 capture device present";

  for (const auto& d : devices) {
    EXPECT_FALSE(d.device.empty());
    // Enumerate() filters out format-less nodes (e.g. UVC metadata), so every
    // returned device must expose at least one format.
    EXPECT_FALSE(d.caps.empty()) << d.device << " has no formats";
    for (const auto& f : d.caps.formats) {
      EXPECT_FALSE(f.sizes.empty()) << d.device << " format has no sizes";
    }
  }
}

TEST(V4l2Enumerate, QueryCapsMatchesEnumerate) {
  auto devices = V4l2Source::Enumerate();
  if (devices.empty()) GTEST_SKIP() << "no V4L2 capture device present";

  SourceCaps caps;
  ASSERT_TRUE(V4l2Source::QueryCaps(devices.front().device, &caps).ok());
  EXPECT_FALSE(caps.empty());
}
