# Multi-Camera Simultaneous Streaming — Design

**Status:** Draft on `feature/multi-camera` · 2026-07-02

Support N USB cameras (and network streams) streaming at once: independent capture/decode per camera, a tiled preview, per-camera controls/qualification, and per-camera state in the UI.

## 1. What already scales vs. what doesn't

**Already per-source (no change needed):**
- `V4l2Source`/`GstSource` each own their thread, `DataStream<VideoFrame>`, decoder, buffer pool, hot-plug recovery (by-path — which is exactly why the by-path work matters here: identical-serial cameras recover to the right port).
- `FrameSink` tee, stats counters, `hw_seq` drop detection — all per-source.

**Single-source assumptions to break (the real work):**

| Component | Today | Multi-camera |
|---|---|---|
| `AppController` | one `source_`, `controls_`, `active_config_`, `display_stats_`, `attached_sink_` | `CameraSession` list + a *selected* session |
| `PreviewPanel` | one texture, pulls "the" stream | grid of tiles, one texture/converter per session |
| `DevicePanel` | one combo set + one button row | per-camera blocks, each with its own state machine |
| `ControlPanel`/`QualifyPanel` | operate on "the" camera | operate on the *selected* session |
| RTSP export | one sink on "the" source | per-session export (distinct mounts/ports) |

## 2. Backend: `CameraSession`

```cpp
struct CameraSession {
  int id;                          // stable per app run
  SourceKind kind;                 // v4l2 | gst
  std::unique_ptr<VideoSource> source;
  std::shared_ptr<V4l2Device> ctrl_dev;   // v4l2 only
  std::unique_ptr<ControlSet> controls;    // v4l2 only
  ActiveV4l2Config config;         // or pipeline string for gst
  DisplayStats display_stats;
  int controls_epoch;
  uint32_t last_generation;        // hot-plug recovery tracking
  FrameSink* attached_sink;        // qual tap / rtsp, one per session
};
```

`AppController` becomes a session manager: `StartV4l2(...)` / `StartGst(...)` **add** a session keyed by device path (starting an already-running device = Apply/restart of that session); `StopSession(id)`; `sessions()` for iteration; `selected_id()` + `Select(id)` for the panels that operate on one camera. Recovery maintenance loops over all sessions. The existing single-source getters become thin wrappers over the selected session so panels migrate incrementally.

**Resource budget (why this is safe):** each MJPEG 1080p30 stream costs ~1 decode thread-slice (~4 ms/frame measured) + ~35 Mbps USB. Two–four cameras are comfortably within a 12-core CPU; the binding constraint is **USB bandwidth per host controller** — two 1080p MJPEG streams on one USB2 hub fit (~70 of 480 Mbps), but raw YUYV won't. The qualification USB-link audit already reports link speed + hub sharing per camera; failure mode (kernel drops under starvation) is visible per-tile via `hw_seq` gap counters.

## 3. Frontend

### Preview: tiled grid in the existing panel
One Preview panel renders all live sessions in an auto-layout grid (1 → 1×1, 2 → 2×1, 3–4 → 2×2, …). Each tile: aspect-fit video, header overlay `card (videoN)` + fps, thin highlight border on the selected tile. **Click a tile to select** that camera (drives Controls/Qualify targeting); **double-click to solo** (tile fills the panel; double-click again to return to grid). Per-session `FrameTexture`+`YuvConverter` owned by the panel, keyed by session id, destroyed when the session stops.

### Device tab: one block per camera
Replace the single combo set with a collapsing block per enumerated device (RealSense-viewer pattern). Each block contains its own Format/Size/FPS combos, its own stateful `Start`/`Apply`/`Stop` buttons, and its own status/stats lines. The selected camera's block is visually marked; clicking a block selects it. Multiple blocks can be LIVE at once.

### Controls & Qualify tabs: target the selection
A header line shows which camera they're bound to (`Controls — Arducam (video2)`), following the global selection; a combo lets you override without touching the preview. Qualification runs against one camera at a time (its serial-collision check already looks across *all* attached cameras).

### Network streams
A GStreamer session is just another session: it gets a tile in the grid and its stats block in the Network panel. Start/stop is independent of the cameras.

### RTSP export
Per-session: exporting camera *i* serves `rtsp://host:<8554+i>/cam`. One export per session, multiple sessions exportable simultaneously (each `RtspSink` is already self-contained with its own server). The Network panel's export UI moves into a per-session context (export the *selected* session).

## 4. Verification plan
- **Headless multi-session** without second camera: two `GstSource` sessions (videotestsrc pipelines) + one V4L2 session → grid of 3 tiles, per-tile fps overlays, selection switching (screenshot-verified); AppController session tests (add/stop/select bookkeeping) with videotestsrc — CI-runnable.
- **Dual-hardware** when the Arducam is attached: both cameras LIVE at 1080p MJPEG, per-tile stats, controls bound per selection, unplug ONE camera → only its session recovers (the other keeps streaming) — the by-path + card-verify recovery test that matters for robots.
- Bandwidth sanity: both cameras on one hub, watch `hw_seq` gaps per tile.

## 5. Implementation blocks (on this branch)
1. `AppController` → session manager (list, selected id, per-session recovery/epochs); single-selection wrappers kept.
2. `PreviewPanel` → tile grid + per-session upload state + click-select/double-click-solo.
3. `DevicePanel` → per-device blocks with independent state machines.
4. `ControlPanel`/`QualifyPanel` → bind to selection with header + override combo.
5. RTSP export per session (port base + id).
6. Tests (gst-based session logic) + headless screenshots + dual-hardware pass.
