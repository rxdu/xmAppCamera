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

TEST(GstSource, SurfacesConnectionErrorOnBus) {
  // Nothing listens on this port: rtspsrc fails AFTER Start(), on the bus.
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kGstreamer;
  desc.pipeline = GstSource::DefaultPipelineForUri("rtsp://127.0.0.1:1/x");

  GstSource src;
  ASSERT_TRUE(src.Open(desc).ok());
  ASSERT_TRUE(src.Start().ok());
  EXPECT_EQ(src.state(), GstSource::State::kConnecting);

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (src.state() != GstSource::State::kError &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_EQ(src.state(), GstSource::State::kError);
  EXPECT_FALSE(src.last_error().empty());
  src.Close();
}

TEST(GstSource, ReachesPlayingState) {
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kGstreamer;
  desc.pipeline =
      "videotestsrc is-live=true ! video/x-raw,width=160,height=120,"
      "framerate=30/1 ! videoconvert ! video/x-raw,format=I420 ! "
      "appsink name=sink max-buffers=1 drop=true sync=false";

  GstSource src;
  ASSERT_TRUE(src.Open(desc).ok());
  ASSERT_TRUE(src.Start().ok());
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (src.state() != GstSource::State::kPlaying &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(src.state(), GstSource::State::kPlaying);
  EXPECT_TRUE(src.last_error().empty());
  src.Close();
}
