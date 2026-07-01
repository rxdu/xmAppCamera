/*
 * @file gst_jpeg_decoder.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/pipeline/gst_jpeg_decoder.hpp"

#include <cstring>

#include "xmcam/pipeline/gst_util.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

GstJpegDecoder::~GstJpegDecoder() { Close(); }

Status GstJpegDecoder::Open() {
  Close();
  GstEnsureInit();

  // appsrc is push-driven with JPEG; jpegdec -> RGBA out. sync=false: decode as
  // fast as pushed, no clock throttling. Not is-live: this is a pull-style
  // transform, not a real-time source.
  const char* desc =
      "appsrc name=src ! jpegdec ! videoconvert ! video/x-raw,format=RGBA ! "
      "appsink name=sink max-buffers=4 drop=false sync=false";
  GError* err = nullptr;
  pipeline_ = gst_parse_launch(desc, &err);
  if (!pipeline_ || err) {
    std::string msg = err ? err->message : "unknown";
    if (err) g_error_free(err);
    if (pipeline_) { gst_object_unref(pipeline_); pipeline_ = nullptr; }
    return Err(ErrorCode::kInternal, "jpeg decoder parse: " + msg);
  }
  src_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
  sink_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "sink"));
  if (!src_ || !sink_) {
    Close();
    return Err(ErrorCode::kInternal, "jpeg decoder missing src/sink");
  }
  // Declare appsrc output as JPEG so jpegdec negotiates immediately.
  GstCaps* jpeg_caps = gst_caps_new_empty_simple("image/jpeg");
  gst_app_src_set_caps(src_, jpeg_caps);
  gst_caps_unref(jpeg_caps);

  if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    Close();
    return Err(ErrorCode::kInternal, "jpeg decoder failed to start");
  }
  gst_element_get_state(pipeline_, nullptr, nullptr, GST_SECOND);  // await PLAYING
  XLOG_DEBUG("GstJpegDecoder started");
  return Ok();
}

Status GstJpegDecoder::Decode(const uint8_t* jpeg, std::size_t len,
                              int64_t pts_ns, uint64_t seq, VideoFrame* out) {
  if (!src_ || !sink_) return Err(ErrorCode::kInternal, "decoder not open");

  // Copy the JPEG into a gst buffer (the source mmap slot is requeued right
  // after this call, so we cannot alias it). Compressed frames are small.
  GstBuffer* buf = gst_buffer_new_allocate(nullptr, len, nullptr);
  gst_buffer_fill(buf, 0, jpeg, len);
  // Leave PTS unset: the raw V4L2 monotonic timestamp would confuse a fresh
  // pipeline's segment. The capture timestamp is reattached to `out` below.
  GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;

  if (gst_app_src_push_buffer(src_, buf) != GST_FLOW_OK)  // takes ownership
    return Err(ErrorCode::kDeviceError, "appsrc push failed");

  GstSample* sample = gst_app_sink_try_pull_sample(sink_, 500 * GST_MSECOND);
  if (!sample) return Err(ErrorCode::kTimeout, "jpeg decode timed out");

  bool ok = GstSampleToVideoFrame(sample, seq, out);
  if (ok && pts_ns > 0) out->pts_ns = pts_ns;  // preserve capture timestamp
  gst_sample_unref(sample);
  return ok ? Ok() : Err(ErrorCode::kInternal, "decoded sample unusable");
}

void GstJpegDecoder::Close() {
  if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
  if (src_) { gst_object_unref(src_); src_ = nullptr; }
  if (sink_) { gst_object_unref(sink_); sink_ = nullptr; }
  if (pipeline_) { gst_object_unref(pipeline_); pipeline_ = nullptr; }
}

}  // namespace xmotion
