# xmAppCamera — TODO

Tracks WHAT, not HOW. Status: `[ ]` todo · `[~]` in progress · `[x]` done. See `docs/DESIGN.md` §11 for phase milestones.

**Status (2026-07-02):** Phases 0–5 implemented; CI green. Hardware-verified: YUYV raw capture, MJPEG→I420 decode, 20-control introspection + INACTIVE-flag dependency, live RTP/H264-over-UDP, GUI live preview @~74fps with GPU YUV→RGB (upload ~0.25ms). Phase 5 RTSP re-export verified E2E (independent client consumed the re-exported camera stream). Remaining (optional): GstGL zero-copy uploader.

## Phase 0 — Skeleton
- [x] Git repo + submodules: quickviz `68b79c4`, xmSigma `d65e418`, googletest `973323e` under `third_party/`
- [x] CMake project: C++17, `namespace xmotion`, `add_subdirectory` submodules (find_package fallback), find GStreamer / yaml-cpp; assert OpenCV is NOT linked
- [x] `.clang-format` (Google, SortIncludes: Never), GoogleTest wiring via submodule
- [x] `core/` value types: `PixelFormat`, `VideoFrame`, `SourceDescriptor`, `SourceCaps`, `SourceStats`, `Result` (+ unit tests)
- [x] App shell: `Viewer` + docking (MainDockingPanel), panels, XLOG wired

## Phase 1 — USB preview
- [x] `VideoSource` interface + `DataStream<VideoFrame>` handoff
- [x] `V4l2Source`: device scan (`/dev/video*`, by-id), `QUERYCAP`, format enum (`ENUM_FMT/FRAMESIZES/FRAMEINTERVALS`) → `SourceCaps`
- [x] `V4l2Source`: mmap streaming capture for raw formats; `VideoFrame` owner re-queues buffer
- [x] `V4l2Source`: MJPEG decode branch via GStreamer (`appsrc ! jpegdec ! appsink`) — camera's primary format
- [x] Frame upload path (FrameTexture; GPU YuvConverter for planar); PreviewPanel draws texture
- [x] DevicePanel: list devices + formats/resolutions/fps, select & start/stop

## Phase 2 — USB tuning (G1 complete)
- [x] `ControlIntrospector`: `QUERY_EXT_CTRL` + `NEXT_CTRL` walk → `ControlInfo` model
- [x] `ControlSet`: typed get/set via `G/S_EXT_CTRLS`; flag re-query after write
- [x] ControlPanel: typed widgets (slider/checkbox/combo/button); flag-driven enable/disable; dependency re-read after write
- [x] `ConfigWriter`/`ConfigReader` YAML + `docs/design/config-schema.md`; round-trip test
- [ ] ExportPanel: save/load current camera config from the GUI

## Phase 3 — Network streams (G2 complete)
- [x] `GstSource`: appsink pipeline, `GstBuffer`→`VideoFrame` map/wrap, latest-only (max-buffers=1 drop)
- [x] `decodebin3` in presets (auto HW/SW decode); manual override via raw pipeline edit
- [x] PipelinePanel: presets + editable raw override, `gst_parse_launch` validate, Play/Stop
- [ ] `config/presets.yaml` data-driven catalog + loader (presets currently hardcoded)
- [x] Compressed-USB decode branch in `V4l2Source` (MJPEG → gst decode)
- [ ] Network resilience: EOS/error reconnect backoff, bus messages → LogPanel

## Phase 4 — Perf hardening (NFR)
- [x] GPU YUV→RGB (YuvConverter FBO shader; decoders emit native I420, no CPU videoconvert)
- [ ] `GlMemUploader` GstGL zero-copy seam (gated on runtime capability probe)
- [x] StatsPanel: capture/display fps, captured/shown/dropped, decode/upload ms, latency estimate, active decoder
- [ ] Glass-to-glass latency measurement pass (documented numbers in perf-notes)

## Phase 5 — RTSP re-export (nice-to-have, G3)
- [x] Install `gstreamer-rtsp-server-1.0` dev
- [x] `FrameSink` tee wired into `VideoSource` (EmitFrame; non-blocking contract)
- [x] `RtspSink`: appsrc (backpressure-dropping) → x264enc → rtph264pay → in-process gst-rtsp-server mount; GUI start/stop + URL
- [x] E2E verify with a GStreamer client (150 H.264 frames consumed from rtsp://127.0.0.1:8554/cam while preview held ~66fps)

## Phase 6 — Production qualification (checklist integration)
- [x] Hot-plug auto-recovery in `V4l2Source` (ENODEV → re-open via by-id, backoff, generation counter; controls rebuilt via epoch)
- [x] `qualify/` backend: FrameTap, control-lock/AWB checks, timestamp stability, platform+firmware identity (sysfs), image fingerprint, YAML+MD report writers (14 tests)
- [x] QualifyPanel: automated run, power-cycle identity + disconnect-recovery operator checks, vendor/procurement record fields, report export; XMCAM_AUTOQUALIFY hook
- [x] Verified on camera: platform/enumeration/exposure-lock/gain-lock/AWB-disable/timestamps all PASS (fw 32e4:0234 bcdDevice 0237; 11.34ms ±1.5ms intervals)
- [ ] Operator-in-the-loop checks exercised with a physical unplug (user)
