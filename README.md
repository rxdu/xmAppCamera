# xmAppCamera

A lightweight, low-latency desktop GUI to **preview and fine-tune camera sources** in the xmotion family:

- **USB / V4L2 cameras** — reliable device enumeration, full control introspection (every control the camera exposes), live tuning, and export to a re-loadable config file.
- **RTSP / UDP streams** — compose GStreamer pipelines (guided builder + raw override) to decode network video.
- **RTSP re-export** *(nice-to-have)* — publish any active source as an RTSP stream.

Built on **quickviz** (windowing, ImGui docking, GL texture streaming) and **xmSigma** (logging). Frames stay native (no OpenCV / `cv::Mat`) for raw performance and a small footprint.

## Status
Backend implemented and hardware-verified; GUI implemented and CI-green (build + tests). Ready for manual testing.

## Build & run

```bash
# one-time deps (Ubuntu)
sudo apt install -y build-essential cmake pkg-config \
  libgl1-mesa-dev libglu1-mesa-dev libglfw3-dev libglm-dev libcairo2-dev \
  libfontconfig1-dev libeigen3-dev libspdlog-dev libfmt-dev libyaml-cpp-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav

git clone --recursive git@github.com:rxdu/xmAppCamera.git
cd xmAppCamera
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure   # unit tests (camera/network ones skip if absent)

./build/bin/xmAppCamera                        # the GUI
```

Headless smoke tools (no GUI): `build/bin/xmcam_v4l2_smoke [dev] [yuyv|mjpeg] [WxH] [fps] [n]`,
`build/bin/xmcam_gst_smoke "<pipeline>" [n]`. Local RTP/UDP test stream: `scripts/test/udp_h264_stream.sh start`.

## Design docs

- **[docs/DESIGN.md](docs/DESIGN.md)** — architecture, frame path, threading, modules, phases.
- **[docs/adr/0001-architecture-and-stack.md](docs/adr/0001-architecture-and-stack.md)** — the key decisions and why.
- **[docs/design/config-schema.md](docs/design/config-schema.md)** — exported config format.
- **[TODO.md](TODO.md)** — phased work breakdown.

## Dependencies (planned)
quickviz · xmSigma · GStreamer 1.x (`gstreamer-1.0 app video`, + `rtsp-server` in phase 2) · libv4l2 / linux uapi · yaml-cpp. **No OpenCV.** C++17.
