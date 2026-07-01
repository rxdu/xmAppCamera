# xmAppCamera — Design

**Status:** Draft (pre-implementation) · **Owner:** rdu · **Last updated:** 2026-07-01

A lightweight, low-latency desktop GUI for previewing and fine-tuning camera sources — USB/V4L2 cameras and RTSP/UDP network streams — built on the xmotion family (quickviz + xmSigma).

---

## 1. Goals & Non-Goals

### Goals
- **G1 — USB cameras:** reliably enumerate V4L2 devices; introspect *every* control the camera exposes; let the user fine-tune them live; export the result to a re-loadable config file.
- **G2 — Network streams:** compose GStreamer pipelines to decode RTSP/UDP streams with minimal friction (guided builder + raw override).
- **G3 — RTSP re-export (nice-to-have):** publish any active source as an RTSP stream.
- **NFR — Efficient, low-latency, smooth, lightweight, flexible.** These are first-class requirements, not aspirations. They drive concrete choices: latest-only frame delivery, single-copy frame path, no OpenCV, auto hardware decode.

### Non-Goals (v1)
- Not a reusable capture *library* — it is an end-user app. This is why we deliberately avoid an OpenCV/`cv::Mat` intermediate representation (pure overhead when nothing downstream consumes frames programmatically).
- No recording/NLE/analytics. Snapshot may come later; live preview + config export is the core.
- No non-Linux capture backend in v1 (V4L2 is Linux-only by nature; the GStreamer path is portable but untested elsewhere).

---

## 2. Platform Substrate (what we reuse)

| Dependency | Provides | Target / include |
|---|---|---|
| **quickviz** (xmGamma) | `Viewer` + ImGui docking, `Panel` base, `Texture` renderable (raw buffer → GL via PBO), `DataStream<T>`, `RingBuffer<T>` | `quickviz` (INTERFACE) / `quickviz::scene`, `quickviz::viewer` |
| **xmSigma** | `XLOG_*` logging, common types | `xmotion::xmSigma` / `xmsigma/logging/xlogger.hpp` |
| **GStreamer 1.x** | decode/network/encode elements, `appsink`/`appsrc`, `gst_parse_launch` | pkg-config `gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0` (+ `gstreamer-rtsp-server-1.0` in phase 2) |
| **libv4l2 / linux uapi** | device + control + format ioctls | `<linux/videodev2.h>`, optional `libv4l2` |
| **yaml-cpp** | config read/write | `find_package(yaml-cpp)` — family standard (xmNabla, swervebot) |

**Explicitly NOT used:** OpenCV / `cv::Mat`, and therefore quickviz's `CvImageWidget` / `BufferedCvImageWidget`. See ADR-0001 and `docs/adr`.

Conventions inherited from the family: C++17, `namespace xmotion`, Google `clang-format` (`SortIncludes: Never`), GoogleTest via submodule, `include/<proj>/ · src/ · test/` layout, `xmotion::` CMake namespace, dependencies point downward only.

**Dependency wiring (ADR D8):** quickviz (pinned `68b79c4`), xmSigma (`d65e418`), and googletest (`973323e`) are git submodules under `third_party/`, consumed via `add_subdirectory` with a `find_package` fallback. GStreamer 1.20.x and yaml-cpp 0.7 are system packages (installed). `gstreamer-rtsp-server-1.0` is not yet installed — needed only for RTSP export (Phase 5).

---

## 3. Architecture Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│ app/          main() · AppController · window+docking composition        │  ← quickviz Viewer
├────────────────────────────────────────────────────────────────────────┤
│ ui/ panels    DevicePanel · PreviewPanel · ControlPanel ·                │  ← quickviz Panel
│               PipelinePanel · ExportPanel · Stats/LogPanel                │
├───────────────────────────┬────────────────────────────────────────────┤
│ pipeline/                 │ control/           │ export/                 │
│  VideoSource (interface)  │  ControlSet        │  ConfigWriter/Reader    │
│  ├ V4l2Source             │  ControlInfo       │  FrameSink (interface)  │
│  └ GstSource              │  ControlIntrospector│  └ RtspSink (phase 2)  │
│  FrameUploader (interface)│  (V4L2 QUERY_EXT_CTRL)                       │
│  ├ PboUploader (v1)       │                    │                         │
│  └ GlMemUploader (later)  │                    │                         │
├───────────────────────────┴────────────────────────────────────────────┤
│ core/         VideoFrame · PixelFormat · SourceDescriptor · SourceCaps · │  ← value types, no deps
│               SourceStats · Result/Error · config schema types           │
└────────────────────────────────────────────────────────────────────────┘
        vendored: quickviz · xmSigma · GStreamer · libv4l2 · yaml-cpp
