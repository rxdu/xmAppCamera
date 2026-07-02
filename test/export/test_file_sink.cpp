// FileSink recording tests — synthetic I420 frames, no camera needed.
#include "xmcam/export/file_sink.hpp"

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
