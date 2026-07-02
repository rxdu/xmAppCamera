/*
 * @file app_controller.hpp
 * @brief Owns the active video source + its control set, and mediates between
 *        the UI panels and the pipeline/control backends. All methods are
 *        called from the render thread (inside panel Draw()).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_APP_APP_CONTROLLER_HPP
#define XMCAM_APP_APP_CONTROLLER_HPP

#include <memory>
#include <string>
#include <vector>

#include "xmcam/control/control_set.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"
#include "xmcam/pipeline/v4l2_source.hpp"
#include "xmcam/pipeline/video_source.hpp"
#ifdef XMCAM_WITH_RTSP_SERVER
#include "xmcam/export/rtsp_sink.hpp"
#endif

namespace xmotion {

class AppController {
 public:
  AppController() = default;
  ~AppController();

  // --- device discovery ---
  const std::vector<DeviceInfo>& RefreshDevices();
  const std::vector<DeviceInfo>& devices() const { return devices_; }

  // --- source lifecycle (render thread) ---
  Status StartV4l2(const std::string& device, PixelFormat fmt, int width,
                   int height, double fps);
  // Convenience for demos/headless tests: pick the first device, prefer MJPEG
  // near 1280x720, and start it. Returns kNotFound if no device.
  Status StartFirstDevice();
  Status StartGst(const std::string& pipeline);
  void StopSource();

  bool HasSource() const { return source_ != nullptr; }
  bool IsRunning() const { return source_ && source_->IsRunning(); }
  const std::string& status() const { return status_; }
  const std::string& active_device() const { return active_device_; }

  // What is currently running, for stateful Start/Stop/Apply buttons: the UI
  // diffs its selection against this to decide between Stop and Apply.
  enum class ActiveKind { kNone, kV4l2, kGst };
  struct ActiveV4l2Config {
    std::string device;
    PixelFormat format = PixelFormat::kUnknown;
    int width = 0;
    int height = 0;
    double fps = 0.0;
  };
  ActiveKind active_kind() const {
    return IsRunning() ? active_kind_ : ActiveKind::kNone;
  }
  const ActiveV4l2Config& active_config() const { return active_config_; }
  const std::string& active_pipeline() const { return active_pipeline_; }

  // Render-side counters, written by the preview panel each frame.
  struct DisplayStats {
    double display_fps = 0.0;
    double upload_ms = 0.0;
    double latency_ms = 0.0;  // glass-to-glass (USB/monotonic only)
    uint64_t frames_shown = 0;
    int width = 0;
    int height = 0;
  };

  // --- per-frame (render thread) ---
  bool PullFrame(VideoFrame* out);
  SourceStats Stats() const;
  void SetDisplayStats(const DisplayStats& s) { display_stats_ = s; }
  DisplayStats display_stats() const { return display_stats_; }

  // --- controls (non-null only for a running V4L2 source) ---
  ControlSet* Controls() { return controls_.get(); }
  // Bumped whenever the control set is rebuilt (start, hot-plug recovery);
  // panels re-read cached values when it changes.
  int controls_epoch() const { return controls_epoch_; }

  // Attach/detach a frame tee on the active source (qualification tap, RTSP).
  // Only one sink at a time; returns false if none active or slot taken.
  bool AttachFrameSink(FrameSink* sink);
  void DetachFrameSink(FrameSink* sink);

  // --- RTSP re-export (Phase 5; no-op if built without gst-rtsp-server) ---
  Status StartRtspExport(int port, const std::string& mount);
  void StopRtspExport();
  bool RtspExporting() const;
  std::string RtspUrl() const;

 private:
  std::vector<DeviceInfo> devices_;
  std::unique_ptr<VideoSource> source_;
  std::shared_ptr<V4l2Device> ctrl_dev_;
  std::unique_ptr<ControlSet> controls_;
  void MaintainRecovery();  // rebuild controls after a hot-plug recovery

  std::string status_ = "idle";
  std::string active_device_;
  ActiveKind active_kind_ = ActiveKind::kNone;
  ActiveV4l2Config active_config_;
  std::string active_pipeline_;
  DisplayStats display_stats_;
  int controls_epoch_ = 0;
  uint32_t last_generation_ = 0;
  FrameSink* attached_sink_ = nullptr;  // render-thread only
#ifdef XMCAM_WITH_RTSP_SERVER
  std::unique_ptr<RtspSink> rtsp_;
#endif
};

}  // namespace xmotion

#endif  // XMCAM_APP_APP_CONTROLLER_HPP
