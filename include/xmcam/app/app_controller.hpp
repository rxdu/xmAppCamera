/*
 * @file app_controller.hpp
 * @brief Session manager: owns N concurrently-streaming sources (USB cameras
 *        and network streams), each with its own controls, stats, and sink
 *        slot, plus a global "selected" session that the Controls/Qualify
 *        panels operate on. All methods are called from the render thread.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_APP_APP_CONTROLLER_HPP
#define XMCAM_APP_APP_CONTROLLER_HPP

#include <memory>
#include <string>
#include <vector>

#include "xmcam/control/control_set.hpp"
#include "xmcam/core/fanout_sink.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"
#include "xmcam/pipeline/v4l2_source.hpp"
#include "xmcam/pipeline/video_source.hpp"
#ifdef XMCAM_WITH_GSTREAMER
#include "xmcam/export/file_sink.hpp"
#endif
#ifdef XMCAM_WITH_RTSP_SERVER
#include "xmcam/export/rtsp_sink.hpp"
#endif

namespace xmotion {

class AppController {
 public:
  enum class SourceKind { kV4l2, kGst };

  struct ActiveV4l2Config {
    std::string device;
    PixelFormat format = PixelFormat::kUnknown;
    int width = 0;
    int height = 0;
    double fps = 0.0;
  };

  // Render-side counters, written by the preview tile each frame.
  struct DisplayStats {
    double display_fps = 0.0;
    double upload_ms = 0.0;
    double latency_ms = 0.0;
    uint64_t frames_shown = 0;
    int width = 0;
    int height = 0;
  };

  // One concurrently-streaming source. `key` identifies it across the UI:
  // the device path for cameras, "network" for the (single) gst session.
  struct Session {
    int id = 0;
    SourceKind kind = SourceKind::kV4l2;
    std::string key;
    std::string label;  // human title for tiles/headers: "card (videoN)"
    std::unique_ptr<VideoSource> source;
    std::shared_ptr<V4l2Device> ctrl_dev;  // v4l2 only
    std::unique_ptr<ControlSet> controls;  // v4l2 only
    ActiveV4l2Config config;               // v4l2 only
    std::string pipeline;                  // gst only
    DisplayStats display_stats;
    int controls_epoch = 0;
    uint32_t last_generation = 0;
    // Frame tee: RTSP export, file recorder and the qualification tap all
    // consume concurrently through the fanout.
    FanoutSink fanout;
#ifdef XMCAM_WITH_RTSP_SERVER
    std::unique_ptr<RtspSink> rtsp;
#endif
#ifdef XMCAM_WITH_GSTREAMER
    std::unique_ptr<FileSink> recorder;
#endif

    bool IsRunning() const { return source && source->IsRunning(); }
  };

  AppController() = default;
  ~AppController();

  // UI preference: draw live stats on the preview tiles (default) instead of
  // in the sidebar blocks. Owned here so all panels agree.
  bool stats_overlay = true;

  // --- device discovery ---
  const std::vector<DeviceInfo>& RefreshDevices();
  const std::vector<DeviceInfo>& devices() const { return devices_; }

  // --- session lifecycle (render thread) ---
  // Create (or restart, if this device already has a session) and select it.
  Status StartV4l2(const std::string& device, PixelFormat fmt, int width,
                   int height, double fps);
  // Network-stream sessions are keyed by the caller (e.g. "net1", "net2"),
  // so several streams can run side by side like camera slots.
  Status StartGst(const std::string& key, const std::string& pipeline);
  void StopSession(const std::string& key);
  void StopAll();
  Status StartFirstDevice();  // demo/headless-test convenience

  const std::vector<std::unique_ptr<Session>>& sessions() const {
    return sessions_;
  }
  Session* FindSession(const std::string& key);
  size_t RunningCount() const;

  // --- selection (drives Controls / Qualify / RTSP-export targeting) ---
  void Select(const std::string& key) { selected_key_ = key; }
  const std::string& selected_key() const { return selected_key_; }
  Session* selected() { return FindSession(selected_key_); }

  // The camera the Controls/Qualify panels should target:
  //  - the selected session when it is a camera;
  //  - the first running camera when the selection merely sits elsewhere
  //    (e.g. a network tile) — tuning works right after Start, no dropdown;
  //  - NONE when the user explicitly deselected (empty key): a deliberate
  //    "hands off" that guards against tuning the wrong camera.
  Session* SelectedCamera() {
    if (selected_key_.empty()) return nullptr;
    Session* s = selected();
    if (!s) return nullptr;  // stale key (session stopped): stay hands-off
    if (s->controls) return s;
    for (auto& c : sessions_)
      if (c->controls && c->IsRunning()) return c.get();
    return nullptr;
  }

  // --- per-frame (render thread) ---
  void MaintainRecovery();  // all sessions; call once per frame
  bool PullFrame(Session& s, VideoFrame* out) {
    return s.source && s.source->Frames().TryPull(*out);
  }

  // --- per-session tee (fanout: multiple sinks at once) ---
  bool AttachFrameSink(Session& s, FrameSink* sink);
  void DetachFrameSink(Session& s, FrameSink* sink);
  Status StartRtspExport(Session& s, const std::string& address, int port,
                         const std::string& mount);
  void StopRtspExport(Session& s);
#ifdef XMCAM_WITH_GSTREAMER
  Status StartRecording(Session& s, const std::string& path,
                        FileSink::Format format, int64_t epoch_ns = 0);
  void StopRecording(Session& s);

  // Synchronized recording: start every running session's recorder against a
  // SHARED capture-clock epoch, grouped under <dir>/<stamp>/ with a
  // manifest.yaml for downstream alignment. Stop halts all of them.
  Status StartRecordingGroup(const std::string& dir, FileSink::Format format);
  void StopRecordingGroup();
  bool GroupRecording() const { return group_recording_; }
  const std::string& group_dir() const { return group_dir_; }
#endif

  // --- target-camera conveniences (Controls / Qualify panels) ---
  bool IsRunning() {
    Session* s = SelectedCamera();
    return s && s->IsRunning();
  }
  ControlSet* Controls() {
    Session* s = SelectedCamera();
    return s ? s->controls.get() : nullptr;
  }
  int controls_epoch() {
    Session* s = SelectedCamera();
    return s ? s->controls_epoch : -1;
  }
  const std::string& active_device() {
    static const std::string kEmpty;
    Session* s = SelectedCamera();
    return s ? s->key : kEmpty;
  }
  SourceStats Stats() {
    Session* s = SelectedCamera();
    return (s && s->source) ? s->source->GetStats() : SourceStats{};
  }
  DisplayStats display_stats() {
    Session* s = SelectedCamera();
    return s ? s->display_stats : DisplayStats{};
  }

 private:
  Session* EnsureSession(SourceKind kind, const std::string& key);
  void TeardownSession(Session& s);
  void RebuildControls(Session& s);

  std::vector<DeviceInfo> devices_;
  std::vector<std::unique_ptr<Session>> sessions_;
  bool group_recording_ = false;
  std::string group_dir_;
  std::string selected_key_;
  int next_id_ = 1;
};

}  // namespace xmotion

#endif  // XMCAM_APP_APP_CONTROLLER_HPP
