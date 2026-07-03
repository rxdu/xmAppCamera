// FileSink recording tests — synthetic I420 frames, no camera needed.
#include "xmcam/export/file_sink.hpp"

#include <gst/app/gstappsink.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using namespace xmotion;

namespace {

// Build a synthetic I420 VideoFrame backed by a heap buffer.
VideoFrame MakeFrame(int w, int h, uint8_t fill) {
  const int cw = (w + 1) / 2, ch = (h + 1) / 2;
  auto buf = std::make_shared<std::vector<uint8_t>>(
      static_cast<size_t>(w) * h + 2 * static_cast<size_t>(cw) * ch, fill);
  VideoFrame f;
  f.width = w;
  f.height = h;
  f.format = PixelFormat::kI420;
  f.data = buf->data();
  f.stride = w;
  f.plane1 = buf->data() + static_cast<size_t>(w) * h;
  f.stride1 = cw;
  f.plane2 = f.plane1 + static_cast<size_t>(cw) * ch;
  f.stride2 = cw;
  f.owner = buf;
  return f;
}

long FileSize(const std::string& p) {
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return -1;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fclose(f);
  return n;
}

void RecordN(FileSink& sink, int n) {
  for (int i = 0; i < n; ++i) {
    VideoFrame f = MakeFrame(64, 48, static_cast<uint8_t>(i));
    sink.OnFrame(f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

}  // namespace

TEST(FileSink, WritesRawY4m) {
  const std::string path = testing::TempDir() + "xmcam_test.y4m";
  std::remove(path.c_str());

  FileSink sink;
  ASSERT_TRUE(sink.Start(path, FileSink::Format::kY4m, 30.0).ok());
  RecordN(sink, 20);
  sink.Stop();

  const auto st = sink.stats();
  EXPECT_EQ(st.frames_written + st.frames_dropped, 20u);
  EXPECT_GE(st.frames_written, 15u);

  // y4m: header line + per frame ("FRAME\n" + tightly packed I420).
  const long frame_bytes = 6 + 64 * 48 * 3 / 2;
  const long size = FileSize(path);
  ASSERT_GT(size, 0);
  EXPECT_GE(size, frame_bytes * static_cast<long>(st.frames_written));
  std::remove(path.c_str());
}

TEST(FileSink, WritesLosslessMkv) {
  const std::string path = testing::TempDir() + "xmcam_test.mkv";
  std::remove(path.c_str());

  FileSink sink;
  ASSERT_TRUE(sink.Start(path, FileSink::Format::kFfv1Mkv, 30.0).ok());
  RecordN(sink, 20);
  sink.Stop();

  EXPECT_GE(sink.stats().frames_written, 15u);
  EXPECT_GT(FileSize(path), 1000);  // finalized container with content
  std::remove(path.c_str());
}

namespace {

// Encode n JPEG frames in-process (gst jpegenc) for passthrough tests.
std::vector<std::vector<uint8_t>> MakeJpegs(int n, int w, int h) {
  if (!gst_is_initialized()) gst_init(nullptr, nullptr);
  std::vector<std::vector<uint8_t>> out;
  gchar* desc = g_strdup_printf(
      "videotestsrc num-buffers=%d ! video/x-raw,width=%d,height=%d,"
      "framerate=30/1 ! jpegenc ! appsink name=sink sync=false",
      n, w, h);
  GError* err = nullptr;
  GstElement* p = gst_parse_launch(desc, &err);
  g_free(desc);
  if (!p || err) {
    if (err) g_error_free(err);
    return out;
  }
  GstElement* sink = gst_bin_get_by_name(GST_BIN(p), "sink");
  gst_element_set_state(p, GST_STATE_PLAYING);
  while (GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(sink),
                                                     2 * GST_SECOND)) {
    GstBuffer* b = gst_sample_get_buffer(s);
    GstMapInfo map;
    if (gst_buffer_map(b, &map, GST_MAP_READ)) {
      out.emplace_back(map.data, map.data + map.size);
      gst_buffer_unmap(b, &map);
    }
    gst_sample_unref(s);
  }
  gst_element_set_state(p, GST_STATE_NULL);
  gst_object_unref(sink);
  gst_object_unref(p);
  return out;
}

VideoFrame MakeCompressedFrame(const std::vector<uint8_t>& jpeg, int w,
                               int h) {
  VideoFrame f;
  f.width = w;
  f.height = h;
  f.format = PixelFormat::kMjpeg;
  f.data = jpeg.data();
  f.data_size = jpeg.size();
  return f;
}

}  // namespace

TEST(FileSink, PassthroughMuxesCameraBitstreamWithoutReencode) {
  const std::string path = testing::TempDir() + "xmcam_pass.mkv";
  std::remove(path.c_str());

  auto jpegs = MakeJpegs(20, 320, 240);
  ASSERT_GE(jpegs.size(), 20u);
  size_t payload = 0;
  for (const auto& j : jpegs) payload += j.size();

  FileSink sink;
  ASSERT_TRUE(
      sink.Start(path, FileSink::Format::kPassthroughMkv, 30.0).ok());
  for (const auto& j : jpegs) {
    sink.OnFrame(MakeCompressedFrame(j, 320, 240));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  // Decoded frames must be ignored by a passthrough recorder.
  VideoFrame decoded = MakeFrame(320, 240, 128);
  sink.OnFrame(decoded);
  sink.Stop();

  EXPECT_EQ(sink.stats().frames_written, 20u);

  // Zero-re-encode heuristic: the file contains the original bitstream, so
  // its size is the JPEG payload plus small mux overhead — nowhere near a
  // re-encode (different size class) or raw (i420 would be ~2.3MB).
  const long size = FileSize(path);
  ASSERT_GT(size, 0);
  EXPECT_GE(size, static_cast<long>(payload));
  EXPECT_LE(size, static_cast<long>(payload) + 100000);
  std::remove(path.c_str());
}
