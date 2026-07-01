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

 private:
  void PullLoop();

  std::string pipeline_desc_;
  GstElement* pipeline_ = nullptr;
  GstAppSink* appsink_ = nullptr;
  DataStream<VideoFrame> frames_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  uint64_t seq_ = 0;

  mutable std::mutex stats_mtx_;
  SourceStats stats_;
  RateCounter rate_;
};

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_GST_SOURCE_HPP
