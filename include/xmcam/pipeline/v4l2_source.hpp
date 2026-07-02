/*
 * @file v4l2_source.hpp
 * @brief V4L2 capture source: device enumeration, capability query, and
 *        mmap streaming capture of raw formats. Compressed formats (MJPEG) are
 *        handled by a GStreamer decode branch (Phase 1.5, wired separately).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_PIPELINE_V4L2_SOURCE_HPP
#define XMCAM_PIPELINE_V4L2_SOURCE_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "xmcam/core/util/rate_counter.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"
#include "xmcam/pipeline/video_source.hpp"

namespace xmotion {

#ifdef XMCAM_WITH_GSTREAMER
class GstCompressedDecoder;  // fwd (MJPEG/H.264 decode branch)
#endif

// Owns the mmap regions + streaming lifecycle. Held by a shared_ptr so it
// outlives V4l2Source::Close() until every in-flight frame lease releases;
// munmap/STREAMOFF happen only in the destructor. This is what makes the
// zero-copy frame path free of use-after-unmap.
class V4l2BufferPool {
 public:
  struct Slot {
    void* start = nullptr;
    std::size_t length = 0;
  };

  V4l2BufferPool(std::shared_ptr<V4l2Device> dev, std::vector<Slot> slots)
      : dev_(std::move(dev)), slots_(std::move(slots)) {}
  ~V4l2BufferPool();

  void Requeue(unsigned index);  // VIDIOC_QBUF (errors ignored post-STREAMOFF)
  const Slot& slot(unsigned i) const { return slots_[i]; }
  std::size_t size() const { return slots_.size(); }

 private:
  std::shared_ptr<V4l2Device> dev_;
  std::vector<Slot> slots_;
};

class V4l2Source : public VideoSource {
 public:
  // Scan /dev/video* and return capture-capable devices with their caps.
  static std::vector<DeviceInfo> Enumerate();
  // Query capabilities of a single device node.
  static Status QueryCaps(const std::string& device, SourceCaps* out);

  V4l2Source();  // out-of-line (unique_ptr to incomplete GstCompressedDecoder)
  ~V4l2Source() override;

  Status Open(const SourceDescriptor& desc) override;
  Status Start() override;
  void Stop() override;
  void Close() override;
  bool IsRunning() const override { return running_.load(); }

  SourceCaps GetCaps() const override { return caps_; }
  SourceStats GetStats() const override;
  DataStream<VideoFrame>& Frames() override { return frames_; }

  // Negotiated format after Open().
  PixelFormat negotiated_format() const { return neg_format_; }
  int negotiated_width() const { return neg_w_; }
  int negotiated_height() const { return neg_h_; }

 private:
  Status NegotiateFormat();
  Status SetupBuffers(unsigned count);
  void CaptureLoop();
  // Hot-plug: re-open (preferring the stable by-id path), renegotiate, and
  // restart streaming. Loops with backoff until success or Stop().
  bool RecoverDevice();

  std::shared_ptr<V4l2Device> dev_;
  SourceDescriptor desc_;
  SourceCaps caps_;
  DataStream<VideoFrame> frames_;

  std::shared_ptr<V4l2BufferPool> pool_;
#ifdef XMCAM_WITH_GSTREAMER
  std::unique_ptr<GstCompressedDecoder> decoder_;  // MJPEG/H.264 branch
#endif
  std::thread thread_;
  std::atomic<bool> running_{false};

  PixelFormat neg_format_ = PixelFormat::kUnknown;
  int neg_w_ = 0;
  int neg_h_ = 0;
  int neg_stride_ = 0;
  uint64_t seq_ = 0;
  // Hot-plug recovery identity. by-path (physical port) is tried first: it is
  // unambiguous on fixed wiring even when units share a USB serial; by-id
  // (serial) is the fallback for a device replugged into a different port.
  std::string by_path_path_;
  std::string by_id_path_;
  std::string card_;  // QUERYCAP card, verified on recovery re-open
  uint32_t generation_ = 0;

  mutable std::mutex stats_mtx_;
  SourceStats stats_;
  RateCounter capture_rate_;
};

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_V4L2_SOURCE_HPP
