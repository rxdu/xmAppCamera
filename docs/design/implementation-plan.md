# xmAppCamera — Implementation Plan

Companion to `DESIGN.md` (architecture) and `TODO.md` (task checklist). This is the **how/order**: block decomposition, the interfaces each block introduces, how each is tested, and the exit criteria + on-hardware verification per phase. Blocks are sized < ~1 day and committed at green state.

Legend: **[T]** unit-tested (gtest), **[H]** verified on the real camera, **[M]** manual/visual check.

---

## Cross-cutting foundations (hold for every phase)

- **Namespace** `xmotion`, include prefix `xmcam`. Layers: `core → {pipeline, control, export} → {ui, app}`; only `ui`/`app` touch quickviz.
- **Logging & perf instrumentation** (your explicit ask):
  - `XLOG_INFO` — lifecycle: device open, format negotiated, decoder chosen, start/stop, disconnect/reconnect, config save/load.
  - `XLOG_WARN/ERROR` — every failure boundary (no silent catches; family anti-pattern #3).
  - `XLOG_DEBUG` — per-second aggregates (fps, drops, queue depth).
  - `XLOG_TRACE` — per-frame per-stage timings (compile-gated, zero cost in release).
  - `core/util/scope_timer.hpp` — RAII stage timer (`{ ScopeTimer t("decode"); ... }` → TRACE µs).
  - `core/util/rate_counter.hpp` — EWMA fps / drop counter feeding `SourceStats` and the StatsPanel.
  - Glass-to-glass latency = present-time − frame PTS, reported per second.
  - **In-app LogPanel**: a ring-buffer spdlog sink (xmBase is spdlog-backed) rendered in an ImGui panel, so bottlenecks are visible without a terminal.
- **Error handling**: `core/result.hpp` (`Status`/`Result<T>` with `{code,message}`); no exceptions across thread boundaries.
- **Threading**: one producer thread per active `VideoSource`; render thread owns all GL; control writes marshalled to the capture thread via an SPSC command queue.
- **Definition of done per block**: builds, `clang-format` clean, tests green, `XLOG` at boundaries, committed.

---

## Phase 0 — Skeleton & foundations
*Goal: a compiling, launching, logging app shell with tested core types.*

- **0.1 Build system** — root `CMakeLists.txt`: C++17, `add_subdirectory(third_party/{xmBase,quickviz,googletest})` with `find_package` fallback; `pkg-config` GStreamer (`gstreamer-1.0 app video`) + yaml-cpp; option `XMCAM_WITH_GSTREAMER=ON`; **assert OpenCV is not linked** (guard target). Export `compile_commands.json`.
- **0.2 Style/tooling** — `.clang-format` (Google, `SortIncludes: Never`), `ctest` wiring.
- **0.3 `core/` value types + tests [T]**:
  - `pixel_format.hpp` — `enum PixelFormat` + `BytesPerPixel/IsPlanar/ToGl()/FromV4l2Fourcc()/FromGstFormat()/to_string`.
  - `video_frame.hpp` — the `VideoFrame` struct (DESIGN §4.1).
  - `source_descriptor.hpp`, `source_caps.hpp` (`{fourcc→{WxH→[fps]}}`), `source_stats.hpp`, `result.hpp`, `util/{scope_timer,rate_counter}.hpp`.
  - Tests: fourcc↔PixelFormat↔GL mappings, caps insertion/query, rate-counter math.
- **0.4 App shell** — `app/app_controller.*`, `main.cpp`: quickviz `Viewer` + docking; empty docked panels (Device/Preview/Control/Pipeline/Stats/Log); `XLOG` init banner; LogPanel sink; clean shutdown.
- **Exit criteria**: `cmake --build` OK; `ldd` shows **no** `libopencv`; window launches with docked panels; `ctest` green. **[H]** launches on the dev host.

---

## Phase 1 — USB live preview
*Goal: smooth full-res MJPEG preview from the camera, fps visible. The core visual milestone.*

- **1.1 `VideoSource` interface** (`pipeline/video_source.hpp`):
  ```cpp
  class VideoSource {
   public:
    virtual ~VideoSource() = default;
    virtual Result Open(const SourceDescriptor&) = 0;
    virtual Result Start() = 0;   // spawns producer thread → Frames()
    virtual void   Stop()  = 0;
    virtual void   Close() = 0;
    virtual bool   IsRunning() const = 0;
    virtual SourceCaps GetCaps()  const = 0;
    virtual SourceStats GetStats() const = 0;
    virtual DataStream<VideoFrame>& Frames() = 0;
    virtual ControlSet* Controls() { return nullptr; }  // V4L2 only
  };
  ```
- **1.2 `V4l2Source` enumerate [T,H]** — `Enumerate()` scans `/dev/video*` + `/dev/v4l/by-id/`, `QUERYCAP`, filters to capture nodes with non-empty `ENUM_FMT` (drops the metadata node); builds `SourceCaps` via `ENUM_FMT/FRAMESIZES/FRAMEINTERVALS`. Ioctl calls behind a thin `V4l2Device` wrapper with a mockable seam for tests. *[H] output matches `v4l2-ctl --list-formats-ext`.*
- **1.3 `V4l2Source` raw capture [H]** — `S_FMT` → `REQBUFS(MMAP, n=4)` → `QUERYBUF`+`mmap` → `QBUF` → `STREAMON`; producer thread `poll→DQBUF→` wrap slot in `VideoFrame` whose `owner` re-queues (`QBUF`) the buffer index on destruction → `Push`. Start with **YUYV 640×480@30** (zero-decode first light).
- **1.4 `FrameUploader` + `PboUploader` [M]** (`pipeline/frame_uploader.hpp`): render-thread `GLuint Upload(const VideoFrame&)`; packed RGB/BGR → quickviz `Texture` (PBO) directly. First-light YUYV handled by a temporary CPU YUYV→RGB shim (flagged; replaced by GPU shader in Phase 4).
- **1.5 MJPEG decode branch [H]** — when negotiated format is MJPG: internal GStreamer `appsrc ! jpegdec ! videoconvert ! appsink(RGBA)`; wrap decoded `GstBuffer` → `VideoFrame(RGBA)` → same uploader. This becomes the **real preview path** (1920×1200@90 capable).
- **1.6 `DevicePanel` [M]** — list devices; pick format/resolution/fps from caps; Start/Stop.
- **1.7 `PreviewPanel` + render wiring [M]** — pre-draw callback `TryPull→Upload`; draw texture (keep-aspect fit); fps overlay.
- **Exit criteria [H]**: live MJPEG preview, smooth; StatsPanel shows capture/display fps; decode µs in TRACE logs. *Verify at 1280×720@60 and 1920×1200@30.*

---

## Phase 2 — USB control tuning *(completes G1)*
*Goal: introspect + live-tune every control; export/import config.*

- **2.1 `ControlInfo` model** (`control/control_info.hpp`) — `ControlType{kInteger,kInteger64,kBoolean,kMenu,kIntegerMenu,kBitmask,kButton,kString}` + `ControlInfo{id,name,type,min,max,step,default,menu[],flags}`.
- **2.2 `ControlIntrospector` [T,H]** — `QUERY_EXT_CTRL` + `NEXT_CTRL` walk; `QUERYMENU` for menu labels; group by control class. *[H] returns the camera's 20 controls with correct types/ranges/menus.*
- **2.3 `ControlSet` [H]** — `Get(id)`, queued `Set(id,value)` applied on the capture-thread fd via SPSC queue between `DQBUF/QBUF`; **re-query flags after each write** (auto/manual dependency); poll `VOLATILE` controls for display.
- **2.4 `ControlPanel` [H]** — typed widgets (slider/checkbox/combo/button); disabled on `INACTIVE`/`READ_ONLY`; reset-to-default; grouped by class. *[H] toggling Auto Exposure clears the INACTIVE flag on Exposure Time (the live 0x10 case); slider moves visibly change the preview.*
- **2.5 `ConfigWriter`/`ConfigReader` [T,H]** — YAML per `config-schema.md` (source + negotiated format + controls map + by-id); `ExportPanel` Save/Load. *[T] write→read→apply round-trip; [H] reload restores control state.*
- **Exit criteria**: all 20 controls enumerated + tuned live, dependency flags correct, YAML round-trips. **G1 complete.**

---

## Phase 3 — Network streams *(completes G2)*
*Goal: compose GStreamer pipelines to decode RTSP/UDP.*

- **3.1 `GstSource` [H]** (`pipeline/gst_source.hpp`) — pipeline ending in `appsink`(max-buffers=1, drop=true); pull `GstSample`, map `GstBuffer`→`VideoFrame` (`owner` unrefs the sample); `decodebin3`; bus watch → errors/EOS → LogPanel; bounded reconnect backoff. Reuses the Phase-1 display path unchanged (format-agnostic).
- **3.2 Pipeline builder [T]** (`pipeline/pipeline_builder.*`) — guided params (protocol/codec/latency/HW-decode/caps) → pipeline string; presets loaded from `config/presets.yaml`. *[T] param-set → expected string.*
- **3.3 `PipelinePanel` [M]** — dropdowns + editable raw override + **Validate** (`gst_parse_launch` in NULL state) + Play/Stop.
- **3.4 Decode auto/override [H]** — `decodebin3` picks VAAPI/NVDEC else software; manual element override.
- **Exit criteria [H]**: play an RTSP and a UDP stream (local `videotestsrc→rtsp/udp` test rig); guided + raw both work; Validate catches bad pipelines. **G2 complete.**

---

## Phase 4 — Performance hardening *(NFR)*
*Goal: measure and shrink latency/CPU; remove conversion copies.*

- **4.1 GPU YUV uploader [M]** — `YuvTexture` renderable (or extend quickviz `Texture`) with an NV12/YUYV→RGB fragment shader; drop the CPU YUYV shim and `videoconvert` where possible (decode straight to NV12/I420, convert on GPU).
- **4.2 `GlMemUploader` seam [M]** — GstGL interop (GPU-decoded frames stay `GstGLMemory`), gated behind a runtime capability probe; may ship as a documented stub if the driver path is risky.
- **4.3 `StatsPanel` full [M]** — capture/display fps, drops, queue depth, per-stage µs, glass-to-glass latency, active decoder.
- **4.4 Latency pass [H]** — tune V4L2 buffer count + appsink queueing; record measured numbers in `docs/design/perf-notes.md`.
- **Exit criteria [H]**: measured fps/latency documented; no per-frame CPU pixel conversion on the hot path.

---

## Phase 5 — RTSP re-export *(nice-to-have, G3)*
*Goal: publish any active source as RTSP.*

- **5.1** Install `gstreamer-rtsp-server-1.0` (dev).
- **5.2 `FrameSink` tee** (`export/frame_sink.hpp`) — a `VideoSource` fans frames to `DisplaySink` (always) + optional sinks.
- **5.3 `RtspSink` [H]** — `appsrc`←`VideoFrame`→auto encoder (`nvh264enc`/`vaapih264enc`/`x264enc`)→`rtph264pay`→in-process gst-rtsp-server mount `rtsp://host:8554/<name>`.
- **5.4 ExportPanel RTSP controls** — mount/port/encoder/bitrate.
- **Exit criteria [H]**: consume the app's RTSP output with `ffplay`/VLC. **G3 complete.**

---

## Sequencing & dependencies

```
Phase 0 (core + shell)
   └─► Phase 1 (VideoSource, V4l2Source, uploader, preview)
          ├─► Phase 2 (controls)      ─┐  needs V4l2Source fd
          ├─► Phase 3 (GstSource)      ─┼─ 2 & 3 independent; done sequentially for focus
          │                             │  (both reuse the Phase-1 display path)
          └─► Phase 5 (FrameSink/RTSP) ─┘  needs a running source
   Phase 4 (perf) ── after 1 & 3
```

Critical path to a **useful demo**: Phase 0 → 1 (live preview), then 2 (the headline "tune parameters" feature). 3/4/5 layer on without reworking earlier code — the `VideoSource`/`FrameUploader`/`FrameSink` seams are what make that true.

## Testing strategy
- **gtest** on all pure logic: pixel/format/GL mappings, caps building, control-model construction (behind a mockable ioctl seam), config round-trip, pipeline-string generation. Target ≥80% on `core`/`control`/`export`.
- **Hardware [H]** checks at each phase exit against the connected camera; a small set of live tests gated on camera presence.
- **Local gate before each commit**: `clang-format` + build + `ctest`.

## Risks (see also DESIGN §12)
- YUV-on-GPU vs `videoconvert` — contained behind `FrameUploader`; Phase 4.
- GstGL↔GLFW context sharing is driver-sensitive — gated behind `GlMemUploader` + capability probe.
- Per-driver control-dependency semantics — handled by flag re-query, not hard-coded relations.
- V4L2 buffer lifetime vs latest-only drop — size the mmap pool (≥4) so an in-flight `VideoFrame` never blocks re-queue.
