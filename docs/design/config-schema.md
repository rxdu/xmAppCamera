# Config Schema (exported camera config)

The file `ConfigWriter` produces / `ConfigReader` consumes. YAML (family standard). Designed to be stable and human-readable so other xmotion apps can consume an exported camera config. Version the top-level `schema_version` for forward compatibility.

## Example — USB / V4L2 source

```yaml
schema_version: 1
source:
  type: v4l2                       # v4l2 | gstreamer
  device: /dev/v4l/by-id/usb-Foo_Camera_1234-video-index0   # stable id preferred
  device_fallback: /dev/video0     # last-seen node, used only if by-id missing
  card: "Foo Camera"               # from VIDIOC_QUERYCAP, for human matching
format:
  fourcc: YUYV                     # negotiated pixel format
  width: 1280
  height: 720
  fps: 30
controls:                          # name -> value (id kept for exact re-apply)
  - { name: brightness,        id: 0x00980900, value: 128 }
  - { name: contrast,          id: 0x00980901, value: 32 }
  - { name: exposure_auto,     id: 0x009a0901, value: 1 }     # 1 = Manual Mode
  - { name: exposure_absolute, id: 0x009a0902, value: 250 }
  - { name: white_balance_automatic, id: 0x0098090c, value: false }
```

## Example — network / GStreamer source

```yaml
schema_version: 1
source:
  type: gstreamer
  uri: rtsp://192.168.1.50:554/stream1
  # The exact pipeline is exported so downstream apps reproduce decode behaviour.
  pipeline: >
    rtspsrc location=rtsp://192.168.1.50:554/stream1 latency=50 !
    rtph264depay ! decodebin3 ! appsink name=sink max-buffers=1 drop=true
  decode: auto                     # auto | vaapi | nvdec | software
format:                            # observed after negotiation (informational)
  width: 1920
  height: 1080
  fps: 25
```

## Rules
- `controls` uses control **name** as the primary key (portable) with numeric **id** for exact re-apply on the same driver; on import, match by id first, fall back to name.
- Applying a config re-queries control flags after each write to respect auto/manual dependencies (see DESIGN §5.3).
- Unknown keys are ignored on read (forward-compatible); `schema_version` mismatch triggers a logged warning, not a hard failure.
- Values reflect V4L2 types: integers as numbers, booleans as `true/false`, menu controls as their integer index (comment the label for readability).
