/*
 * @file gst_source.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/pipeline/gst_source.hpp"

#include <chrono>
#include <thread>

#include "xmcam/core/util/scope_timer.hpp"
#include "xmcam/pipeline/gst_util.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

Status GstSource::Validate(const std::string& pipeline) {
  GstEnsureInit();
  GError* err = nullptr;
  GstElement* p = gst_parse_launch(pipeline.c_str(), &err);
  if (!p || err) {
    std::string msg = err ? err->message : "unknown parse error";
    if (err) g_error_free(err);
    if (p) gst_object_unref(p);
    return Err(ErrorCode::kInvalidArgument, "pipeline parse: " + msg);
  }
  gst_object_unref(p);
  return Ok();
}

std::string GstSource::DefaultPipelineForUri(const std::string& uri) {
  // Codec-agnostic: decodebin3 plugs the right depayloader/parser/decoder for
  // whatever the stream carries (H.264, H.265, MJPEG, ...) — hardcoding
  // rtph264depay silently produces nothing on an H.265 camera.
  // I420 out: decoders emit it natively so videoconvert is passthrough; the
  // GPU converter handles YUV->RGB for display.
  const std::string tail =
      " ! decodebin3 ! videoconvert ! video/x-raw,format=I420 ! "
      "appsink name=sink max-buffers=1 drop=true sync=false";
  if (uri.rfind("rtsp://", 0) == 0)
    return "rtspsrc location=" + uri + " latency=50" + tail;
  return "uridecodebin uri=" + uri + tail;
}

GstSource::~GstSource() { Close(); }

Status GstSource::Open(const SourceDescriptor& desc) {
  Close();
  GstEnsureInit();

  pipeline_desc_ = !desc.pipeline.empty() ? desc.pipeline
                                          : DefaultPipelineForUri(desc.uri);
  if (pipeline_desc_.empty())
    return Err(ErrorCode::kInvalidArgument, "no pipeline or uri");

  GError* err = nullptr;
  pipeline_ = gst_parse_launch(pipeline_desc_.c_str(), &err);
  if (!pipeline_ || err) {
    std::string msg = err ? err->message : "unknown";
    if (err) g_error_free(err);
    if (pipeline_) {
      gst_object_unref(pipeline_);
      pipeline_ = nullptr;
    }
    return Err(ErrorCode::kInvalidArgument, "pipeline parse: " + msg);
  }

  GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
  if (!sink) {
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    return Err(ErrorCode::kInvalidArgument,
               "pipeline has no element named 'sink'");
  }
  appsink_ = GST_APP_SINK(sink);  // takes the ref returned by get_by_name
  bus_ = gst_element_get_bus(pipeline_);
  XLOG_INFO("GstSource opened: {}", pipeline_desc_);
  return Ok();
}

Status GstSource::Start() {
  if (running_.load()) return Ok();
  if (!pipeline_) return Err(ErrorCode::kInternal, "Start() before Open()");

  GstStateChangeReturn r = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  if (r == GST_STATE_CHANGE_FAILURE)
    return Err(ErrorCode::kDeviceError, "failed to set pipeline PLAYING");

  running_.store(true);
  seq_ = 0;
  rate_.Reset();
  reconnect_backoff_ms_ = 1000;
  state_.store(State::kConnecting);
  {
    std::lock_guard<std::mutex> lk(err_mtx_);
    last_error_.clear();
  }
  thread_ = std::thread(&GstSource::PullLoop, this);
  XLOG_INFO("GstSource started");
  return Ok();
}

std::string GstSource::last_error() const {
  std::lock_guard<std::mutex> lk(err_mtx_);
  return last_error_;
}

bool GstSource::HandleBus() {
  while (GstMessage* msg = gst_bus_pop_filtered(
             bus_, static_cast<GstMessageType>(GST_MESSAGE_ERROR |
                                               GST_MESSAGE_EOS))) {
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
      GError* err = nullptr;
      gchar* dbg = nullptr;
      gst_message_parse_error(msg, &err, &dbg);
      std::string text = err ? err->message : "unknown stream error";
      XLOG_ERROR("GstSource error: {} ({})", text, dbg ? dbg : "-");
      {
        std::lock_guard<std::mutex> lk(err_mtx_);
        last_error_ = text;
      }
      if (err) g_error_free(err);
      g_free(dbg);
      state_.store(State::kError);
      gst_message_unref(msg);
      if (auto_reconnect_.load()) {
        Reconnect();
        continue;
      }
      return false;
    }
    // EOS
    XLOG_WARN("GstSource: end-of-stream");
    state_.store(State::kEos);
    gst_message_unref(msg);
    if (auto_reconnect_.load()) {
      Reconnect();
      continue;
    }
    return false;
  }
  return true;
}

void GstSource::Reconnect() {
  XLOG_INFO("GstSource: reconnecting in {} ms", reconnect_backoff_ms_);
  gst_element_set_state(pipeline_, GST_STATE_NULL);
  for (int waited = 0; waited < reconnect_backoff_ms_ && running_.load();
       waited += 100)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if (!running_.load()) return;
  reconnect_backoff_ms_ = std::min(reconnect_backoff_ms_ * 2, 10000);
  gst_bus_set_flushing(bus_, TRUE);
  gst_bus_set_flushing(bus_, FALSE);
  state_.store(State::kConnecting);
  gst_element_set_state(pipeline_, GST_STATE_PLAYING);
}

void GstSource::PullLoop() {
  while (running_.load()) {
    if (!HandleBus()) break;  // dead stream, no reconnect wanted

    // Bounded wait so we can observe running_ and bus messages.
    GstSample* sample =
        gst_app_sink_try_pull_sample(appsink_, 200 * GST_MSECOND);
    if (!sample) continue;

    VideoFrame f;
    if (GstSampleToVideoFrame(sample, seq_, &f)) {
      ++seq_;
      state_.store(State::kPlaying);
      reconnect_backoff_ms_ = 1000;  // healthy again: reset the backoff
      const double fps = rate_.Tick();
      {
        std::lock_guard<std::mutex> lk(stats_mtx_);
        stats_.capture_fps = fps;
        stats_.frames = rate_.count();
        stats_.decoder = "gstreamer";
      }
      EmitFrame(f);
      frames_.Push(std::move(f));
    }
    gst_sample_unref(sample);
  }
}

void GstSource::Stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
  if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
  XLOG_INFO("GstSource stopped ({} frames)", seq_);
}

void GstSource::Close() {
  Stop();
  if (bus_) {
    gst_object_unref(bus_);
    bus_ = nullptr;
  }
  state_.store(State::kIdle);
  if (appsink_) {
    gst_object_unref(appsink_);
    appsink_ = nullptr;
  }
  if (pipeline_) {
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
  }
}

SourceStats GstSource::GetStats() const {
  std::lock_guard<std::mutex> lk(stats_mtx_);
  return stats_;
}

}  // namespace xmotion
