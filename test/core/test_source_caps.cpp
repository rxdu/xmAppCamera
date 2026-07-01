#include "xmcam/core/source_caps.hpp"

#include <gtest/gtest.h>

using namespace xmotion;

TEST(SourceCaps, AddIsIdempotentPerFormat) {
  SourceCaps caps;
  auto& mjpg = caps.AddFormat(PixelFormat::kMjpeg,
                              FourCC('M', 'J', 'P', 'G'), "Motion-JPEG", true);
  mjpg.sizes.push_back(FrameSize{1920, 1200, {90, 60, 30}});

  // Re-adding the same format returns the existing entry (no duplicate).
  auto& again = caps.AddFormat(PixelFormat::kMjpeg,
                               FourCC('M', 'J', 'P', 'G'), "Motion-JPEG", true);
  again.sizes.push_back(FrameSize{640, 480, {90}});

  ASSERT_EQ(caps.formats.size(), 1u);
  EXPECT_EQ(caps.formats[0].sizes.size(), 2u);
  EXPECT_TRUE(caps.formats[0].compressed);
}

TEST(SourceCaps, FindByFormat) {
  SourceCaps caps;
  caps.AddFormat(PixelFormat::kYuyv, FourCC('Y', 'U', 'Y', 'V'), "YUYV", false);
  ASSERT_NE(caps.Find(PixelFormat::kYuyv), nullptr);
  EXPECT_FALSE(caps.Find(PixelFormat::kYuyv)->compressed);
  EXPECT_EQ(caps.Find(PixelFormat::kNv12), nullptr);
}
