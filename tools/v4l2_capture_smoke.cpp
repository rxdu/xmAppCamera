/*
 * @file v4l2_capture_smoke.cpp
 * @brief Headless end-to-end check of the V4L2 capture path: enumerate, open,
 *        capture N frames off the real camera, report fps + a cheap checksum to
 *        prove pixels actually flow. No GUI required.
 *
 * Usage: xmcam_v4l2_smoke [device] [num_frames]
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "xmcam/pipeline/v4l2_source.hpp"

using namespace xmotion;

int main(int argc, char** argv) {
  auto devices = V4l2Source::Enumerate();
  std::printf("== enumerated %zu capture device(s) ==\n", devices.size());
  for (const auto& d : devices) {
    std::printf("  %s [%s] by-id=%s formats=%zu\n", d.device.c_str(),
                d.card.c_str(), d.by_id.empty() ? "-" : d.by_id.c_str(),
                d.caps.formats.size());
    for (const auto& f : d.caps.formats) {
      std::printf("    %-6s%s sizes=%zu\n", ToString(f.format),
                  f.compressed ? "(compressed)" : "", f.sizes.size());
    }
  }
  if (devices.empty()) {
    std::printf("no devices — nothing to capture\n");
    return 0;
  }

  // args: [device] [yuyv|mjpeg] [WxH] [fps] [nframes]
  std::string device = argc > 1 ? argv[1] : devices.front().device;
  std::string fmt = argc > 2 ? argv[2] : "yuyv";
  int w = 640, h = 480;
  if (argc > 3) std::sscanf(argv[3], "%dx%d", &w, &h);
  double fps = argc > 4 ? std::atof(argv[4]) : 30;
  int n = argc > 5 ? std::atoi(argv[5]) : 60;

  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kV4l2;
  desc.device = device;
  desc.format = (fmt == "mjpeg") ? PixelFormat::kMjpeg : PixelFormat::kYuyv;
  desc.width = w;
  desc.height = h;
  desc.fps = fps;

  V4l2Source src;
  if (auto st = src.Open(desc); !st.ok()) {
    std::printf("Open failed: %s\n", st.message().c_str());
    return 1;
  }
  std::printf("== capturing %d frames from %s (%s %dx%d) ==\n", n,
              device.c_str(), ToString(src.negotiated_format()),
              src.negotiated_width(), src.negotiated_height());
  if (auto st = src.Start(); !st.ok()) {
    std::printf("Start failed: %s\n", st.message().c_str());
    return 1;
  }

  int got = 0;
  uint64_t last_seq = 0;
  auto t0 = std::chrono::steady_clock::now();
  const auto deadline = t0 + std::chrono::seconds(10);
  while (got < n && std::chrono::steady_clock::now() < deadline) {
    VideoFrame f;
    if (!src.Frames().TryPull(f)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    // Cheap checksum over the first row to prove real pixels moved.
    uint32_t sum = 0;
    if (f.valid())
      for (int i = 0; i < f.stride; ++i) sum += f.data[i];
    if (got < 3 || got == n - 1)
      std::printf("  frame seq=%llu %dx%d stride=%d pts=%lldns row0sum=%u\n",
                  (unsigned long long)f.seq, f.width, f.height, f.stride,
                  (long long)f.pts_ns, sum);
    last_seq = f.seq;
    ++got;
  }
  auto t1 = std::chrono::steady_clock::now();
  double secs = std::chrono::duration<double>(t1 - t0).count();

  auto stats = src.GetStats();
  src.Stop();
  src.Close();

  std::printf("== got %d unique frames in %.2fs (%.1f fps observed, "
              "%.1f fps counter), last_seq=%llu ==\n",
              got, secs, got / secs, stats.capture_fps,
              (unsigned long long)last_seq);
  return got > 0 ? 0 : 2;
}
