/*
 * @file app_controller.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/app/app_controller.hpp"

#include <cstdlib>

#include "xmsigma/logging/xlogger.hpp"

#ifdef XMCAM_WITH_GSTREAMER
#include "xmcam/pipeline/gst_source.hpp"
#endif

namespace xmotion {

AppController::~AppController() { StopSource(); }

const std::vector<DeviceInfo>& AppController::RefreshDevices() {
  devices_ = V4l2Source::Enumerate();
  XLOG_INFO("AppController: {} device(s)", devices_.size());
  return devices_;
}

Status AppController::StartV4l2(const std::string& device, PixelFormat fmt,
                               int width, int height, double fps) {
  StopSource();

  auto src = std::make_unique<V4l2Source>();
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kV4l2;
  desc.device = device;
  desc.format = fmt;
  desc.width = width;
  desc.height = height;
  desc.fps = fps;
  if (auto st = src->Open(desc); !st.ok()) {
    status_ = "open failed: " + st.message();
    return st;
  }
  if (auto st = src->Start(); !st.ok()) {
    status_ = "start failed: " + st.message();
    return st;
  }
  source_ = std::move(src);
  active_device_ = device;

  // Controls on a second fd to the same device (kernel applies them globally).
  ctrl_dev_ = std::make_shared<V4l2Device>();
  if (ctrl_dev_->Open(device).ok()) {
    controls_ = std::make_unique<ControlSet>(ctrl_dev_);
    if (auto st = controls_->Refresh(); !st.ok()) {
      XLOG_WARN("control refresh: {}", st.message());
      controls_.reset();
      ctrl_dev_.reset();
    }
  }
  status_ = "streaming " + device;
  XLOG_INFO("AppController: started V4L2 {}", device);
  return Ok();
}

Status AppController::StartFirstDevice() {
  RefreshDevices();
  if (devices_.empty()) return Err(ErrorCode::kNotFound, "no V4L2 device");
  const DeviceInfo& dev = devices_.front();

  // Prefer MJPEG (the high-fps path), else the first format.
  const FormatDesc* fmt = nullptr;
  for (const auto& f : dev.caps.formats)
    if (f.format == PixelFormat::kMjpeg) fmt = &f;
  if (!fmt && !dev.caps.formats.empty()) fmt = &dev.caps.formats.front();
  if (!fmt || fmt->sizes.empty())
    return Err(ErrorCode::kUnsupported, "device has no usable format");

  // Choose the size nearest 1280x720.
  const FrameSize* best = &fmt->sizes.front();
  long best_d = 1L << 60;
  for (const auto& s : fmt->sizes) {
    const long d = std::abs(s.width - 1280L) + std::abs(s.height - 720L);
    if (d < best_d) { best_d = d; best = &s; }
  }
  const double fps = best->fps.empty() ? 30.0 : best->fps.front();
  return StartV4l2(dev.device, fmt->format, best->width, best->height, fps);
}

Status AppController::StartGst(const std::string& pipeline) {
  StopSource();
#ifdef XMCAM_WITH_GSTREAMER
  auto src = std::make_unique<GstSource>();
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kGstreamer;
  desc.pipeline = pipeline;
  if (auto st = src->Open(desc); !st.ok()) {
    status_ = "open failed: " + st.message();
    return st;
  }
  if (auto st = src->Start(); !st.ok()) {
    status_ = "start failed: " + st.message();
    return st;
  }
  source_ = std::move(src);
  active_device_ = "gstreamer";
  status_ = "streaming pipeline";
  return Ok();
#else
  status_ = "GStreamer support not built";
  return Err(ErrorCode::kUnsupported, status_);
#endif
}

Status AppController::StartRtspExport(int port, const std::string& mount) {
#ifdef XMCAM_WITH_RTSP_SERVER
  if (!source_) return Err(ErrorCode::kInvalidArgument, "no active source");
  StopRtspExport();
  rtsp_ = std::make_unique<RtspSink>();
  if (auto st = rtsp_->Start(port, mount); !st.ok()) {
    rtsp_.reset();
    return st;
  }
  source_->SetFrameSink(rtsp_.get());
  return Ok();
#else
  (void)port;
  (void)mount;
  return Err(ErrorCode::kUnsupported, "RTSP export not built");
#endif
}

void AppController::StopRtspExport() {
#ifdef XMCAM_WITH_RTSP_SERVER
  if (source_) source_->SetFrameSink(nullptr);
  if (rtsp_) {
    rtsp_->Stop();
    rtsp_.reset();
  }
#endif
}

bool AppController::RtspExporting() const {
#ifdef XMCAM_WITH_RTSP_SERVER
  return rtsp_ && rtsp_->running();
#else
  return false;
#endif
}

std::string AppController::RtspUrl() const {
#ifdef XMCAM_WITH_RTSP_SERVER
  return rtsp_ ? rtsp_->url() : std::string();
#else
  return std::string();
#endif
}

void AppController::StopSource() {
  StopRtspExport();  // detach + stop the sink before the source goes away
  if (source_) {
    source_->Stop();
    source_->Close();
    source_.reset();
  }
  controls_.reset();
  ctrl_dev_.reset();
  active_device_.clear();
  status_ = "idle";
}

bool AppController::PullFrame(VideoFrame* out) {
  if (!source_) return false;
  return source_->Frames().TryPull(*out);
}

SourceStats AppController::Stats() const {
  return source_ ? source_->GetStats() : SourceStats{};
}

}  // namespace xmotion
