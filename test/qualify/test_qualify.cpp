/*
 * @file test_qualify.cpp
 * @brief Unit tests for the qualification backend: timestamp stability,
 *        image fingerprint compare, enumeration check, FrameTap statistics,
 *        soak evaluation, serial uniqueness, USB link audit, open/close
 *        stress, report writers, and (device permitting) platform info
 *        collection and the control-effect/latency skip paths.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "xmcam/control/control_set.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"
#include "xmcam/qualify/frame_tap.hpp"
#include "xmcam/qualify/qual_checks.hpp"
#include "xmcam/qualify/qual_report.hpp"
#include "xmcam/qualify/qual_types.hpp"
#include "yaml-cpp/yaml.h"

namespace xmotion {
namespace {

constexpr int64_t kNs30FpsPeriod = 33333333;  // ~30 fps in ns

std::vector<int64_t> Perfect30FpsSeries(int n) {
  std::vector<int64_t> pts(n);
  for (int i = 0; i < n; ++i) pts[i] = static_cast<int64_t>(i) * kNs30FpsPeriod;
  return pts;
}

std::string FindMetric(const QualCheckResult& r, const std::string& key) {
  for (const auto& kv : r.metrics)
    if (kv.first == key) return kv.second;
  return {};
}

// ---------------------------------------------------------------------------
// CheckTimestampStability
// ---------------------------------------------------------------------------
TEST(TimestampStability, PerfectSeriesPasses) {
  QualCheckResult r = CheckTimestampStability(Perfect30FpsSeries(60), 30.0);
  EXPECT_EQ(r.status, QualStatus::kPass) << r.detail;
  const std::string stddev = FindMetric(r, "stddev_ms");
  ASSERT_FALSE(stddev.empty());
  EXPECT_LT(std::stod(stddev), 0.1);
}

TEST(TimestampStability, JitterySeriesFails) {
  // Alternate 10 ms / 60 ms deltas: mean ~35 ms, stddev ~25 ms >> 20% of the
  // 33.3 ms nominal period.
  std::vector<int64_t> pts;
  int64_t t = 0;
  for (int i = 0; i < 60; ++i) {
    pts.push_back(t);
    t += (i % 2 == 0) ? 10000000LL : 60000000LL;
  }
  QualCheckResult r = CheckTimestampStability(pts, 30.0);
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
}

TEST(TimestampStability, NonMonotonicSeriesFails) {
  std::vector<int64_t> pts = Perfect30FpsSeries(60);
  pts[40] = pts[39] - 1000000;  // step backwards past the warm-up window
  QualCheckResult r = CheckTimestampStability(pts, 30.0);
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
  EXPECT_EQ(FindMetric(r, "monotonic"), "false");
}

TEST(TimestampStability, TooFewSamplesSkipped) {
  QualCheckResult r = CheckTimestampStability(Perfect30FpsSeries(10), 30.0);
  EXPECT_EQ(r.status, QualStatus::kSkipped) << r.detail;
}

// ---------------------------------------------------------------------------
// CompareImageFingerprint
// ---------------------------------------------------------------------------
FrameTap::Snapshot MakeSnapshot(uint64_t frames, double y, double u, double v,
                                int w = 640, int h = 480) {
  FrameTap::Snapshot s;
  s.frames = frames;
  s.mean_y = y;
  s.mean_u = u;
  s.mean_v = v;
  s.width = w;
  s.height = h;
  return s;
}

TEST(ImageFingerprint, IdenticalSnapshotsPass) {
  FrameTap::Snapshot snap = MakeSnapshot(20, 100.0, 128.0, 128.0);
  QualCheckResult r = CompareImageFingerprint(snap, snap, 2.0);
  EXPECT_EQ(r.status, QualStatus::kPass) << r.detail;
}

TEST(ImageFingerprint, LargeLumaDriftFails) {
  FrameTap::Snapshot before = MakeSnapshot(20, 100.0, 128.0, 128.0);
  // 20% of 255 = 51 luma drift, well beyond a 5% tolerance.
  FrameTap::Snapshot after = MakeSnapshot(20, 151.0, 128.0, 128.0);
  QualCheckResult r = CompareImageFingerprint(before, after, 5.0);
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
}

TEST(ImageFingerprint, EmptySnapshotsSkipped) {
  QualCheckResult r =
      CompareImageFingerprint(FrameTap::Snapshot{}, FrameTap::Snapshot{}, 2.0);
  EXPECT_EQ(r.status, QualStatus::kSkipped) << r.detail;
}

// ---------------------------------------------------------------------------
// CheckEnumeration
// ---------------------------------------------------------------------------
TEST(Enumeration, NonEmptyCapsAndControlsPass) {
  SourceCaps caps;
  FormatDesc& fmt = caps.AddFormat(PixelFormat::kYuyv,
                                   FourCC('Y', 'U', 'Y', 'V'), "YUYV 4:2:2",
                                   false);
  FrameSize size;
  size.width = 640;
  size.height = 480;
  size.fps = {30.0, 15.0};
  fmt.sizes.push_back(size);

  std::vector<ControlInfo> controls(1);
  controls[0].id = 0x00980900;  // V4L2_CID_BRIGHTNESS
  controls[0].name = "Brightness";

  QualCheckResult r = CheckEnumeration(caps, controls);
  EXPECT_EQ(r.status, QualStatus::kPass) << r.detail;
  EXPECT_EQ(FindMetric(r, "n_formats"), "1");
  EXPECT_EQ(FindMetric(r, "n_controls"), "1");
  EXPECT_EQ(FindMetric(r, "fmt_YUYV"), "1 sizes");
}

TEST(Enumeration, EmptyFails) {
  QualCheckResult r = CheckEnumeration(SourceCaps{}, {});
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
}

// ---------------------------------------------------------------------------
// FrameTap
// ---------------------------------------------------------------------------
TEST(FrameTapTest, AccumulatesI420Statistics) {
  constexpr int kWidth = 64;
  constexpr int kHeight = 48;
  const size_t y_size = static_cast<size_t>(kWidth) * kHeight;
  const size_t c_size = y_size / 4;

  auto buffer = std::make_shared<std::vector<uint8_t>>(y_size + 2 * c_size);
  uint8_t* y_plane = buffer->data();
  uint8_t* u_plane = y_plane + y_size;
  uint8_t* v_plane = u_plane + c_size;
  std::fill(y_plane, y_plane + y_size, 100);
  std::fill(u_plane, u_plane + c_size, 50);
  std::fill(v_plane, v_plane + c_size, 200);

  VideoFrame f;
  f.width = kWidth;
  f.height = kHeight;
  f.format = PixelFormat::kI420;
  f.data = y_plane;
  f.stride = kWidth;
  f.plane1 = u_plane;
  f.stride1 = kWidth / 2;
  f.plane2 = v_plane;
  f.stride2 = kWidth / 2;
  f.owner = buffer;

  FrameTap tap;
  for (int i = 0; i < 3; ++i) {
    f.pts_ns = static_cast<int64_t>(i) * kNs30FpsPeriod;
    f.seq = static_cast<uint64_t>(i);
    tap.OnFrame(f);
  }

  FrameTap::Snapshot snap = tap.Take();
  EXPECT_EQ(snap.frames, 3u);
  EXPECT_EQ(snap.width, kWidth);
  EXPECT_EQ(snap.height, kHeight);
  EXPECT_NEAR(snap.mean_y, 100.0, 0.01);
  EXPECT_NEAR(snap.mean_u, 50.0, 0.01);
  EXPECT_NEAR(snap.mean_v, 200.0, 0.01);
  ASSERT_EQ(snap.pts_ns.size(), 3u);
  EXPECT_EQ(snap.pts_ns[1], kNs30FpsPeriod);

  tap.Reset();
  EXPECT_EQ(tap.Take().frames, 0u);
}

// ---------------------------------------------------------------------------
// Report writers
// ---------------------------------------------------------------------------
QualReport MakeTestReport() {
  QualReport report;
  report.device = "/dev/video0";
  report.card = "Test Camera";
  report.by_id = "/dev/v4l/by-id/usb-Test_Camera-video-index0";
  report.started_at = "2026-07-02T10:00:00";
  report.finished_at = "2026-07-02T10:05:00";
  report.platform.kernel = "5.15.0-test";
  report.platform.driver = "uvcvideo 5.15.0";
  report.platform.usb_vid_pid = "1234:abcd";

  QualCheckResult pass;
  pass.id = "exposure_lock";
  pass.title = "Exposure lock";
  pass.status = QualStatus::kPass;
  pass.detail = "value held for 2000 ms";
  pass.metrics.emplace_back("samples", "20");
  report.results.push_back(pass);

  QualCheckResult fail;
  fail.id = "timestamp_stability";
  fail.title = "Timestamp stability";
  fail.status = QualStatus::kFail;
  fail.detail = "jitter too high | see metrics";
  report.results.push_back(fail);

  report.manual_fields.emplace_back("operator", "rdu");
  report.manual_fields.emplace_back("unit_serial", "SN-0042");
  return report;
}

TEST(ReportWriters, YamlRoundTrips) {
  const std::string path = testing::TempDir() + "test_qual_report.yaml";
  const QualReport report = MakeTestReport();
  Status s = WriteReportYaml(report, path);
  ASSERT_TRUE(s.ok()) << s.message();

  YAML::Node root = YAML::LoadFile(path);
  EXPECT_EQ(root["device"].as<std::string>(), report.device);
  ASSERT_TRUE(root["results"].IsSequence());
  EXPECT_EQ(root["results"].size(), report.results.size());
  EXPECT_EQ(root["results"][0]["status"].as<std::string>(), "PASS");
  EXPECT_EQ(root["platform"]["kernel"].as<std::string>(),
            report.platform.kernel);
  EXPECT_EQ(root["manual_fields"]["unit_serial"].as<std::string>(), "SN-0042");
}

TEST(ReportWriters, MarkdownFileWritten) {
  const std::string path = testing::TempDir() + "test_qual_report.md";
  Status s = WriteReportMarkdown(MakeTestReport(), path);
  ASSERT_TRUE(s.ok()) << s.message();

  std::ifstream ifs(path);
  ASSERT_TRUE(ifs.good());
  std::string content((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("# Camera Qualification Report"), std::string::npos);
  EXPECT_NE(content.find("| Exposure lock | PASS |"), std::string::npos);
  EXPECT_NE(content.find("SN-0042"), std::string::npos);
}

TEST(ReportWriters, UnwritablePathFails) {
  Status s = WriteReportYaml(MakeTestReport(), "/nonexistent/dir/report.yaml");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kIo);
}

// ---------------------------------------------------------------------------
// CollectPlatformInfo (needs a real device)
// ---------------------------------------------------------------------------
TEST(PlatformInfo, CollectFromVideo0) {
  if (::access("/dev/video0", F_OK) != 0)
    GTEST_SKIP() << "/dev/video0 not present";
  PlatformInfo info;
  Status s = CollectPlatformInfo("/dev/video0", &info);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_FALSE(info.kernel.empty());
  EXPECT_FALSE(info.driver.empty());
}

// ---------------------------------------------------------------------------
// FrameTap v2 aggregates
// ---------------------------------------------------------------------------
// Owning I420 test frame with constant plane values.
struct TestFrame {
  std::shared_ptr<std::vector<uint8_t>> buffer;
  VideoFrame frame;
};

TestFrame MakeI420Frame(int width, int height, uint8_t y, uint8_t u,
                        uint8_t v) {
  const size_t y_size = static_cast<size_t>(width) * height;
  const size_t c_size = y_size / 4;
  TestFrame t;
  t.buffer = std::make_shared<std::vector<uint8_t>>(y_size + 2 * c_size);
  uint8_t* base = t.buffer->data();
  std::fill(base, base + y_size, y);
  std::fill(base + y_size, base + y_size + c_size, u);
  std::fill(base + y_size + c_size, base + y_size + 2 * c_size, v);
  t.frame.width = width;
  t.frame.height = height;
  t.frame.format = PixelFormat::kI420;
  t.frame.data = base;
  t.frame.stride = width;
  t.frame.plane1 = base + y_size;
  t.frame.stride1 = width / 2;
  t.frame.plane2 = base + y_size + c_size;
  t.frame.stride2 = width / 2;
  t.frame.owner = t.buffer;
  return t;
}

TEST(FrameTapV2, HwSeqGapsCounted) {
  FrameTap tap;
  TestFrame t = MakeI420Frame(64, 48, 100, 128, 128);
  const uint32_t seqs[] = {1, 2, 5};  // 5 skips 3 and 4 -> 2 gaps
  for (int i = 0; i < 3; ++i) {
    t.frame.pts_ns = static_cast<int64_t>(i) * kNs30FpsPeriod;
    t.frame.hw_seq = seqs[i];
    tap.OnFrame(t.frame);
  }
  EXPECT_EQ(tap.Take().hw_seq_gaps, 2u);
}

TEST(FrameTapV2, HwSeqZeroIgnored) {
  FrameTap tap;
  TestFrame t = MakeI420Frame(64, 48, 100, 128, 128);
  const uint32_t seqs[] = {0, 0, 0};  // non-V4L2 source
  for (int i = 0; i < 3; ++i) {
    t.frame.pts_ns = static_cast<int64_t>(i) * kNs30FpsPeriod;
    t.frame.hw_seq = seqs[i];
    tap.OnFrame(t.frame);
  }
  EXPECT_EQ(tap.Take().hw_seq_gaps, 0u);
}

TEST(FrameTapV2, StuckAndBlackWhiteFramesCounted) {
  FrameTap tap;
  TestFrame same = MakeI420Frame(64, 48, 100, 128, 128);
  same.frame.pts_ns = 0;
  tap.OnFrame(same.frame);
  same.frame.pts_ns = kNs30FpsPeriod;  // identical payload again
  tap.OnFrame(same.frame);
  TestFrame black = MakeI420Frame(64, 48, 0, 128, 128);
  black.frame.pts_ns = 2 * kNs30FpsPeriod;
  tap.OnFrame(black.frame);
  TestFrame white = MakeI420Frame(64, 48, 255, 128, 128);
  white.frame.pts_ns = 3 * kNs30FpsPeriod;
  tap.OnFrame(white.frame);

  FrameTap::Snapshot s = tap.Take();
  EXPECT_GE(s.stuck_frames, 1u);
  EXPECT_EQ(s.black_frames, 1u);
  EXPECT_EQ(s.white_frames, 1u);
}

TEST(FrameTapV2, RecentMeansOldestFirst) {
  FrameTap tap;
  const uint8_t levels[] = {50, 100, 150};
  for (int i = 0; i < 3; ++i) {
    TestFrame t = MakeI420Frame(64, 48, levels[i], 128, 128);
    t.frame.pts_ns = static_cast<int64_t>(i) * kNs30FpsPeriod;
    tap.OnFrame(t.frame);
  }
  std::vector<FrameTap::MeanSample> means = tap.RecentMeans();
  ASSERT_EQ(means.size(), 3u);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(means[i].pts_ns, static_cast<int64_t>(i) * kNs30FpsPeriod);
    EXPECT_NEAR(means[i].mean_y, static_cast<double>(levels[i]), 0.01);
  }
}

TEST(FrameTapV2, Per10sBucketsAndIntervalStats) {
  FrameTap tap;
  TestFrame t = MakeI420Frame(64, 48, 100, 128, 128);
  // 25 frames at 1 fps spanning 25 s -> buckets of 10 / 10 / 5.
  for (int i = 0; i < 25; ++i) {
    t.frame.pts_ns = static_cast<int64_t>(i) * 1000000000LL;
    tap.OnFrame(t.frame);
  }
  FrameTap::Snapshot s = tap.Take();
  ASSERT_EQ(s.per10s_frames.size(), 3u);
  EXPECT_EQ(s.per10s_frames[0], 10u);
  EXPECT_EQ(s.per10s_frames[1], 10u);
  EXPECT_EQ(s.per10s_frames[2], 5u);
  EXPECT_NEAR(s.interval_mean_ms, 1000.0, 1e-6);
  EXPECT_NEAR(s.interval_stddev_ms, 0.0, 1e-6);
  EXPECT_NEAR(s.interval_min_ms, 1000.0, 1e-6);
  EXPECT_NEAR(s.interval_max_ms, 1000.0, 1e-6);

  tap.Reset();
  s = tap.Take();
  EXPECT_TRUE(s.per10s_frames.empty());
  EXPECT_EQ(s.hw_seq_gaps, 0u);
  EXPECT_TRUE(tap.RecentMeans().empty());
}

// ---------------------------------------------------------------------------
// CheckSoak
// ---------------------------------------------------------------------------
// A clean 35 s / 20 fps soak window: three full 10 s buckets and one partial.
FrameTap::Snapshot MakeSoakSnapshot() {
  FrameTap::Snapshot s;
  s.frames = 650;
  s.per10s_frames = {200, 200, 200, 50};
  s.interval_mean_ms = 50.0;
  s.interval_stddev_ms = 1.0;
  s.interval_min_ms = 48.0;
  s.interval_max_ms = 52.0;
  return s;
}

TEST(Soak, CleanWindowPasses) {
  QualCheckResult r = CheckSoak(MakeSoakSnapshot(), 20.0, 100000, 100100, 35);
  EXPECT_EQ(r.status, QualStatus::kPass) << r.detail;
  EXPECT_EQ(FindMetric(r, "frames"), "650");
  EXPECT_EQ(FindMetric(r, "fps_min_bucket"), "20.000");
  EXPECT_EQ(FindMetric(r, "fps_max_bucket"), "20.000");
  EXPECT_EQ(FindMetric(r, "rss_growth_kb"), "100");
}

TEST(Soak, HwSeqGapsFail) {
  FrameTap::Snapshot s = MakeSoakSnapshot();
  s.hw_seq_gaps = 10;  // > 0.1% of 650 frames
  QualCheckResult r = CheckSoak(s, 20.0, 100000, 100100, 35);
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
  EXPECT_NE(r.detail.find("gap"), std::string::npos);
}

TEST(Soak, StuckFramesFail) {
  FrameTap::Snapshot s = MakeSoakSnapshot();
  s.stuck_frames = 1;
  QualCheckResult r = CheckSoak(s, 20.0, 100000, 100100, 35);
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
  EXPECT_NE(r.detail.find("stuck"), std::string::npos);
}

TEST(Soak, SaggingBucketFails) {
  FrameTap::Snapshot s = MakeSoakSnapshot();
  s.per10s_frames = {200, 200, 100, 50};  // one full bucket at 50% of mean
  s.frames = 550;
  QualCheckResult r = CheckSoak(s, 20.0, 100000, 100100, 35);
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
  EXPECT_NE(r.detail.find("bucket"), std::string::npos);
}

TEST(Soak, RssGrowthFails) {
  QualCheckResult r =
      CheckSoak(MakeSoakSnapshot(), 20.0, 100000, 120480, 35);  // +20 MB
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
  EXPECT_NE(r.detail.find("RSS"), std::string::npos);
}

TEST(Soak, TooFewFramesSkipped) {
  FrameTap::Snapshot s;
  s.frames = 50;
  QualCheckResult r = CheckSoak(s, 20.0, 100000, 100100, 5);
  EXPECT_EQ(r.status, QualStatus::kSkipped) << r.detail;
}

TEST(Soak, ReadRssKbReturnsPositive) {
  EXPECT_GT(ReadRssKb(), 0);
}

// ---------------------------------------------------------------------------
// CheckSerialUniqueness
// ---------------------------------------------------------------------------
PlatformInfo MakePlatformWithSerial(const std::string& serial) {
  PlatformInfo pi;
  pi.usb_serial = serial;
  return pi;
}

TEST(SerialUniqueness, VersionLikeSerialFails) {
  QualCheckResult r = CheckSerialUniqueness(MakePlatformWithSerial("01.00.00"),
                                            {"01.00.00"});
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
  EXPECT_NE(r.detail.find("version"), std::string::npos);
}

TEST(SerialUniqueness, EmptySerialFails) {
  QualCheckResult r = CheckSerialUniqueness(MakePlatformWithSerial(""), {});
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
}

TEST(SerialUniqueness, UniqueSerialPasses) {
  QualCheckResult r = CheckSerialUniqueness(
      MakePlatformWithSerial("SN-A1B2C3"), {"SN-A1B2C3", "SN-D4E5F6"});
  EXPECT_EQ(r.status, QualStatus::kPass) << r.detail;
}

TEST(SerialUniqueness, DuplicateSerialFails) {
  QualCheckResult r = CheckSerialUniqueness(
      MakePlatformWithSerial("SN-A1B2C3"), {"SN-A1B2C3", "SN-A1B2C3"});
  EXPECT_EQ(r.status, QualStatus::kFail) << r.detail;
  EXPECT_EQ(FindMetric(r, "occurrences"), "2");
}

// ---------------------------------------------------------------------------
// CheckUsbLink
// ---------------------------------------------------------------------------
TEST(UsbLink, EmptySysfsPathSkipped) {
  QualCheckResult r = CheckUsbLink(PlatformInfo{});
  EXPECT_EQ(r.status, QualStatus::kSkipped) << r.detail;
}

// ---------------------------------------------------------------------------
// CheckOpenCloseStress (needs a real device)
// ---------------------------------------------------------------------------
TEST(OpenCloseStress, FiveCyclesOnVideo0) {
  if (::access("/dev/video0", F_OK) != 0)
    GTEST_SKIP() << "/dev/video0 not present";
  QualCheckResult r = CheckOpenCloseStress("/dev/video0", 5);
  EXPECT_EQ(r.status, QualStatus::kPass) << r.detail;
  EXPECT_EQ(FindMetric(r, "cycles"), "5");
  EXPECT_FALSE(FindMetric(r, "avg_ms").empty());
}

// ---------------------------------------------------------------------------
// CheckControlEffect / CheckWriteEffectLatency skip paths (the live-camera
// behavior is exercised by the GUI, not gtest)
// ---------------------------------------------------------------------------
TEST(ControlEffect, MissingControlsSkipped) {
  if (::access("/dev/video0", F_OK) != 0)
    GTEST_SKIP() << "/dev/video0 not present";
  auto dev = std::make_shared<V4l2Device>();
  Status s = dev->Open("/dev/video0");
  if (!s.ok()) GTEST_SKIP() << "/dev/video0 not openable: " << s.message();
  ControlSet cs(dev);
  ASSERT_TRUE(cs.Refresh().ok());
  FrameTap tap;
  constexpr uint32_t kBogusCtrl = 0x00feed00;  // not a real V4L2 CID

  QualCheckResult effect = CheckControlEffect(
      cs, tap, "effect_test", "Effect test", 0, 0, kBogusCtrl, 10, 10, 5.0);
  EXPECT_EQ(effect.status, QualStatus::kSkipped) << effect.detail;

  QualCheckResult latency = CheckWriteEffectLatency(cs, tap, 0, 0, kBogusCtrl);
  EXPECT_EQ(latency.status, QualStatus::kSkipped) << latency.detail;
}

}  // namespace
}  // namespace xmotion
