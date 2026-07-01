# xmAppCamera

A lightweight, low-latency desktop GUI to **preview and fine-tune camera sources** in the xmotion family:

- **USB / V4L2 cameras** — reliable device enumeration, full control introspection (every control the camera exposes), live tuning, and export to a re-loadable config file.
- **RTSP / UDP streams** — compose GStreamer pipelines (guided builder + raw override) to decode network video.
- **RTSP re-export** *(nice-to-have)* — publish any active source as an RTSP stream.

Built on **quickviz** (windowing, ImGui docking, GL texture streaming) and **xmSigma** (logging). Frames stay native (no OpenCV / `cv::Mat`) for raw performance and a small footprint.

## Status
Pre-implementation. The design is complete — start here:

- **[docs/DESIGN.md](docs/DESIGN.md)** — architecture, frame path, threading, modules, phases.
- **[docs/adr/0001-architecture-and-stack.md](docs/adr/0001-architecture-and-stack.md)** — the key decisions and why.
- **[docs/design/config-schema.md](docs/design/config-schema.md)** — exported config format.
- **[TODO.md](TODO.md)** — phased work breakdown.

## Dependencies (planned)
quickviz · xmSigma · GStreamer 1.x (`gstreamer-1.0 app video`, + `rtsp-server` in phase 2) · libv4l2 / linux uapi · yaml-cpp. **No OpenCV.** C++17.
