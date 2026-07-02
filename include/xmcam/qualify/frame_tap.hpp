/*
 * @file frame_tap.hpp
 * @brief FrameSink that accumulates cheap per-frame statistics (timestamps and
 *        subsampled plane means) for the qualification checks. The producer
 *        thread calls OnFrame(); the GUI/worker thread calls Take()/Reset().
 *        All state is mutex-protected.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_QUALIFY_FRAME_TAP_HPP
#define XMCAM_QUALIFY_FRAME_TAP_HPP

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
  };

  // Clear all accumulated statistics.
  void Reset();

  // Called on the source's producer thread. Cheap: subsamples pixels (every
  // 8th row/column) and never blocks beyond a short mutex hold.
  void OnFrame(const VideoFrame& f) override;

  // Copy of the current statistics.
  Snapshot Take() const;

 private:
  mutable std::mutex mutex_;
  Snapshot snap_;
};

}  // namespace xmotion

#endif  // XMCAM_QUALIFY_FRAME_TAP_HPP
