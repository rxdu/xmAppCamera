/*
 * @file test_qualify.cpp
 * @brief Unit tests for the qualification backend: timestamp stability,
 *        image fingerprint compare, enumeration check, FrameTap statistics,
 *        report writers, and (device permitting) platform info collection.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
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

}  // namespace
}  // namespace xmotion