```

Dependencies point downward only. `core/` has no dependency on quickviz/GStreamer/V4L2 and is unit-testable in isolation. `pipeline/`, `control/`, `export/` are pure logic testable without a window. `ui/` and `app/` are the only layers that touch quickviz.

---

## 4. The Frame Path (the performance-critical spine)

### 4.1 `VideoFrame` — native frame currency (no cv::Mat)

```cpp
namespace xmotion {

enum class PixelFormat { kGray8, kRgb8, kRgba8, kBgr8, kBgra8, kYuyv, kNv12, kI420 };

// A cheap-to-copy handle to a decoded frame. Copying bumps a refcount; it does
// NOT copy pixels. The underlying buffer (GstBuffer map, or V4L2 mmap slot) is
// kept alive by `owner` and released (unmap / VIDIOC_QBUF re-queue) when the
// last VideoFrame referencing it is destroyed.
struct VideoFrame {
  int          width  = 0;
  int          height = 0;
  PixelFormat  format = PixelFormat::kRgba8;
  int          stride = 0;         // bytes per row of plane 0
  const uint8_t* data = nullptr;   // plane 0 base pointer
  const uint8_t* plane1 = nullptr; // e.g. UV plane for NV12 (nullptr if packed)
  int          stride1 = 0;
  int64_t      pts_ns = 0;
  uint64_t     seq    = 0;
  std::shared_ptr<void> owner;     // custom deleter: unmaps / requeues the source buffer
};

}  // namespace xmotion
```

`VideoFrame` is trivially cheap to move/copy, so it flows through `DataStream<VideoFrame>` (which stores by value in a `DoubleBuffer`) without copying pixels — the refcount is what moves.

### 4.2 Threading model

```
 producer thread (one per active source)          render thread (quickviz Viewer loop)
 ┌─────────────────────────────────────┐          ┌──────────────────────────────────┐
 │ V4l2Source / GstSource:             │  Push()  │ pre-draw callback:                │
 │  dequeue decoded buffer → wrap in   │ ───────► │  DataStream<VideoFrame>::TryPull  │
 │  VideoFrame → stream.Push(frame)    │          │  → FrameUploader::Upload → GLuint │
 │  (apply pending control cmds here)  │          │  → PreviewPanel draws texture     │
 └─────────────────────────────────────┘          └──────────────────────────────────┘
        DataStream<VideoFrame>  (latest-only, lossy, never blocks the render loop)
