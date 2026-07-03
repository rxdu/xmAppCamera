/*
 * @file app_controller.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/app/app_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

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
#ifdef XMCAM_WITH_GSTREAMER
  StopRecording(s);
#endif
  if (s.source) {
    s.source->SetFrameSink(nullptr);
    s.source->Stop();
    s.source->Close();
    s.source.reset();
  }
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
  if (!s.source) return false;
  s.fanout.Add(sink);
  s.source->SetFrameSink(&s.fanout);
  return true;
}

void AppController::DetachFrameSink(Session& s, FrameSink* sink) {
  s.fanout.Remove(sink);
}

#ifdef XMCAM_WITH_GSTREAMER
Status AppController::StartRecording(Session& s, const std::string& path,
                                     FileSink::Format format,
                                     int64_t epoch_ns) {
  if (!s.source) return Err(ErrorCode::kInvalidArgument, "session not running");
  StopRecording(s);
  auto rec = std::make_unique<FileSink>();
  const double fps =
      s.kind == SourceKind::kV4l2 && s.config.fps > 0 ? s.config.fps : 30.0;
  if (auto st = rec->Start(path, format, fps, epoch_ns); !st.ok()) return st;
  s.recorder = std::move(rec);
  AttachFrameSink(s, s.recorder.get());
  return Ok();
}

Status AppController::StartRecordingGroup(const std::string& dir,
                                          FileSink::Format format) {
  if (RunningCount() == 0)
    return Err(ErrorCode::kInvalidArgument, "no running sources to record");

  char stamp[32];
  std::time_t t = std::time(nullptr);
  std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&t));
  group_dir_ = dir + "/" + stamp;
  std::error_code ec;
  std::filesystem::create_directories(group_dir_, ec);

  // Shared epoch on the capture clock (CLOCK_MONOTONIC == V4L2 timestamps):
  // every file's timeline zero is this instant, so frames align by CAPTURE
  // time no matter how many ms apart the recorders actually started.
  const int64_t epoch =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();

  FILE* mf = std::fopen((group_dir_ + "/manifest.yaml").c_str(), "w");
  if (mf) {
    std::fprintf(mf, "session_stamp: %s\nepoch_monotonic_ns: %lld\nfiles:\n",
                 stamp, static_cast<long long>(epoch));
  }

  Status first_err = Ok();
  for (auto& s : sessions_) {
    if (!s->IsRunning()) continue;
    // Passthrough only fits compressed USB modes; others fall back to H.264.
    FileSink::Format f = format;
    if (f == FileSink::Format::kPassthroughMkv &&
        !(s->kind == SourceKind::kV4l2 && IsCompressed(s->config.format)))
      f = FileSink::Format::kH264Mkv;
    // Network streams carry pipeline-relative timestamps: fall back to
    // arrival-time stamping (noted in the manifest).
    const bool epoch_ok = s->kind == SourceKind::kV4l2;

    std::string name = s->key;
    for (auto& ch : name)
      if (ch == '/' || ch == ':') ch = '_';
    const std::string path =
        group_dir_ + "/" + name + "." + FileSink::Extension(f);
    if (auto st = StartRecording(*s, path, f, epoch_ok ? epoch : 0);
        !st.ok() && first_err.ok())
      first_err = st;
    if (mf) {
      std::fprintf(mf,
                   "  - session: %s\n    label: \"%s\"\n    file: %s.%s\n"
                   "    sync: %s\n",
                   s->key.c_str(), s->label.c_str(), name.c_str(),
                   FileSink::Extension(f),
                   epoch_ok ? "capture-epoch" : "arrival-time");
      if (s->kind == SourceKind::kV4l2)
        std::fprintf(mf, "    config: %s %dx%d @%.0f\n",
                     ToString(s->config.format), s->config.width,
                     s->config.height, s->config.fps);
    }
  }
  if (mf) std::fclose(mf);
  group_recording_ = true;
  XLOG_INFO("group recording -> {}", group_dir_);
  return first_err;
}

void AppController::StopRecordingGroup() {
  for (auto& s : sessions_) StopRecording(*s);
  group_recording_ = false;
  XLOG_INFO("group recording stopped ({})", group_dir_);
}

void AppController::StopRecording(Session& s) {
  if (s.recorder) {
    DetachFrameSink(s, s.recorder.get());
    s.recorder->Stop();
    s.recorder.reset();
  }
}
#endif

Status AppController::StartRtspExport(Session& s, const std::string& address,
                                      int port, const std::string& mount) {
#ifdef XMCAM_WITH_RTSP_SERVER
  if (!s.source) return Err(ErrorCode::kInvalidArgument, "session not running");
  StopRtspExport(s);
  s.rtsp = std::make_unique<RtspSink>();
  if (auto st = s.rtsp->Start(address, port, mount); !st.ok()) {
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
  (void)address;
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
