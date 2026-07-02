/*
 * @file app_controller.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/app/app_controller.hpp"

#include <algorithm>
#include <cstdlib>

#include "xmsigma/logging/xlogger.hpp"

#ifdef XMCAM_WITH_GSTREAMER
#include "xmcam/pipeline/gst_source.hpp"
#endif

namespace xmotion {
AppController::~AppController() { StopAll(); }

const std::vector<DeviceInfo>& AppController::RefreshDevices() {
  devices_ = V4l2Source::Enumerate();
  XLOG_INFO("AppController: {} device(s)", devices_.size());
  return devices_;
}

AppController::Session* AppController::FindSession(const std::string& key) {
  for (auto& s : sessions_)
    if (s->key == key) return s.get();
  return nullptr;
}

size_t AppController::RunningCount() const {
  return static_cast<size_t>(
      std::count_if(sessions_.begin(), sessions_.end(),
                    [](const auto& s) { return s->IsRunning(); }));
}

AppController::Session* AppController::EnsureSession(SourceKind kind,
                                                     const std::string& key) {
  if (Session* existing = FindSession(key)) {
    TeardownSession(*existing);  // restart-in-place (Apply)
    existing->kind = kind;
    return existing;
  }
  auto s = std::make_unique<Session>();
  s->id = next_id_++;
  s->kind = kind;
  s->key = key;
  sessions_.push_back(std::move(s));
  return sessions_.back().get();
}

void AppController::TeardownSession(Session& s) {
  StopRtspExport(s);
  if (s.source) {
    if (s.attached_sink) s.source->SetFrameSink(nullptr);
    s.source->Stop();
    s.source->Close();
    s.source.reset();
  }
  s.attached_sink = nullptr;
  s.controls.reset();
  s.ctrl_dev.reset();
  s.last_generation = 0;
  s.display_stats = DisplayStats{};
}

void AppController::RebuildControls(Session& s) {
  s.controls.reset();
  s.ctrl_dev.reset();
  if (s.kind != SourceKind::kV4l2) return;
  s.ctrl_dev = std::make_shared<V4l2Device>();
  if (s.ctrl_dev->Open(s.key).ok()) {
    auto cs = std::make_unique<ControlSet>(s.ctrl_dev);
    if (cs->Refresh().ok()) s.controls = std::move(cs);
  }
  ++s.controls_epoch;
}

Status AppController::StartV4l2(const std::string& device, PixelFormat fmt,
                                int width, int height, double fps) {
  Session* s = EnsureSession(SourceKind::kV4l2, device);

  auto src = std::make_unique<V4l2Source>();
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kV4l2;
  desc.device = device;
  desc.format = fmt;
  desc.width = width;
  desc.height = height;
  desc.fps = fps;
  if (auto st = src->Open(desc); !st.ok()) return st;
  if (auto st = src->Start(); !st.ok()) return st;
  s->source = std::move(src);
  s->config = ActiveV4l2Config{device, fmt, width, height, fps};

  // Human label for tiles/headers: "card (videoN)".
  s->label = device;
  for (const auto& d : devices_) {
    if (d.device == device) {
      const auto slash = device.rfind('/');
      s->label = d.card + " (" +
                 (slash == std::string::npos ? device
                                             : device.substr(slash + 1)) +
                 ")";
    }
  }

  RebuildControls(*s);
  Select(device);
  XLOG_INFO("AppController: session '{}' started ({} running)", s->label,
            RunningCount());
  return Ok();
}

Status AppController::StartGst(const std::string& key,
                               const std::string& pipeline) {
#ifdef XMCAM_WITH_GSTREAMER
  Session* s = EnsureSession(SourceKind::kGst, key);
  auto src = std::make_unique<GstSource>();
  SourceDescriptor desc;
  desc.type = SourceDescriptor::Type::kGstreamer;
  desc.pipeline = pipeline;
  if (auto st = src->Open(desc); !st.ok()) return st;
  if (auto st = src->Start(); !st.ok()) return st;
  s->source = std::move(src);
  s->pipeline = pipeline;
  if (s->label.empty()) s->label = key;
  Select(key);
  return Ok();
#else
  (void)key;
  (void)pipeline;
  return Err(ErrorCode::kUnsupported, "GStreamer support not built");
#endif
}

Status AppController::StartFirstDevice() {
  RefreshDevices();
  if (devices_.empty()) return Err(ErrorCode::kNotFound, "no V4L2 device");
  const DeviceInfo& dev = devices_.front();

  const FormatDesc* fmt = nullptr;
  for (const auto& f : dev.caps.formats)
    if (f.format == PixelFormat::kMjpeg) fmt = &f;
  if (!fmt && !dev.caps.formats.empty()) fmt = &dev.caps.formats.front();
  if (!fmt || fmt->sizes.empty())
    return Err(ErrorCode::kUnsupported, "device has no usable format");

  const FrameSize* best = &fmt->sizes.front();
  long best_d = 1L << 60;
  for (const auto& sz : fmt->sizes) {
    const long d = std::abs(sz.width - 1280L) + std::abs(sz.height - 720L);
    if (d < best_d) { best_d = d; best = &sz; }
  }
  const double fps = best->fps.empty() ? 30.0 : best->fps.front();
  return StartV4l2(dev.device, fmt->format, best->width, best->height, fps);
}

void AppController::StopSession(const std::string& key) {
  auto it = std::find_if(sessions_.begin(), sessions_.end(),
                         [&](const auto& s) { return s->key == key; });
  if (it == sessions_.end()) return;
  TeardownSession(**it);
  sessions_.erase(it);
  // Stopping the selected session is an explicit reset, not a retarget.
  if (selected_key_ == key) selected_key_.clear();
  XLOG_INFO("AppController: session '{}' stopped ({} running)", key,
            RunningCount());
}

void AppController::StopAll() {
  for (auto& s : sessions_) TeardownSession(*s);
  sessions_.clear();
}

void AppController::MaintainRecovery() {
  for (auto& s : sessions_) {
    if (!s->source) continue;
    const SourceStats st = s->source->GetStats();
    if (st.generation == s->last_generation) continue;
    s->last_generation = st.generation;
    // Device returned on a (possibly new) node: rebuild the control fd; the
    // UI reloads cached values when controls_epoch changes. Values are NOT
    // auto re-applied — panels show the camera's actual post-recovery state.
    RebuildControls(*s);
    XLOG_INFO("AppController: '{}' controls rebuilt after recovery (gen {})",
              s->label, st.generation);
  }
}

bool AppController::AttachFrameSink(Session& s, FrameSink* sink) {
  if (!s.source || s.attached_sink) return false;
  s.attached_sink = sink;
  s.source->SetFrameSink(sink);
  return true;
}

void AppController::DetachFrameSink(Session& s, FrameSink* sink) {
  if (s.attached_sink != sink) return;
  if (s.source) s.source->SetFrameSink(nullptr);
  s.attached_sink = nullptr;
}

Status AppController::StartRtspExport(Session& s, int port,
                                      const std::string& mount) {
#ifdef XMCAM_WITH_RTSP_SERVER
  if (!s.source) return Err(ErrorCode::kInvalidArgument, "session not running");
  StopRtspExport(s);
  s.rtsp = std::make_unique<RtspSink>();
  if (auto st = s.rtsp->Start(port, mount); !st.ok()) {
    s.rtsp.reset();
    return st;
  }
  if (!AttachFrameSink(s, s.rtsp.get())) {
    s.rtsp->Stop();
    s.rtsp.reset();
    return Err(ErrorCode::kInvalidArgument,
               "frame tee busy (qualification tap active?)");
  }
  return Ok();
#else
  (void)port;
  (void)mount;
  return Err(ErrorCode::kUnsupported, "RTSP export not built");
#endif
}

void AppController::StopRtspExport(Session& s) {
#ifdef XMCAM_WITH_RTSP_SERVER
  if (s.rtsp) {
    DetachFrameSink(s, s.rtsp.get());
    s.rtsp->Stop();
    s.rtsp.reset();
  }
#else
  (void)s;
#endif
}

}  // namespace xmotion