```

Rules (inherited from quickviz's canonical streaming pattern):
1. **All GL calls on the render thread only.** Producers never touch GL. Upload happens in the pre-draw callback.
2. **Render never blocks.** `TryPull` returns the newest frame or "nothing new" (reuse last texture). Intermediate frames are dropped — correct for a live preview.
3. **Control writes are marshalled to the capture thread.** The V4L2 `fd` is owned by the capture thread. The UI posts `SetControl{id,value}` onto a lock-free SPSC command queue; the capture thread applies them between `DQBUF`/`QBUF` cycles. Query/enumeration happens once at open (plus re-query of dependent controls after a write).

### 4.3 `FrameUploader` seam (decision: PBO now, GL-interop later)

```cpp
class FrameUploader {
 public:
  virtual ~FrameUploader() = default;
  // Called on the render thread. Returns a GL texture id ready to sample.
  virtual GLuint Upload(const VideoFrame& frame) = 0;
};
```

- **`PboUploader` (v1):** double-buffered Pixel Buffer Objects; `glMapBuffer` → `memcpy` frame → `glTexSubImage2D`. One CPU→GPU copy, no cv::Mat. Handles ~1080p60 comfortably. For packed RGB(A)/BGR(A) it uploads directly; for `kYuyv`/`kNv12` it uploads the Y/UV planes as textures and a **YUV→RGB fragment shader** does the conversion on the GPU (avoids a CPU `videoconvert`). v1 fallback for exotic formats: let GStreamer `videoconvert` emit RGBA.
- **`GlMemUploader` (later):** shares the GLFW GL context with GStreamer so GPU-decoded frames stay as `GstGLMemory` and are sampled with zero CPU roundtrip. Drops in behind the same interface without touching sources or UI.

> Note: quickviz's `Texture` renderable already implements PBO streaming for packed formats (`kRgb/kRgba/kBgr/kBgra`). The `PboUploader` wraps/extends it; the YUV-shader path is a small custom renderable we add.

---

## 5. USB / V4L2 Source (G1)

### 5.1 Enumeration
- Discover devices by scanning `/dev/video*` (and, for robustness against index churn, resolve stable names via `/dev/v4l/by-id/`).
- For each: `VIDIOC_QUERYCAP` (name, bus, capabilities), filter to `V4L2_CAP_VIDEO_CAPTURE`. A single physical camera can expose multiple `/dev/videoN` nodes; only capture-capable nodes are listed, annotated with card name + bus info so duplicates are distinguishable.
- Formats: `VIDIOC_ENUM_FMT` (fourccs incl. compressed MJPG/H264) → `VIDIOC_ENUM_FRAMESIZES` → `VIDIOC_ENUM_FRAMEINTERVALS` build a `SourceCaps` tree {fourcc → {WxH → [fps]}}.

### 5.2 Capture
- `VIDIOC_S_FMT` to negotiate; `VIDIOC_REQBUFS`(MMAP) + `QUERYBUF` + `mmap` + `QBUF`; `VIDIOC_STREAMON`; loop `poll` → `DQBUF` → wrap slot in `VideoFrame` (owner re-queues via `QBUF` on destruction) → `Push`.
- **Raw formats** (YUYV/UYVY/NV12/…) → wrapped directly, zero decode.
- **Compressed formats** (MJPEG/H.264, common webcam defaults) → fed into an internal GStreamer branch (`appsrc ! jpegdec|decodebin3 ! appsink`) and the decoded output is wrapped. This is the "V4l2Source has a decode branch" decision.

### 5.3 Control introspection (the heart of G1)
- Walk the full control list with `VIDIOC_QUERY_EXT_CTRL` + `V4L2_CTRL_FLAG_NEXT_CTRL` (captures standard, camera-class, and driver-private controls — everything the camera exposes).
- Model:
  ```cpp
  enum class ControlType { kInteger, kInteger64, kBoolean, kMenu, kIntegerMenu,
                           kBitmask, kButton, kString };
  struct ControlInfo {
    uint32_t id; std::string name; ControlType type;
    int64_t min, max, step, default_value;
    std::vector<std::pair<int64_t, std::string>> menu;  // kMenu / kIntegerMenu
    uint32_t flags;  // INACTIVE, READ_ONLY, VOLATILE, UPDATE, ...
  };
  ```
- UI renders by type: slider (int), checkbox (bool), combo (menu), button; disabled when `INACTIVE`/`READ_ONLY`. **Dependency handling:** after any write, re-query sibling controls (e.g. setting `exposure_auto` to Manual clears the `INACTIVE` flag on `exposure_absolute`); `VOLATILE` controls (auto-driven gain/exposure) are polled for display.
- Reads/writes via `VIDIOC_G_EXT_CTRLS`/`S_EXT_CTRLS` on the capture-thread fd (command queue).

### 5.4 Config export
- `ConfigWriter` serialises to YAML: source descriptor (type, device path + stable by-id), negotiated format (fourcc, WxH, fps), and a `controls:` map `{name: value}` (+ id for exactness). Schema in `docs/design/config-schema.md` — stable and documented so other xmotion apps can consume it. `ConfigReader` re-applies a saved config to a matching device.

---

## 6. Network Streams — GStreamer Source (G2)

- `GstSource` builds a pipeline ending in `appsink` (emit-signals, max-buffers=1, drop=true for latest-only): typically `rtspsrc/udpsrc ! rtp<codec>depay ! decodebin3 ! appsink`, pulls `GstSample`, maps the `GstBuffer`, wraps in `VideoFrame`.
- **Auto HW/SW decode:** `decodebin3` selects the best available decoder (VAAPI/NVDEC → software fallback) so one build runs on any box; a manual override lets the user pin an element.
- **PipelinePanel UX (guided + raw):** dropdowns (protocol rtsp/udp/tcp · codec h264/h265/mjpeg/raw · latency ms · HW-decode auto/off · extra caps) generate a pipeline string shown in an **editable** text field. A **Validate** button runs `gst_parse_launch` in `NULL` state to catch syntax/element errors before **Play**. Presets are loaded from a data-driven `config/presets.yaml` so common sources can be added without recompiling.
- Resilience: network sources auto-reconnect with backoff on EOS/error; bus messages surfaced to the Stats/LogPanel.

---

## 7. RTSP Re-export — nice-to-have (G3, phase 2)

- **`FrameSink` seam designed in v1:** an active `VideoSource` can tee its `VideoFrame`s to N sinks. `DisplaySink` (always on) drives the preview; additional sinks are opt-in.
  ```cpp
  class FrameSink {
   public:
    virtual ~FrameSink() = default;
    virtual void OnFrame(const VideoFrame& frame) = 0;  // producer thread
  };
  ```
- **`RtspSink` (phase 2 implementation):** `appsrc` fed from `VideoFrame` → auto-selected encoder (`nvh264enc`/`vaapih264enc`/`x264enc`, mirroring decode detection) → `rtph264pay` → in-process **gst-rtsp-server** mount point (`rtsp://host:8554/<name>`). Because frames are already native buffers, they feed `appsrc` directly with no cv::Mat detour.

