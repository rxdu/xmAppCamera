/*
 * @file test_config_roundtrip.cpp
 * @brief Round-trip and robustness tests for ConfigWriter / ConfigReader.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <gtest/gtest.h>

#include <string>

#include "xmcam/export/config_reader.hpp"
#include "xmcam/export/config_writer.hpp"

using namespace xmotion;

namespace {

CameraConfig MakeV4l2Config() {
  CameraConfig cfg;
  cfg.schema_version = 1;
  cfg.source.type = SourceDescriptor::Type::kV4l2;
  cfg.source.device = "/dev/v4l/by-id/usb-Foo_Camera_1234-video-index0";
  cfg.card = "Foo Camera";
  cfg.device_by_id = "/dev/v4l/by-id/usb-Foo_Camera_1234-video-index0";
  cfg.device_by_path = "/dev/v4l/by-path/pci-0000:00:14.0-usb-0:2:1.0-video-index0";
  cfg.source.format = PixelFormat::kYuyv;
  cfg.source.width = 640;
  cfg.source.height = 480;
  cfg.source.fps = 30.0;
  cfg.controls = {
      {"brightness", 0x00980900u, 128},
      {"contrast", 0x00980901u, 32},
      // A control at value 0 (must survive the round-trip, not be dropped).
      {"saturation", 0x00980902u, 0},
      {"exposure_absolute", 0x009a0902u, 250},
  };
  return cfg;
}

CameraConfig MakeGstreamerConfig() {
  CameraConfig cfg;
  cfg.schema_version = 1;
  cfg.source.type = SourceDescriptor::Type::kGstreamer;
  cfg.source.uri = "rtsp://192.168.1.50:554/stream1";
  cfg.pipeline =
      "rtspsrc location=rtsp://192.168.1.50:554/stream1 latency=50 ! "
      "rtph264depay ! decodebin3 ! appsink name=sink max-buffers=1 drop=true";
  cfg.source.width = 1920;
  cfg.source.height = 1080;
  cfg.source.fps = 25.0;
  return cfg;
}

void ExpectSourceEq(const CameraConfig& a, const CameraConfig& b) {
  EXPECT_EQ(a.source.type, b.source.type);
  EXPECT_EQ(a.source.device, b.source.device);
  EXPECT_EQ(a.device_by_id, b.device_by_id);
  EXPECT_EQ(a.device_by_path, b.device_by_path);
  EXPECT_EQ(a.source.uri, b.source.uri);
  EXPECT_EQ(a.source.format, b.source.format);
  EXPECT_EQ(a.source.width, b.source.width);
  EXPECT_EQ(a.source.height, b.source.height);
  EXPECT_DOUBLE_EQ(a.source.fps, b.source.fps);
}

void ExpectControlsEq(const CameraConfig& a, const CameraConfig& b) {
  ASSERT_EQ(a.controls.size(), b.controls.size());
  for (size_t i = 0; i < a.controls.size(); ++i) {
    EXPECT_EQ(a.controls[i].name, b.controls[i].name) << "control " << i;
    EXPECT_EQ(a.controls[i].id, b.controls[i].id) << "control " << i;
    EXPECT_EQ(a.controls[i].value, b.controls[i].value) << "control " << i;
  }
}

}  // namespace

TEST(ConfigRoundTrip, V4l2StringRoundTrip) {
  const CameraConfig cfg = MakeV4l2Config();
  const std::string yaml = ConfigWriter::ToYamlString(cfg);

  CameraConfig back;
  ASSERT_TRUE(ConfigReader::FromYamlString(yaml, &back).ok());

  EXPECT_EQ(back.schema_version, cfg.schema_version);
  EXPECT_EQ(back.card, cfg.card);
  ExpectSourceEq(cfg, back);
  ExpectControlsEq(cfg, back);
}

TEST(ConfigRoundTrip, V4l2FileRoundTrip) {
  const CameraConfig cfg = MakeV4l2Config();
  const std::string path = testing::TempDir() + "/xmcam_v4l2_config.yaml";

  ASSERT_TRUE(ConfigWriter::Write(cfg, path).ok());

  CameraConfig back;
  ASSERT_TRUE(ConfigReader::Read(path, &back).ok());

  ExpectSourceEq(cfg, back);
  ExpectControlsEq(cfg, back);
  // Explicitly confirm the value-0 control was preserved.
  EXPECT_EQ(back.controls[2].name, "saturation");
  EXPECT_EQ(back.controls[2].value, 0);
}

TEST(ConfigRoundTrip, GstreamerFileRoundTrip) {
  const CameraConfig cfg = MakeGstreamerConfig();
  const std::string path = testing::TempDir() + "/xmcam_gst_config.yaml";

  ASSERT_TRUE(ConfigWriter::Write(cfg, path).ok());

  CameraConfig back;
  ASSERT_TRUE(ConfigReader::Read(path, &back).ok());

  ExpectSourceEq(cfg, back);
  EXPECT_EQ(back.pipeline, cfg.pipeline);
  EXPECT_EQ(back.source.pipeline, cfg.pipeline);
}

TEST(ConfigRoundTrip, MalformedYamlReturnsError) {
  // Unterminated flow mapping -> parser error.
  const std::string bad = "source: { type: v4l2, device: /dev/video0";
  CameraConfig out;
  const Status st = ConfigReader::FromYamlString(bad, &out);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), ErrorCode::kInvalidArgument);
}

TEST(ConfigRoundTrip, UnknownKeysIgnored) {
  const std::string yaml =
      "schema_version: 1\n"
      "unknown_top_level: 42\n"
      "source:\n"
      "  type: v4l2\n"
      "  device: /dev/video0\n"
      "  future_field: hello\n"
      "format:\n"
      "  fourcc: YUYV\n"
      "  width: 1280\n"
      "  height: 720\n"
      "  fps: 30\n"
      "controls:\n"
      "  - { name: brightness, id: 0x00980900, value: 128, extra: nope }\n";

  CameraConfig out;
  ASSERT_TRUE(ConfigReader::FromYamlString(yaml, &out).ok());
  EXPECT_EQ(out.source.device, "/dev/video0");
  EXPECT_EQ(out.source.format, PixelFormat::kYuyv);
  EXPECT_EQ(out.source.width, 1280);
  ASSERT_EQ(out.controls.size(), 1u);
  EXPECT_EQ(out.controls[0].name, "brightness");
  EXPECT_EQ(out.controls[0].id, 0x00980900u);
  EXPECT_EQ(out.controls[0].value, 128);
}

TEST(ConfigRoundTrip, MissingSchemaVersionStillParses) {
  const std::string yaml =
      "source:\n"
      "  type: v4l2\n"
      "  device: /dev/video0\n";
  CameraConfig out;
  ASSERT_TRUE(ConfigReader::FromYamlString(yaml, &out).ok());
  EXPECT_EQ(out.source.device, "/dev/video0");
}
