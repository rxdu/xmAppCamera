# xmAppCamera — TODO

Tracks WHAT, not HOW. Status: `[ ]` todo · `[~]` in progress · `[x]` done. See `docs/DESIGN.md` §11 for phase milestones.

**Status (2026-07-02):** Phases 0–3 backend + GUI implemented; CI green (build + 22 tests). Hardware-verified on the Global Shutter Camera: YUYV raw capture, MJPEG→RGBA decode @~60fps, 20-control introspection + INACTIVE-flag dependency, live RTP/H264-over-UDP. GUI compiles in CI; **awaiting manual/visual testing** (layout, live preview, control tuning). Remaining: Phase 4 perf (GPU YUV shader, GlMem zero-copy, full StatsPanel) and Phase 5 RTSP export.

## Phase 0 — Skeleton
- [x] Git repo + submodules: quickviz `68b79c4`, xmSigma `d65e418`, googletest `973323e` under `third_party/`
- [x] CMake project: C++17, `namespace xmotion`, `add_subdirectory` submodules (find_package fallback), find GStreamer / yaml-cpp; assert OpenCV is NOT linked
- [x] `.clang-format` (Google, SortIncludes: Never), GoogleTest wiring via submodule
- [x] `core/` value types: `PixelFormat`, `VideoFrame`, `SourceDescriptor`, `SourceCaps`, `SourceStats`, `Result` (+ unit tests)
- [~] App shell: `Viewer` + docking, empty DevicePanel + PreviewPanel + Stats/LogPanel, XLOG wired

## Phase 1 — USB preview
- [ ] `VideoSource` interface + `DataStream<VideoFrame>` handoff
- [ ] `V4l2Source`: device scan (`/dev/video*`, by-id), `QUERYCAP`, format enum (`ENUM_FMT/FRAMESIZES/FRAMEINTERVALS`) → `SourceCaps`
- [ ] `V4l2Source`: mmap streaming capture for raw formats; `VideoFrame` owner re-queues buffer
- [ ] `V4l2Source`: MJPEG decode branch via GStreamer (`appsrc ! jpegdec ! appsink`) — camera's primary format
- [ ] `FrameUploader` interface + `PboUploader` (packed RGB/BGR path); PreviewPanel draws texture
- [ ] DevicePanel: list devices + formats/resolutions/fps, select & start/stop

## Phase 2 — USB tuning (G1 complete)
- [ ] `ControlIntrospector`: `QUERY_EXT_CTRL` + `NEXT_CTRL` walk → `ControlInfo` model
- [ ] `ControlSet`: typed get/set via `G/S_EXT_CTRLS`; UI→capture-thread command queue
- [ ] ControlPanel: typed widgets (slider/checkbox/combo/button); flag-driven enable/disable; dependency re-query; volatile polling
- [ ] `ConfigWriter`/`ConfigReader` YAML + `docs/design/config-schema.md`; ExportPanel; round-trip test

## Phase 3 — Network streams (G2 complete)
- [ ] `GstSource`: appsink pipeline, `GstBuffer`→`VideoFrame` map/wrap, latest-only (max-buffers=1 drop)
- [ ] `decodebin3` auto HW/SW decode + manual override
- [ ] PipelinePanel: guided builder → pipeline string, editable raw override, `gst_parse_launch` validate, Play/Stop
- [ ] `config/presets.yaml` catalog + loader
- [ ] Compressed-USB decode branch in `V4l2Source` (MJPEG/H.264 → gst decode)
- [ ] Network resilience: EOS/error reconnect backoff, bus messages → LogPanel

## Phase 4 — Perf hardening (NFR)
- [ ] YUV→RGB shader upload (avoid CPU videoconvert) behind `FrameUploader`
- [ ] `GlMemUploader` GstGL zero-copy seam (gated on runtime capability probe)
- [ ] StatsPanel: capture/display fps, drops, latency estimate, active decoder
- [ ] Glass-to-glass latency measurement pass

## Phase 5 — RTSP re-export (nice-to-have, G3)
- [ ] Install `gstreamer-rtsp-server-1.0` dev (not yet present)
- [ ] `FrameSink` tee wired into `VideoSource`
- [ ] `RtspSink`: appsrc → auto encoder → rtph264pay → in-process gst-rtsp-server mount
- [ ] Manual verify with ffplay/vlc
