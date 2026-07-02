/*
 * @file rtsp_sink.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/export/rtsp_sink.hpp"

#include <gst/app/gstappsrc.h>

#include <cstring>

#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

// Encoder backpressure: appsrc raises enough-data when its queue passes
// max-bytes and need-data when it drains. We drop frames instead of blocking
// the capture thread (FrameSink contract).
void NeedDataCb(GstElement* /*src*/, guint /*len*/, gpointer user) {
  static_cast<RtspSink*>(user)->SetFeed(true);
}
void EnoughDataCb(GstElement* /*src*/, gpointer user) {
  static_cast<RtspSink*>(user)->SetFeed(false);
}

// media-configure: grab the named appsrc and register teardown handling.
void MediaUnpreparedCb(GstRTSPMedia* /*media*/, gpointer user) {
  auto* self = static_cast<RtspSink*>(user);
  self->OnMediaConfigure(nullptr);  // clear appsrc on client disconnect
}

void MediaConfigureCb(GstRTSPMediaFactory* /*f*/, GstRTSPMedia* media,
                      gpointer user) {
  auto* self = static_cast<RtspSink*>(user);
  GstElement* element = gst_rtsp_media_get_element(media);
  GstElement* appsrc =
      gst_bin_get_by_name_recurse_up(GST_BIN(element), "xmsrc");
  self->OnMediaConfigure(appsrc);  // takes ownership of the appsrc ref
  g_signal_connect(media, "unprepared", G_CALLBACK(MediaUnpreparedCb), user);
  gst_object_unref(element);
}

}  // namespace

RtspSink::~RtspSink() { Stop(); }

Status RtspSink::Start(int port, const std::string& mount, int bitrate_kbps) {
  if (running_.load()) return Ok();
  if (!gst_is_initialized()) gst_init(nullptr, nullptr);
  bitrate_kbps_ = bitrate_kbps;
  url_ = "rtsp://127.0.0.1:" + std::to_string(port) + mount;
  service_ = std::to_string(port);
  mount_ = mount;
  running_.store(true);
  thread_ = std::thread(&RtspSink::Loop, this);
  XLOG_INFO("RtspSink serving {}", url_);
  return Ok();
}

void RtspSink::Loop() {
  GMainContext* ctx = g_main_context_new();
  g_main_context_push_thread_default(ctx);
  loop_ = g_main_loop_new(ctx, FALSE);

  server_ = gst_rtsp_server_new();
  gst_rtsp_server_set_service(server_, service_.c_str());

  GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
  GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
  // block=false + max-bytes(~4 frames of 720p I420): OnFrame drops when the
  // encoder saturates (need/enough-data), never blocking the capture thread.
  gchar* launch = g_strdup_printf(
      "( appsrc name=xmsrc is-live=true block=false max-bytes=6000000 "
      "format=time do-timestamp=true ! videoconvert ! "
      "x264enc tune=zerolatency speed-preset=ultrafast bitrate=%d ! "
      "rtph264pay name=pay0 pt=96 config-interval=1 )",
      bitrate_kbps_);
  gst_rtsp_media_factory_set_launch(factory, launch);
  g_free(launch);
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  g_signal_connect(factory, "media-configure", G_CALLBACK(MediaConfigureCb),
                   this);
  gst_rtsp_mount_points_add_factory(mounts, mount_.c_str(), factory);
  g_object_unref(mounts);

  if (gst_rtsp_server_attach(server_, ctx) == 0) {
    XLOG_ERROR("RtspSink: failed to attach server (port in use?)");
  } else {
    g_main_loop_run(loop_);
  }

  // Teardown (after Stop quits the loop).
  {
    std::lock_guard<std::mutex> lk(src_mtx_);
    if (appsrc_) { gst_object_unref(appsrc_); appsrc_ = nullptr; }
  }
  if (server_) { g_object_unref(server_); server_ = nullptr; }
  g_main_loop_unref(loop_);
  loop_ = nullptr;
  g_main_context_pop_thread_default(ctx);
  g_main_context_unref(ctx);
}

void RtspSink::Stop() {
  if (!running_.exchange(false)) return;
  if (loop_) g_main_loop_quit(loop_);
  if (thread_.joinable()) thread_.join();
  XLOG_INFO("RtspSink stopped ({} buffers pushed)", pushed_);
  pushed_ = 0;
}

void RtspSink::OnMediaConfigure(GstElement* appsrc) {
  std::lock_guard<std::mutex> lk(src_mtx_);
  if (appsrc_) gst_object_unref(appsrc_);
  appsrc_ = appsrc;  // may be nullptr (teardown); ownership transferred
  caps_w_ = caps_h_ = 0;  // force caps refresh on next frame
  feed_.store(true);
  if (appsrc_) {
    g_signal_connect(appsrc_, "need-data", G_CALLBACK(NeedDataCb), this);
    g_signal_connect(appsrc_, "enough-data", G_CALLBACK(EnoughDataCb), this);
  }
}

void RtspSink::OnFrame(const VideoFrame& f) {
  if (f.format != PixelFormat::kI420) return;  // encoder expects I420
  if (!feed_.load()) return;  // encoder saturated: drop, never block capture
  std::lock_guard<std::mutex> lk(src_mtx_);
  if (!appsrc_) return;  // no client connected

  if (f.width != caps_w_ || f.height != caps_h_) {
    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw", "format", G_TYPE_STRING, "I420", "width", G_TYPE_INT,
        f.width, "height", G_TYPE_INT, f.height, "framerate",
        GST_TYPE_FRACTION, 30, 1, nullptr);
    gst_app_src_set_caps(GST_APP_SRC(appsrc_), caps);
    gst_caps_unref(caps);
    caps_w_ = f.width;
    caps_h_ = f.height;
  }

  // Pack tightly into an I420 buffer (rows may be strided in the source).
  const int cw = (f.width + 1) / 2;
  const int ch = (f.height + 1) / 2;
  const gsize size = static_cast<gsize>(f.width) * f.height + 2 * cw * ch;
  GstBuffer* buf = gst_buffer_new_allocate(nullptr, size, nullptr);
  GstMapInfo map;
  gst_buffer_map(buf, &map, GST_MAP_WRITE);
  uint8_t* dst = map.data;
  for (int y = 0; y < f.height; ++y)
    std::memcpy(dst + y * f.width, f.data + y * f.stride, f.width);
  dst += static_cast<gsize>(f.width) * f.height;
  for (int y = 0; y < ch; ++y)
    std::memcpy(dst + y * cw, f.plane1 + y * f.stride1, cw);
  dst += static_cast<gsize>(cw) * ch;
  for (int y = 0; y < ch; ++y)
    std::memcpy(dst + y * cw, f.plane2 + y * f.stride2, cw);
  gst_buffer_unmap(buf, &map);

  if (gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buf) == GST_FLOW_OK)
    ++pushed_;
}

}  // namespace xmotion
