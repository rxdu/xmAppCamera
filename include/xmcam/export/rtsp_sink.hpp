/*
 * @file rtsp_sink.hpp
 * @brief Re-export a live source as an RTSP/H.264 stream. Frames teed from a
 *        VideoSource are fed into a per-client appsrc -> x264enc -> rtph264pay
 *        pipeline hosted by an in-process gst-rtsp-server.
 *
 * Only built when XMCAM_WITH_RTSP_SERVER (libgstrtspserver-1.0-dev) is present.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_EXPORT_RTSP_SINK_HPP
#define XMCAM_EXPORT_RTSP_SINK_HPP

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "xmcam/core/frame_sink.hpp"
#include "xmcam/core/result.hpp"

namespace xmotion {

class RtspSink : public FrameSink {
 public:
  RtspSink() = default;
  ~RtspSink() override;

  // Start serving rtsp://<host>:<port><mount> (mount like "/cam"). Idempotent
  // stop via Stop().
  Status Start(int port, const std::string& mount, int bitrate_kbps = 2000);
  void Stop();
  bool running() const { return running_.load(); }
  const std::string& url() const { return url_; }

  // FrameSink: called on the source producer thread.
  void OnFrame(const VideoFrame& frame) override;

  // Internal (called from the gst-rtsp media-configure callback).
  void OnMediaConfigure(GstElement* appsrc);

 private:
  void Loop();

  std::thread thread_;
  GMainLoop* loop_ = nullptr;
  GstRTSPServer* server_ = nullptr;
  std::atomic<bool> running_{false};
  std::string url_;
  std::string service_;
  std::string mount_;
  int bitrate_kbps_ = 2000;

  std::mutex src_mtx_;
  GstElement* appsrc_ = nullptr;  // current client's appsrc (ref held)
  int caps_w_ = 0;
  int caps_h_ = 0;
  PixelFormat caps_fmt_ = PixelFormat::kUnknown;
  uint64_t pushed_ = 0;
};

}  // namespace xmotion

#endif  // XMCAM_EXPORT_RTSP_SINK_HPP
