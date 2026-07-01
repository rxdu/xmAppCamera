#!/usr/bin/env bash
# Local RTP/H.264-over-UDP test stream for exercising GstSource without a
# camera or external server.
#
#   scripts/test/udp_h264_stream.sh start [port] [pattern]   # background sender
#   scripts/test/udp_h264_stream.sh stop
#   scripts/test/udp_h264_stream.sh recv-pipeline [port]      # prints the
#         matching receiver pipeline string (feed to xmcam_gst_smoke)
#
# Copyright (c) 2026 Ruixiang Du (rdu)
set -euo pipefail
PIDFILE="${TMPDIR:-/tmp}/xmcam_udp_stream.pid"
PORT="${2:-5004}"
PATTERN="${3:-ball}"

recv_pipeline() {
  echo "udpsrc port=${1} caps=application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000 ! rtpjitterbuffer latency=50 ! rtph264depay ! avdec_h264 ! videoconvert ! video/x-raw,format=RGBA ! appsink name=sink max-buffers=1 drop=true sync=false"
}

case "${1:-}" in
  start)
    gst-launch-1.0 -q videotestsrc is-live=true pattern="${PATTERN}" ! \
      video/x-raw,width=640,height=480,framerate=30/1 ! \
      x264enc tune=zerolatency bitrate=1500 ! rtph264pay config-interval=1 pt=96 ! \
      udpsink host=127.0.0.1 port="${PORT}" &
    echo $! > "${PIDFILE}"
    echo "sender started pid=$(cat "${PIDFILE}") on udp/${PORT}"
    echo "receiver pipeline:"; recv_pipeline "${PORT}"
    ;;
  stop)
    if [[ -f "${PIDFILE}" ]]; then kill "$(cat "${PIDFILE}")" 2>/dev/null || true; rm -f "${PIDFILE}"; echo stopped; fi
    ;;
  recv-pipeline)
    recv_pipeline "${PORT}"
    ;;
  *)
    echo "usage: $0 {start [port] [pattern]|stop|recv-pipeline [port]}"; exit 1 ;;
esac
