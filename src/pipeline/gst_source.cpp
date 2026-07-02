/*
 * @file gst_source.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/pipeline/gst_source.hpp"

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
  // uridecodebin handles rtsp/http/file incl. depay+decode; RGBA for GL upload.
  // I420 out: most decoders emit it natively so videoconvert is passthrough;
  // the GPU converter handles YUV->RGB for display.
  return "uridecodebin uri=" + uri +
         " ! videoconvert ! video/x-raw,format=I420 ! "
         "appsink name=sink max-buffers=1 drop=true sync=false";
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
  thread_ = std::thread(&GstSource::PullLoop, this);
  XLOG_INFO("GstSource started");
  return Ok();
}

void GstSource::PullLoop() {
  while (running_.load()) {
    // Bounded wait so we can observe running_ and EOS/errors.
    GstSample* sample =
        gst_app_sink_try_pull_sample(appsink_, 200 * GST_MSECOND);
    if (!sample) {
      if (gst_app_sink_is_eos(appsink_)) {
        XLOG_WARN("GstSource: end-of-stream");
        break;
      }
      continue;  // timeout, keep waiting
    }

    VideoFrame f;
    if (GstSampleToVideoFrame(sample, seq_, &f)) {
      ++seq_;
      const double fps = rate_.Tick();
      {
        std::lock_guard<std::mutex> lk(stats_mtx_);
        stats_.capture_fps = fps;
        stats_.frames = rate_.count();
        stats_.decoder = "gstreamer";
      }
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
