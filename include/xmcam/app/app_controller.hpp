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
  Status StartGst(const std::string& pipeline);
  void StopSource();

  bool HasSource() const { return source_ != nullptr; }
  bool IsRunning() const { return source_ && source_->IsRunning(); }
  const std::string& status() const { return status_; }
  const std::string& active_device() const { return active_device_; }

  // --- per-frame (render thread) ---
  bool PullFrame(VideoFrame* out);
  SourceStats Stats() const;

  // --- controls (non-null only for a running V4L2 source) ---
  ControlSet* Controls() { return controls_.get(); }

 private:
  std::vector<DeviceInfo> devices_;
  std::unique_ptr<VideoSource> source_;
  std::shared_ptr<V4l2Device> ctrl_dev_;
  std::unique_ptr<ControlSet> controls_;
  std::string status_ = "idle";
  std::string active_device_;
};

}  // namespace xmotion

#endif  // XMCAM_APP_APP_CONTROLLER_HPP
