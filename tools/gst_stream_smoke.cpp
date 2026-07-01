/*
 * @file gst_stream_smoke.cpp
 * @brief Headless check of GstSource: run a pipeline (or URI) and pull N frames.
 *
 * Usage:
 *   xmcam_gst_smoke "<full gst pipeline with appsink name=sink>" [num_frames]
 *   xmcam_gst_smoke --uri rtsp://host/stream [num_frames]
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "xmcam/pipeline/gst_source.hpp"

using namespace xmotion;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s \"<pipeline>\" [n] | --uri <uri> [n]\n", argv[0]);
    return 1;
  }
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kGstreamer;
  int argi = 1;
  if (std::strcmp(argv[1], "--uri") == 0) {
    if (argc < 3) return 1;
    desc.uri = argv[2];
    argi = 3;
  } else {
    desc.pipeline = argv[1];
    argi = 2;
  }
  int n = (argc > argi) ? std::atoi(argv[argi]) : 60;

  // Validate first (parse-only).
  const std::string& p =
      desc.pipeline.empty() ? GstSource::DefaultPipelineForUri(desc.uri)
                            : desc.pipeline;
  if (auto st = GstSource::Validate(p); !st.ok()) {
    std::printf("validate FAILED: %s\n", st.message().c_str());
    return 1;
  }
  std::printf("validate OK: %s\n", p.c_str());

  GstSource src;
  if (auto st = src.Open(desc); !st.ok()) {
    std::printf("open FAILED: %s\n", st.message().c_str());
    return 1;
  }
  if (auto st = src.Start(); !st.ok()) {
    std::printf("start FAILED: %s\n", st.message().c_str());
    return 1;
  }

  int got = 0;
  auto t0 = std::chrono::steady_clock::now();
  const auto deadline = t0 + std::chrono::seconds(15);
  while (got < n && std::chrono::steady_clock::now() < deadline) {
    VideoFrame f;
    if (!src.Frames().TryPull(f)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    if (got < 3 || got == n - 1)
      std::printf("  frame seq=%llu %dx%d %s stride=%d\n",
                  (unsigned long long)f.seq, f.width, f.height,
                  ToString(f.format), f.stride);
    ++got;
  }
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              t0)
                    .count();
  src.Stop();
  src.Close();
  std::printf("== got %d frames in %.2fs (%.1f fps) ==\n", got, secs,
              got / secs);
  return got > 0 ? 0 : 2;
}