---

## 8. Requirements Traceability

| Req | Blocks | Verification |
|---|---|---|
| G1 enumerate | `V4l2Source::Enumerate`, DevicePanel | unit test against mocked ioctls; manual: list matches `v4l2-ctl --list-devices` |
| G1 controls | `ControlIntrospector`, `ControlSet`, ControlPanel | test control-model build; manual: matches `v4l2-ctl --list-ctrls-menus` |
| G1 export | `ConfigWriter/Reader`, ExportPanel | round-trip test write→read→apply |
| G2 decode | `GstSource`, PipelinePanel | test pipeline-string generation + `gst_parse_launch` validation; manual: play rtsp/udp |
| G3 export | `FrameSink`, `RtspSink` | phase 2; manual: consume with ffplay/vlc |
| NFR latency | `DataStream`, `VideoFrame`, `PboUploader` | measure glass-to-glass; frame-drop counters in StatsPanel |

---

## 9. Observability & Failure Handling
- `XLOG_*` at every boundary: device open, format negotiation, decoder selection, stream errors, frame drops, control writes.
- `SourceStats` (capture fps, display fps, dropped count, end-to-end latency estimate, decoder in use) shown in a compact overlay/StatsPanel.
- Graceful degradation: HW decode failure → software fallback (logged, not silent); network disconnect → bounded reconnect backoff; missing GStreamer element → actionable error in the UI, not a crash. No silent retries or swallowed errors (family anti-pattern #3).

---

## 10. Module / Directory Layout

```
xmAppCamera/
├── CMakeLists.txt              # C++17, finds quickviz/xmSigma/GStreamer/yaml-cpp; no OpenCV
├── README.md · TODO.md
├── docs/{DESIGN.md, adr/, design/config-schema.md}
├── include/xmcam/
│   ├── core/      video_frame.hpp pixel_format.hpp source_descriptor.hpp
│   │              source_caps.hpp source_stats.hpp result.hpp
│   ├── pipeline/  video_source.hpp v4l2_source.hpp gst_source.hpp
│   │              frame_uploader.hpp pbo_uploader.hpp
│   ├── control/   control_info.hpp control_set.hpp control_introspector.hpp
│   ├── export/    config_writer.hpp config_reader.hpp frame_sink.hpp rtsp_sink.hpp
│   ├── ui/        device_panel.hpp preview_panel.hpp control_panel.hpp
│   │              pipeline_panel.hpp export_panel.hpp stats_panel.hpp
│   └── app/       app_controller.hpp
├── src/           (mirrors include/, + main.cpp)
├── test/          gtest: core types, control-model build, config round-trip, pipeline-string gen
├── config/        presets.yaml  example_export.yaml
└── cmake/         xmAppCameraConfig.cmake.in (if ever installed)
```

Namespace `xmotion`; folder/include prefix `xmcam`.

---

## 11. Delivery Phases

- **Phase 0 — Skeleton:** CMake + dependency wiring, `Viewer`+docking shell, Stats/LogPanel, `core/` value types + tests.
- **Phase 1 — USB preview:** `V4l2Source` enumerate + capture, `DataStream<VideoFrame>`, `PboUploader`, DevicePanel + PreviewPanel. Start with a **zero-decode YUYV first-light** (640×480@30) to validate capture→upload, then add the **MJPEG→GStreamer `jpegdec` decode branch** (the test camera's primary format). *Milestone: live USB preview at full-res MJPEG, no controls.*
- **Phase 2 — USB tuning:** `ControlIntrospector` + `ControlSet`, ControlPanel (typed widgets + dependency re-query), YAML export/import. *Milestone: G1 complete.*
- **Phase 3 — Network:** `GstSource` + PipelinePanel (guided + raw + validate), compressed-USB decode branch. *Milestone: G2 complete.*
- **Phase 4 — Perf hardening:** HW-decode auto-detect polish, YUV-shader upload, `GlMemUploader` GL-interop seam. *Milestone: NFR targets measured.*
- **Phase 5 — RTSP export (nice-to-have):** `RtspSink` + gst-rtsp-server. *Milestone: G3.*

---

## 12. Open Questions / Risks
- **YUV-on-GPU vs videoconvert:** shader path is the perf win but adds a custom renderable; validate quickviz `Texture` can be extended vs. adding `YuvTexture`. (Behind `FrameUploader`, so contained.)
- **GstGL context sharing** with GLFW is driver-sensitive; keep it strictly behind the `GlMemUploader` seam and gate on runtime capability probe.
- **Control dependency semantics** vary by driver; rely on re-query of `flags` rather than hard-coded control relationships.
- **Multi-node cameras / index churn:** mitigate with `/dev/v4l/by-id/` stable identifiers in exported configs.
