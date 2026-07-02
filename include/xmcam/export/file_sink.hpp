/*
 * @file file_sink.hpp
 * @brief Record a session's frames to disk without hidden processing. Formats:
 *          kY4m     — raw I420, bit-exact frames as delivered (large!)
 *          kFfv1Mkv — mathematically lossless FFV1 in Matroska
 *          kH264Mkv — visually-good H.264 in Matroska (long captures)
 *        A writer thread with a bounded queue keeps the FrameSink contract
 *        (never block the capture thread); overruns drop frames and count.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_EXPORT_FILE_SINK_HPP
#define XMCAM_EXPORT_FILE_SINK_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include "xmcam/core/frame_sink.hpp"
#include "xmcam/core/result.hpp"

namespace xmotion {

class FileSink : public FrameSink {
 public:
  enum class Format { kY4m, kFfv1Mkv, kH264Mkv };
  static const char* Extension(Format f);  // "y4m" / "mkv"

  struct Stats {
    uint64_t frames_written = 0;
    uint64_t frames_dropped = 0;  // queue overruns (disk too slow)
  };

  FileSink() = default;
  ~FileSink() override;

  // Opens `path` for writing; fps is stamped into the container timing.
  Status Start(const std::string& path, Format format, double fps);
  // Finalizes the file (EOS through the muxer) — safe to call twice.
  void Stop();
  bool running() const { return running_.load(); }
  const std::string& path() const { return path_; }
  Stats stats() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return stats_;
  }

  // FrameSink: producer thread. Copies the frame into the bounded queue.
  void OnFrame(const VideoFrame& frame) override;

 private:
  struct Packed {  // tightly packed I420 payload
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;
  };
  void WriterLoop();
  bool EnsurePipeline(int width, int height);

  std::string path_;
  Format format_ = Format::kY4m;
  double fps_ = 30.0;

  GstElement* pipeline_ = nullptr;
  GstAppSrc* appsrc_ = nullptr;

  std::thread writer_;
  std::atomic<bool> running_{false};
  mutable std::mutex mtx_;
  std::condition_variable cv_;
  std::deque<Packed> queue_;
  Stats stats_;
};

}  // namespace xmotion

#endif  // XMCAM_EXPORT_FILE_SINK_HPP
