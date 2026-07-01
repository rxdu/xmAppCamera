#include "xmcam/core/pixel_format.hpp"

#include <gtest/gtest.h>

using namespace xmotion;

TEST(PixelFormat, V4l2FourccRoundTrip) {
  EXPECT_EQ(FromV4l2Fourcc(FourCC('Y', 'U', 'Y', 'V')), PixelFormat::kYuyv);
  EXPECT_EQ(FromV4l2Fourcc(FourCC('M', 'J', 'P', 'G')), PixelFormat::kMjpeg);
  EXPECT_EQ(FromV4l2Fourcc(FourCC('N', 'V', '1', '2')), PixelFormat::kNv12);
  EXPECT_EQ(FromV4l2Fourcc(FourCC('G', 'R', 'E', 'Y')), PixelFormat::kGray8);
  EXPECT_EQ(FromV4l2Fourcc(0xdeadbeef), PixelFormat::kUnknown);

  // Round-trip the expressible ones.
  for (auto f : {PixelFormat::kYuyv, PixelFormat::kNv12, PixelFormat::kGray8,
                 PixelFormat::kMjpeg, PixelFormat::kI420}) {
    EXPECT_EQ(FromV4l2Fourcc(ToV4l2Fourcc(f)), f) << ToString(f);
  }
}

TEST(PixelFormat, Classification) {
  EXPECT_TRUE(IsCompressed(PixelFormat::kMjpeg));
  EXPECT_TRUE(IsCompressed(PixelFormat::kH264));
  EXPECT_FALSE(IsCompressed(PixelFormat::kYuyv));

  EXPECT_TRUE(IsPlanar(PixelFormat::kNv12));
  EXPECT_TRUE(IsPlanar(PixelFormat::kI420));
  EXPECT_FALSE(IsPlanar(PixelFormat::kRgba8));
}

TEST(PixelFormat, BitsPerPixel) {
  EXPECT_EQ(BitsPerPixel(PixelFormat::kGray8), 8);
  EXPECT_EQ(BitsPerPixel(PixelFormat::kYuyv), 16);
  EXPECT_EQ(BitsPerPixel(PixelFormat::kNv12), 12);
  EXPECT_EQ(BitsPerPixel(PixelFormat::kRgba8), 32);
  EXPECT_EQ(BitsPerPixel(PixelFormat::kMjpeg), 0);
}

TEST(PixelFormat, GstNames) {
  EXPECT_EQ(FromGstFormatName("RGBA"), PixelFormat::kRgba8);
  EXPECT_EQ(FromGstFormatName("NV12"), PixelFormat::kNv12);
  EXPECT_EQ(FromGstFormatName("YUY2"), PixelFormat::kYuyv);
  EXPECT_EQ(FromGstFormatName("bogus"), PixelFormat::kUnknown);
}
