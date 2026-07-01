/*
 * @file gst_util.hpp
 * @brief GStreamer init + zero-extra-copy GstSample -> VideoFrame bridge.
 *        Shared by GstSource (network) and the V4L2 MJPEG decode branch.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_PIPELINE_GST_UTIL_HPP
#define XMCAM_PIPELINE_GST_UTIL_HPP

#include <cstdint>

#include <gst/gst.h>

#include "xmcam/core/video_frame.hpp"

namespace xmotion {

// Idempotent gst_init (thread-safe, call before any GStreamer use).
void GstEnsureInit();

// Wrap an appsink GstSample as a VideoFrame. The sample is ref'd and its buffer
// mapped READ; `out->owner` holds a deleter that unmaps + unrefs on release, so
// no pixel copy happens here. Returns false if the caps/format are unusable.
// The sample's video/x-raw format must be one of the PixelFormats we support.
bool GstSampleToVideoFrame(GstSample* sample, uint64_t seq, VideoFrame* out);

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_GST_UTIL_HPP
