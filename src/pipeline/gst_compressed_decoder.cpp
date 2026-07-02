/*
 * @file gst_compressed_decoder.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/pipeline/gst_compressed_decoder.hpp"

#include <cstring>

#include "xmcam/pipeline/gst_util.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

GstCompressedDecoder::~GstCompressedDecoder() { Close(); }

const char* GstCompressedDecoder::name() const {
  switch (format_) {
    case PixelFormat::kMjpeg: return "mjpeg->i420";
    case PixelFormat::kH264: return "h264->i420";
    default: return "?";
  }
}

Status GstCompressedDecoder::Open(PixelFormat format) {
  Close();
  if (!Supports(format))
    return Err(ErrorCode::kUnsupported, "no decoder for this format");
  GstEnsureInit();
  format_ = format;

  // sync=false: decode as fast as pushed, no clock throttling. Not is-live:
  // this is a pull-style transform, not a real-time source. jpegdec/avdec
  // emit I420 natively, so videoconvert is a passthrough; the GPU does
  // YUV->RGB for display.
  const char* desc =
      format == PixelFormat::kMjpeg
          ? "appsrc name=src ! jpegdec ! videoconvert ! "
            "video/x-raw,format=I420 ! "
            "appsink name=sink max-buffers=4 drop=false sync=false"
          // max-threads=1: ffmpeg frame-threading delays output by ~thread-
          // count frames (e.g. 12 frames = 400ms at 30fps) — poison for a
          // live preview. Single-threaded H.264 decode is far faster than
          // camera rates at typical resolutions.
          : "appsrc name=src ! h264parse ! avdec_h264 max-threads=1 ! "
            "videoconvert ! video/x-raw,format=I420 ! "
            "appsink name=sink max-buffers=4 drop=false sync=false";

  GError* err = nullptr;
  pipeline_ = gst_parse_launch(desc, &err);
  if (!pipeline_ || err) {
    std::string msg = err ? err->message : "unknown";
    if (err) g_error_free(err);
    if (pipeline_) { gst_object_unref(pipeline_); pipeline_ = nullptr; }
    return Err(ErrorCode::kInternal, "decoder parse: " + msg);
  }
  src_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
  sink_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "sink"));
  if (!src_ || !sink_) {
    Close();
    return Err(ErrorCode::kInternal, "decoder missing src/sink");
  }
  // Declare appsrc output caps so the parser/decoder negotiate immediately.
  // UVC H.264 delivers Annex-B byte-stream access units per V4L2 buffer.
  GstCaps* caps =
      format == PixelFormat::kMjpeg
          ? gst_caps_new_empty_simple("image/jpeg")
          : gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING,
                                "byte-stream", "alignment", G_TYPE_STRING,
                                "au", nullptr);
  gst_app_src_set_caps(src_, caps);
  gst_caps_unref(caps);

  if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    Close();
    return Err(ErrorCode::kInternal, "decoder failed to start");
  }
  gst_element_get_state(pipeline_, nullptr, nullptr, GST_SECOND);
  XLOG_DEBUG("GstCompressedDecoder started ({})", name());
  return Ok();
}

Status GstCompressedDecoder::Decode(const uint8_t* data, std::size_t len,
                                    int64_t pts_ns, uint64_t seq,
                                    VideoFrame* out) {
  if (!src_ || !sink_) return Err(ErrorCode::kInternal, "decoder not open");

  // Copy the compressed bytes into a gst buffer (the source mmap slot is
  // requeued right after this call, so we cannot alias it).
  GstBuffer* buf = gst_buffer_new_allocate(nullptr, len, nullptr);
  gst_buffer_fill(buf, 0, data, len);
  // Leave PTS unset: the raw V4L2 monotonic timestamp would confuse a fresh
  // pipeline's segment. The capture timestamp is reattached to `out` below.
  GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;

  if (gst_app_src_push_buffer(src_, buf) != GST_FLOW_OK)  // takes ownership
    return Err(ErrorCode::kDeviceError, "appsrc push failed");

  GstSample* sample = gst_app_sink_try_pull_sample(sink_, 500 * GST_MSECOND);
  if (!sample)
    return Err(ErrorCode::kTimeout, "no decoded frame yet");

  bool ok = GstSampleToVideoFrame(sample, seq, out);
  if (ok && pts_ns > 0) out->pts_ns = pts_ns;  // preserve capture timestamp
  gst_sample_unref(sample);
  return ok ? Ok() : Err(ErrorCode::kInternal, "decoded sample unusable");
}

void GstCompressedDecoder::Close() {
  if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
  if (src_) { gst_object_unref(src_); src_ = nullptr; }
  if (sink_) { gst_object_unref(sink_); sink_ = nullptr; }
  if (pipeline_) { gst_object_unref(pipeline_); pipeline_ = nullptr; }
  format_ = PixelFormat::kUnknown;
}

}  // namespace xmotion
