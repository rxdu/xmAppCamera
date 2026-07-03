// AppController session-manager tests. Uses GStreamer videotestsrc sessions so
// they run without any camera (CI-safe); V4L2 paths are covered by the
// hardware-gated pipeline tests.
#include "xmcam/app/app_controller.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace xmotion;

#ifdef XMCAM_WITH_GSTREAMER

namespace {
constexpr const char* kTestPipeline =
    "videotestsrc is-live=true ! video/x-raw,width=320,height=240,"
    "framerate=30/1 ! videoconvert ! video/x-raw,format=I420 ! "
    "appsink name=sink max-buffers=1 drop=true sync=false";
}  // namespace

TEST(Sessions, GstSessionLifecycle) {
  AppController app;
  ASSERT_TRUE(app.StartGst("net1", kTestPipeline).ok());

  ASSERT_EQ(app.sessions().size(), 1u);
  AppController::Session* s = app.FindSession("net1");
  ASSERT_NE(s, nullptr);
  EXPECT_TRUE(s->IsRunning());
  EXPECT_EQ(app.RunningCount(), 1u);
  EXPECT_EQ(app.selected_key(), "net1");
  EXPECT_EQ(s->pipeline, kTestPipeline);

  // Frames flow through the session.
  VideoFrame f;
  bool got = false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!got && std::chrono::steady_clock::now() < deadline) {
    got = app.PullFrame(*s, &f);
    if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_TRUE(got);
  EXPECT_EQ(f.width, 320);

  app.StopSession("net1");
  EXPECT_EQ(app.sessions().size(), 0u);
  EXPECT_EQ(app.RunningCount(), 0u);
  EXPECT_EQ(app.selected(), nullptr);
}

TEST(Sessions, RestartInPlaceKeepsSingleSession) {
  AppController app;
  ASSERT_TRUE(app.StartGst("net1", kTestPipeline).ok());
  const int id_before = app.FindSession("net1")->id;

  // Starting the same key again restarts in place (Apply), no new session.
  ASSERT_TRUE(app.StartGst("net1", kTestPipeline).ok());
  ASSERT_EQ(app.sessions().size(), 1u);
  EXPECT_EQ(app.FindSession("net1")->id, id_before);
  app.StopAll();
}

TEST(Sessions, SelectionFollowsAndOverrides) {
  AppController app;
  ASSERT_TRUE(app.StartGst("net1", kTestPipeline).ok());
  EXPECT_EQ(app.selected_key(), "net1");

  app.Select("nonexistent");
  EXPECT_EQ(app.selected(), nullptr);  // selection of a gone key is harmless

  app.Select("net1");
  ASSERT_NE(app.selected(), nullptr);
  app.StopAll();
}

TEST(Sessions, FanoutDeliversToAllSinks) {
  struct CountingSink : FrameSink {
    std::atomic<int> n{0};
    void OnFrame(const VideoFrame&) override { ++n; }
  };
  AppController app;
  ASSERT_TRUE(app.StartGst("net1", kTestPipeline).ok());
  AppController::Session* s = app.FindSession("net1");

  CountingSink a, b;
  EXPECT_TRUE(app.AttachFrameSink(*s, &a));
  EXPECT_TRUE(app.AttachFrameSink(*s, &b));  // concurrent sinks OK now

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((a.n.load() < 5 || b.n.load() < 5) &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_GE(a.n.load(), 5);
  EXPECT_GE(b.n.load(), 5);

  // Detached sinks stop receiving; the other keeps flowing.
  app.DetachFrameSink(*s, &a);
  const int a_after = a.n.load();
  const int b_mark = b.n.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(a.n.load(), a_after);
  EXPECT_GT(b.n.load(), b_mark);
  app.StopAll();
}

#else
TEST(Sessions, SkippedWithoutGStreamer) { GTEST_SKIP(); }
#endif
