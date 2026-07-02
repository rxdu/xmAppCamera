/*
 * @file gst_source.hpp
 * @brief GStreamer-backed VideoSource for RTSP/UDP/file streams. Runs a
 *        pipeline ending in an appsink (latest-only), maps decoded frames to
 *        native VideoFrames. Also used to validate arbitrary pipeline strings.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_PIPELINE_GST_SOURCE_HPP
#define XMCAM_PIPELINE_GST_SOURCE_HPP

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include "xmcam/core/util/rate_counter.hpp"
#include "xmcam/pipeline/video_source.hpp"

namespace xmotion {

class GstSource : public VideoSource {
 public:
  // Connection lifecycle, surfaced to the UI. RTSP failures (auth, unreachable
  // host, codec mismatch) arrive asynchronously on the pipeline bus after
  // Start() — without this they look like an eternal "waiting for frames".
  enum class State { kIdle, kConnecting, kPlaying, kEos, kError };

  // Parse-check a pipeline string without running it (VIDIOC-free). Returns an
  // error with the GStreamer message on failure. The string should contain an
  // appsink named "sink" for use as a source, but validation does not require
  // it.
  static Status Validate(const std::string& pipeline);

  // Build the default pipeline for a URI (uridecodebin -> RGBA appsink).
  static std::string DefaultPipelineForUri(const std::string& uri);

  GstSource() = default;
  ~GstSource() override;

  // Uses desc.pipeline if non-empty, else builds one from desc.uri. The
  // pipeline must contain `appsink name=sink`.
  Status Open(const SourceDescriptor& desc) override;
  Status Start() override;
  void Stop() override;
  void Close() override;
  bool IsRunning() const override { return running_.load(); }

  SourceCaps GetCaps() const override { return SourceCaps{}; }
  SourceStats GetStats() const override;
  DataStream<VideoFrame>& Frames() override { return frames_; }

  State state() const { return state_.load(); }
  std::string last_error() const;
  // On bus error / EOS, tear the pipeline down to NULL and retry PLAYING with
  // bounded backoff instead of giving up (network cameras drop out).
  void SetAutoReconnect(bool enable) { auto_reconnect_.store(enable); }

 private:
  void PullLoop();
  // Drain bus messages; returns false when the stream is dead and no
  // reconnect is wanted.
  bool HandleBus();
  void Reconnect();

  std::string pipeline_desc_;
  GstElement* pipeline_ = nullptr;
  GstAppSink* appsink_ = nullptr;
  GstBus* bus_ = nullptr;
  DataStream<VideoFrame> frames_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<State> state_{State::kIdle};
  std::atomic<bool> auto_reconnect_{false};
  int reconnect_backoff_ms_ = 1000;
  mutable std::mutex err_mtx_;
  std::string last_error_;
  uint64_t seq_ = 0;

  mutable std::mutex stats_mtx_;
  SourceStats stats_;
  RateCounter rate_;
};

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_GST_SOURCE_HPP
