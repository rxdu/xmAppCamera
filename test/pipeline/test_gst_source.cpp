// GstSource tests. Fully self-contained: videotestsrc generates frames
// in-process, so no camera or network stream is required.
#include "xmcam/pipeline/gst_source.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace xmotion;

TEST(GstValidate, AcceptsGoodPipeline) {
  EXPECT_TRUE(GstSource::Validate("videotestsrc ! fakesink").ok());
}

TEST(GstValidate, RejectsBadPipeline) {
  auto st = GstSource::Validate("this_element_does_not_exist_42 ! fakesink");
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), ErrorCode::kInvalidArgument);
}

TEST(GstSource, DecodesVideoTestSrcToRgba) {
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kGstreamer;
  desc.pipeline =
      "videotestsrc num-buffers=60 ! "
      "video/x-raw,width=320,height=240,framerate=30/1 ! "
      "videoconvert ! video/x-raw,format=RGBA ! "
      "appsink name=sink max-buffers=2 sync=true";

  GstSource src;
  ASSERT_TRUE(src.Open(desc).ok());
  ASSERT_TRUE(src.Start().ok());

  int got = 0;
  bool format_ok = true;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (got < 10 && std::chrono::steady_clock::now() < deadline) {
    VideoFrame f;
    if (!src.Frames().TryPull(f)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    if (f.width != 320 || f.height != 240 || f.format != PixelFormat::kRgba8)
      format_ok = false;
    ++got;
  }
  src.Stop();
  src.Close();

  EXPECT_GE(got, 5) << "expected to catch several decoded frames";
  EXPECT_TRUE(format_ok) << "frames were not 320x240 RGBA";
}
