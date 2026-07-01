# ADR-0001 — Architecture & Technology Stack for xmAppCamera

**Status:** Accepted · **Date:** 2026-07-01 · **Deciders:** rdu

## Context
xmAppCamera is an end-user GUI to preview and fine-tune camera sources (USB/V4L2 and RTSP/UDP) in the xmotion family. Priorities, in order: correctness of camera control introspection, then low latency / lightweight / smooth / flexible. It is an application, not a library — no external code consumes its frames programmatically.

## Decisions

### D1 — No OpenCV / no `cv::Mat` intermediate representation
Frames use a native `VideoFrame` value that wraps a `GstBuffer` map or a V4L2 mmap slot (ref-counted `owner`), not `cv::Mat`.
- **Why:** cv::Mat (and the whole OpenCV dependency) is overhead when nothing downstream needs a library-friendly matrix type; it forces extra copies/allocations that fight the low-latency/lightweight goal.
- **Consequence:** we do **not** use quickviz `CvImageWidget`/`BufferedCvImageWidget`; we use the `Texture` renderable (raw buffer + PBO) and a custom YUV-shader uploader. OpenCV stays out of the dependency set.

### D2 — Hybrid USB backend: raw V4L2 + GStreamer decode branch
Raw V4L2 ioctls for enumeration, control introspection (`VIDIOC_QUERY_EXT_CTRL` walk), and raw-format capture; MJPEG/H.264 camera modes routed through GStreamer decode.
- **Why:** `cv::VideoCapture` cannot enumerate arbitrary controls (only a fixed `CAP_PROP_*` set) — raw V4L2 is mandatory for "list whatever the camera exposes." Raw capture stays lowest-latency; the decode branch covers compressed-mode webcams.
- **Alternatives rejected:** GStreamer-everywhere (adds latency to raw USB, splits control/capture APIs); OpenCV+V4L2-controls (weak format control, highest latency).

### D3 — Network via `GstSource` with `decodebin3` auto HW/SW decode
One build auto-detects VAAPI/NVDEC and falls back to software.
- **Why:** portability across deploy targets (laptop iGPU, NVIDIA/Jetson, plain CPU) without per-target builds.
- **Consequence:** more HW paths to test; manual decoder override provided.

### D4 — `FrameUploader` seam: PBO one-copy now, GstGL zero-copy later
v1 = `PboUploader` (map + PBO upload, one CPU→GPU copy); the GL-interop zero-copy path (`GlMemUploader`) is designed behind the interface but deferred.
- **Why:** ship a portable, correct, 1080p60-capable path fast without the driver-sensitive complexity of GLFW↔GstGL context sharing; keep a clean upgrade route.

### D5 — Pipeline UX: guided builder + editable raw override
Dropdown-generated pipeline string, editable, validated with `gst_parse_launch` before Play. Presets from a data-driven `config/presets.yaml`.
- **Why:** approachable for common cases, unconstrained for power users; validation prevents opaque runtime failures.

### D6 — RTSP re-export via `FrameSink`/`RtspSink` seam, gst-rtsp-server, phase 2
Design the tee/sink seam in v1; implement in-process gst-rtsp-server encoding later.
- **Why:** it is a nice-to-have; keep v1 scope tight while guaranteeing the architecture won't need surgery to add it.

### D7 — Family conventions
C++17, `namespace xmotion` (include prefix `xmcam`), Google clang-format, GoogleTest, yaml-cpp for config, xmSigma `XLOG` logging, quickviz for windowing/rendering. Layered so `core/`+`pipeline/`+`control/`+`export/` are testable without a window.

### D8 — Family C++ deps as pinned git submodules
quickviz, xmSigma, and googletest live under `third_party/` as git submodules (pinned commits), consumed via `add_subdirectory` with a `find_package` fallback (xmotion umbrella convention). GStreamer and yaml-cpp are system packages.
- **Why:** reproducible known-good combination per the family pattern; xmAppCamera is its own repo.
- **Pinned:** quickviz `68b79c4`, xmSigma `d65e418`, googletest `973323e`.

### D9 — GStreamer required from Phase 1 (MJPEG is the camera's primary format)
The test camera's usable modes are MJPEG-only (raw YUYV is throttled to 5 fps at full resolution), so JPEG decode is needed for a usable preview — not an edge case. USB MJPEG decodes through GStreamer (`appsrc ! jpegdec ! appsink`), the same framework used for network streams, rather than adding a second decoder (libjpeg-turbo).
- **Why:** one decode framework; GStreamer is needed for network features regardless.
- **Consequence:** GStreamer is a Phase-1 dependency. A zero-decode "first light" on YUYV 640×480@30 de-risks the capture→upload path before decode lands. `gstreamer-rtsp-server-1.0` is still needed later for RTSP export (D6).

## Consequences
- OpenCV removed from the dependency graph; smaller, lighter binary.
- Two USB code paths (raw vs decode branch) to maintain.
- Frame path is single-copy in v1 with a documented zero-copy upgrade.
- A YUV→RGB shader (or `YuvTexture` renderable) is needed to avoid CPU `videoconvert`; contained behind `FrameUploader`.
