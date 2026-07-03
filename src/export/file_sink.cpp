/*
 * @file file_sink.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/export/file_sink.hpp"

#include <cstring>
#include <filesystem>

#include "xmbase/logging/xlogger.hpp"

namespace xmotion {
namespace {

constexpr size_t kMaxQueue = 16;  // bounded: drop (and count) beyond this

}  // namespace

const char* FileSink::Extension(Format f) {
  return f == Format::kY4m ? "y4m" : "mkv";
}

FileSink::~FileSink() { Stop(); }

Status FileSink::Start(const std::string& path, Format format, double fps,
                       int64_t epoch_ns) {
  if (running_.load()) return Ok();
  if (!gst_is_initialized()) gst_init(nullptr, nullptr);
  path_ = path;
  // Create the parent directory so "recordings/..." works out of the box.
  std::error_code ec;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  format_ = format;
  fps_ = fps > 0 ? fps : 30.0;
  epoch_ns_ = epoch_ns;
  stats_ = Stats{};
  running_.store(true);
  writer_ = std::thread(&FileSink::WriterLoop, this);
  XLOG_INFO("FileSink recording to {} ({})", path_,
            format == Format::kY4m             ? "raw y4m"
            : format == Format::kFfv1Mkv       ? "ffv1 lossless"
            : format == Format::kPassthroughMkv ? "passthrough"
                                                : "h264");
  return Ok();
}

bool FileSink::EnsurePipeline(const Packed& first) {
  if (pipeline_) return true;

  // The pipeline is created on the first frame (we need geometry/codec).
  //   h264:        quality-oriented encode for long captures (default).
  //   passthrough: mux the camera's own bitstream — zero decode/re-encode.
  //   ffv1:        mathematically lossless intra-frame codec.
  //   y4m:         bit-exact raw I420 container, zero processing.
  gchar* fps_frac = g_strdup_printf("%d/100", static_cast<int>(fps_ * 100));
  gchar* desc = nullptr;
  if (format_ == Format::kPassthroughMkv) {
    const char* caps_and_parse =
        first.fmt == PixelFormat::kH264
            ? "caps=video/x-h264,stream-format=byte-stream,alignment=au,"
              "width=%d,height=%d,framerate=%s ! h264parse"
            : "caps=image/jpeg,width=%d,height=%d,framerate=%s ! jpegparse";
    gchar* head = g_strdup_printf(caps_and_parse, first.width, first.height,
                                  fps_frac);
    desc = g_strdup_printf(
        "appsrc name=src is-live=true block=true format=time "
        "do-timestamp=%s %s ! matroskamux ! filesink location=\"%s\"",
        epoch_ns_ ? "false" : "true", head, path_.c_str());
    g_free(head);
  } else {
    std::string encode;
    switch (format_) {
      case Format::kY4m: encode = "y4menc"; break;
      case Format::kFfv1Mkv: encode = "avenc_ffv1 ! matroskamux"; break;
      default:
        encode =
            "x264enc speed-preset=fast tune=zerolatency bitrate=8000 ! "
            "h264parse ! matroskamux";
        break;
    }
    desc = g_strdup_printf(
        "appsrc name=src is-live=true block=true format=time "
        "do-timestamp=%s "
        "caps=video/x-raw,format=I420,width=%d,height=%d,framerate=%s ! "
        "%s ! filesink location=\"%s\"",
        epoch_ns_ ? "false" : "true", first.width, first.height, fps_frac,
        encode.c_str(), path_.c_str());
  }
  GError* err = nullptr;
  pipeline_ = gst_parse_launch(desc, &err);
  g_free(fps_frac);
  g_free(desc);
  if (!pipeline_ || err) {
    XLOG_ERROR("FileSink pipeline: {}", err ? err->message : "unknown");
    if (err) g_error_free(err);
    if (pipeline_) {
      gst_object_unref(pipeline_);
      pipeline_ = nullptr;
    }
    return false;
  }
  appsrc_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
  gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  return true;
}

void FileSink::OnFrame(const VideoFrame& f) {
  if (!running_.load() || !f.valid()) return;

  Packed p;
  p.fmt = f.format;
  p.width = f.width;
  p.height = f.height;
  p.pts_ns = f.pts_ns;

  if (format_ == Format::kPassthroughMkv) {
    // Passthrough wants the camera's compressed bitstream, nothing else.
    if (!IsCompressed(f.format) || f.data_size == 0) return;
    p.data.assign(f.data, f.data + f.data_size);
    {
      std::lock_guard<std::mutex> lk(mtx_);
      if (queue_.size() >= kMaxQueue) {
        ++stats_.frames_dropped;
        return;
      }
      queue_.push_back(std::move(p));
    }
    cv_.notify_one();
    return;
  }

  if (f.format != PixelFormat::kI420) return;
  // Tightly pack the (possibly strided) planes; the copy is the price of the
  // never-block contract — the writer thread does the slow disk/encode work.
  const int cw = (f.width + 1) / 2;
  const int ch = (f.height + 1) / 2;
  p.data.resize(static_cast<size_t>(f.width) * f.height +
                2 * static_cast<size_t>(cw) * ch);
  uint8_t* dst = p.data.data();
  for (int y = 0; y < f.height; ++y)
    std::memcpy(dst + static_cast<size_t>(y) * f.width,
                f.data + static_cast<size_t>(y) * f.stride, f.width);
  dst += static_cast<size_t>(f.width) * f.height;
  for (int y = 0; y < ch; ++y)
    std::memcpy(dst + static_cast<size_t>(y) * cw,
                f.plane1 + static_cast<size_t>(y) * f.stride1, cw);
  dst += static_cast<size_t>(cw) * ch;
  for (int y = 0; y < ch; ++y)
    std::memcpy(dst + static_cast<size_t>(y) * cw,
                f.plane2 + static_cast<size_t>(y) * f.stride2, cw);

  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (queue_.size() >= kMaxQueue) {
      ++stats_.frames_dropped;  // disk/encoder too slow: drop, never block
      return;
    }
    queue_.push_back(std::move(p));
  }
  cv_.notify_one();
}

void FileSink::WriterLoop() {
  while (true) {
    Packed p;
    {
      std::unique_lock<std::mutex> lk(mtx_);
      cv_.wait(lk, [&] { return !queue_.empty() || !running_.load(); });
      if (queue_.empty() && !running_.load()) break;
      p = std::move(queue_.front());
      queue_.pop_front();
    }

    if (!EnsurePipeline(p)) {
      running_.store(false);
      break;
    }
    if (p.width != 0) {
      GstBuffer* buf =
          gst_buffer_new_allocate(nullptr, p.data.size(), nullptr);
      gst_buffer_fill(buf, 0, p.data.data(), p.data.size());
      if (epoch_ns_ != 0) {
        // Epoch mode: timeline zero is the shared group epoch, so files
        // recorded together are aligned by CAPTURE time, not arrival.
        const int64_t rel = p.pts_ns > epoch_ns_ ? p.pts_ns - epoch_ns_ : 0;
        GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(rel);
        GST_BUFFER_DURATION(buf) =
            static_cast<GstClockTime>(1e9 / fps_);
      }
      if (gst_app_src_push_buffer(appsrc_, buf) == GST_FLOW_OK) {
        std::lock_guard<std::mutex> lk(mtx_);
        ++stats_.frames_written;
      }
    }
  }

  // Finalize: EOS through the muxer so the container index is written.
  if (pipeline_) {
    if (appsrc_) gst_app_src_end_of_stream(appsrc_);
    GstBus* bus = gst_element_get_bus(pipeline_);
    GstMessage* msg = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (msg) gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (appsrc_) {
      gst_object_unref(appsrc_);
      appsrc_ = nullptr;
    }
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
  }
}

void FileSink::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  cv_.notify_all();
  if (writer_.joinable()) writer_.join();
  XLOG_INFO("FileSink finalized {} ({} frames, {} dropped)", path_,
            stats_.frames_written, stats_.frames_dropped);
}

}  // namespace xmotion
