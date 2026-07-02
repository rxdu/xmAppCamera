/*
 * @file gst_util.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/pipeline/gst_util.hpp"

#include <gst/video/video.h>

#include <mutex>

#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

void GstEnsureInit() {
  static std::once_flag once;
  std::call_once(once, [] {
    gst_init(nullptr, nullptr);
    XLOG_DEBUG("GStreamer initialized ({})", gst_version_string());
  });
}

namespace {
// Keeps a mapped GstSample alive; released with the last VideoFrame referencing
// it. Order matters: unmap before unref.
struct GstFrameLease {
  GstSample* sample = nullptr;
  GstBuffer* buffer = nullptr;
  GstMapInfo map{};
  ~GstFrameLease() {
    if (buffer) gst_buffer_unmap(buffer, &map);
    if (sample) gst_sample_unref(sample);
  }
};
}  // namespace

bool GstSampleToVideoFrame(GstSample* sample, uint64_t seq, VideoFrame* out) {
  GstCaps* caps = gst_sample_get_caps(sample);
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  if (!caps || !buffer) return false;

  GstVideoInfo info;
  if (!gst_video_info_from_caps(&info, caps)) {
    XLOG_WARN("GstSampleToVideoFrame: caps not video/x-raw");
    return false;
  }

  PixelFormat pf = PixelFormat::kUnknown;
  switch (GST_VIDEO_INFO_FORMAT(&info)) {
    case GST_VIDEO_FORMAT_RGBA: pf = PixelFormat::kRgba8; break;
    case GST_VIDEO_FORMAT_RGB:  pf = PixelFormat::kRgb8;  break;
    case GST_VIDEO_FORMAT_BGRA: pf = PixelFormat::kBgra8; break;
    case GST_VIDEO_FORMAT_BGR:  pf = PixelFormat::kBgr8;  break;
    case GST_VIDEO_FORMAT_GRAY8: pf = PixelFormat::kGray8; break;
    case GST_VIDEO_FORMAT_NV12: pf = PixelFormat::kNv12;  break;
    case GST_VIDEO_FORMAT_I420: pf = PixelFormat::kI420;  break;
    case GST_VIDEO_FORMAT_YUY2: pf = PixelFormat::kYuyv;  break;
    default:
      XLOG_WARN("GstSampleToVideoFrame: unsupported format {}",
                gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info)));
      return false;
  }

  auto lease = std::make_shared<GstFrameLease>();
  lease->sample = gst_sample_ref(sample);
  lease->buffer = buffer;
  if (!gst_buffer_map(buffer, &lease->map, GST_MAP_READ)) {
    lease->buffer = nullptr;  // avoid unmap in dtor
    return false;
  }

  out->width = GST_VIDEO_INFO_WIDTH(&info);
  out->height = GST_VIDEO_INFO_HEIGHT(&info);
  out->format = pf;
  const uint8_t* base = lease->map.data;
  const int nplanes = GST_VIDEO_INFO_N_PLANES(&info);
  out->data = base + GST_VIDEO_INFO_PLANE_OFFSET(&info, 0);
  out->stride = static_cast<int>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 0));
  if (nplanes > 1) {
    out->plane1 = base + GST_VIDEO_INFO_PLANE_OFFSET(&info, 1);
    out->stride1 = static_cast<int>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 1));
  }
  if (nplanes > 2) {
    out->plane2 = base + GST_VIDEO_INFO_PLANE_OFFSET(&info, 2);
    out->stride2 = static_cast<int>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 2));
  }
  const GstClockTime pts = GST_BUFFER_PTS(buffer);
  out->pts_ns = GST_CLOCK_TIME_IS_VALID(pts) ? static_cast<int64_t>(pts) : 0;
  out->seq = seq;
  out->owner = lease;
  return true;
}

}  // namespace xmotion
