/*
 * @file frame_tap.hpp
 * @brief FrameSink that accumulates cheap per-frame statistics (timestamps,
 *        subsampled plane means, hardware-sequence gaps, stuck/black/white
 *        frame counters, interval stats and per-10s frame buckets) for the
 *        qualification checks. The producer thread calls OnFrame(); the
 *        GUI/worker thread calls Take()/Reset()/RecentMeans(). All state is
 *        mutex-protected.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_QUALIFY_FRAME_TAP_HPP
#define XMCAM_QUALIFY_FRAME_TAP_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "xmcam/core/frame_sink.hpp"
#include "xmcam/core/video_frame.hpp"

namespace xmotion {

class FrameTap : public FrameSink {
 public:
  struct Snapshot {
    uint64_t frames = 0;
    std::vector<int64_t> pts_ns;  // in arrival order, capped at 4096 entries
    // Running mean over frames. For I420 these are the Y/U/V plane means; for
    // packed formats the mean of plane-0 bytes lands in mean_y and u/v stay 0.
    double mean_y = 0, mean_u = 0, mean_v = 0;
    int width = 0, height = 0;
    // Aggregates since Reset():
    // Sum over frames of max(0, hw_seq - prev_hw_seq - 1). Frames with
    // hw_seq == 0 (non-V4L2 sources) and the first frame are ignored.
    uint64_t hw_seq_gaps = 0;
    // Frames whose subsampled checksum equals the previous frame's exactly.
    uint64_t stuck_frames = 0;
    uint64_t black_frames = 0;  // frames with per-frame mean_y < 8
    uint64_t white_frames = 0;  // frames with per-frame mean_y > 247
    // Welford running statistics over successive pts deltas, in milliseconds.
    double interval_mean_ms = 0;
    double interval_stddev_ms = 0;
    double interval_min_ms = 0;
    double interval_max_ms = 0;
    // Frames per 10-second bucket, indexed from the first pts since Reset().
    std::vector<uint32_t> per10s_frames;
  };

  // One per-frame subsampled Y mean, tagged with the frame's pts.
  struct MeanSample {
    int64_t pts_ns;
    double mean_y;
  };

  // Clear all accumulated statistics.
  void Reset();

  // Called on the source's producer thread. Cheap: subsamples pixels (every
  // 8th row/column) and never blocks beyond a short mutex hold.
  void OnFrame(const VideoFrame& f) override;

  // Copy of the current statistics.
  Snapshot Take() const;

  // The last <= 256 per-frame Y means, oldest first. Used by the write-effect
  // latency check to spot the first frame reacting to a control write.
  std::vector<MeanSample> RecentMeans() const;

 private:
  mutable std::mutex mutex_;
  Snapshot snap_;

  // Per-frame carry state behind the aggregate counters (cleared on Reset).
  bool has_prev_hw_seq_ = false;
  uint32_t prev_hw_seq_ = 0;
  bool has_prev_checksum_ = false;
  uint64_t prev_checksum_ = 0;
  bool has_prev_pts_ = false;
  int64_t prev_pts_ns_ = 0;
  int64_t first_pts_ns_ = 0;

  // Welford accumulators over pts deltas (ms); folded into the Snapshot's
  // interval_mean_ms / interval_stddev_ms in Take().
  uint64_t interval_n_ = 0;
  double interval_mean_ = 0;
  double interval_m2_ = 0;

  // Ring buffer behind RecentMeans(): oldest entry at recent_next_ once full.
  std::vector<MeanSample> recent_;
  size_t recent_next_ = 0;
};

}  // namespace xmotion

#endif  // XMCAM_QUALIFY_FRAME_TAP_HPP
