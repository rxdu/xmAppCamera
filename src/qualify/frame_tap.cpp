/*
 * @file frame_tap.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/qualify/frame_tap.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "xmcam/core/pixel_format.hpp"

namespace xmotion {
namespace {

// Sample every 8th row/column to keep OnFrame cheap on the producer thread.
constexpr int kSubsampleStep = 8;
// Cap on the recorded pts series; enough for minutes of timestamps at 30 fps.
constexpr size_t kMaxPtsEntries = 4096;
// Ring capacity behind RecentMeans().
constexpr size_t kMaxRecentMeans = 256;
// Per-frame Y-mean thresholds for the black/white frame counters.
constexpr double kBlackMeanY = 8.0;
constexpr double kWhiteMeanY = 247.0;
// Width of one per10s_frames bucket, and a hard cap on the bucket count so a
// corrupt pts cannot grow the vector unboundedly (8640 buckets = 24 h).
constexpr int64_t kBucketNs = 10LL * 1000000000LL;
constexpr size_t kMaxBuckets = 8640;
// FNV-1a parameters for the subsampled stuck-frame checksum.
constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

// Mean of a subsampled grid over one plane, folding the sampled bytes into
// *checksum (FNV-1a) for stuck-frame detection. `width` is in bytes (samples
// per row), so packed formats can pass width * bytes-per-pixel.
double PlaneMean(const uint8_t* base, int stride, int width, int height,
                 uint64_t* checksum) {
  if (base == nullptr || width <= 0 || height <= 0) return 0.0;
  uint64_t sum = 0;
  uint64_t count = 0;
  for (int y = 0; y < height; y += kSubsampleStep) {
    const uint8_t* row = base + static_cast<size_t>(y) * stride;
    for (int x = 0; x < width; x += kSubsampleStep) {
      sum += row[x];
      ++count;
      *checksum = (*checksum ^ row[x]) * kFnvPrime;
    }
  }
  return count > 0 ? static_cast<double>(sum) / static_cast<double>(count)
                   : 0.0;
}

}  // namespace

void FrameTap::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  snap_ = Snapshot{};
  has_prev_hw_seq_ = false;
  prev_hw_seq_ = 0;
  has_prev_checksum_ = false;
  prev_checksum_ = 0;
  has_prev_pts_ = false;
  prev_pts_ns_ = 0;
  first_pts_ns_ = 0;
  interval_n_ = 0;
  interval_mean_ = 0;
  interval_m2_ = 0;
  recent_.clear();
  recent_next_ = 0;
}

void FrameTap::OnFrame(const VideoFrame& f) {
  // Compressed frames (the passthrough tee) carry a bitstream, not pixels —
  // they would pollute the means/intervals and double-count frames.
  if (!f.valid() || IsCompressed(f.format)) return;

  // Compute the (subsampled) per-frame means and checksum outside the lock.
  uint64_t checksum = kFnvOffsetBasis;
  double y = 0, u = 0, v = 0;
  if (f.format == PixelFormat::kI420 && f.plane1 != nullptr &&
      f.plane2 != nullptr) {
    y = PlaneMean(f.data, f.stride, f.width, f.height, &checksum);
    u = PlaneMean(f.plane1, f.stride1, f.width / 2, f.height / 2, &checksum);
    v = PlaneMean(f.plane2, f.stride2, f.width / 2, f.height / 2, &checksum);
  } else {
    // Packed (or non-I420 planar) formats: mean of plane-0 bytes only. For
    // planar formats plane 0 is the Y plane at 1 byte per pixel.
    int bytes_per_px = 1;
    if (!IsPlanar(f.format)) {
      const int bpp = BitsPerPixel(f.format);
      if (bpp >= 8) bytes_per_px = bpp / 8;
    }
    y = PlaneMean(f.data, f.stride, f.width * bytes_per_px, f.height,
                  &checksum);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const double n = static_cast<double>(snap_.frames);
  snap_.mean_y = (snap_.mean_y * n + y) / (n + 1.0);
  snap_.mean_u = (snap_.mean_u * n + u) / (n + 1.0);
  snap_.mean_v = (snap_.mean_v * n + v) / (n + 1.0);
  snap_.frames += 1;
  snap_.width = f.width;
  snap_.height = f.height;
  if (snap_.pts_ns.size() < kMaxPtsEntries) snap_.pts_ns.push_back(f.pts_ns);

  // Hardware sequence gaps: the V4L2 driver counter is 0 for non-V4L2 sources,
  // and the first counted frame only seeds the previous value.
  if (f.hw_seq != 0) {
    if (has_prev_hw_seq_) {
      const int64_t gap = static_cast<int64_t>(f.hw_seq) -
                          static_cast<int64_t>(prev_hw_seq_) - 1;
      if (gap > 0) snap_.hw_seq_gaps += static_cast<uint64_t>(gap);
    }
    prev_hw_seq_ = f.hw_seq;
    has_prev_hw_seq_ = true;
  }

  // Stuck / black / white frame counters.
  if (has_prev_checksum_ && checksum == prev_checksum_) ++snap_.stuck_frames;
  prev_checksum_ = checksum;
  has_prev_checksum_ = true;
  if (y < kBlackMeanY)
    ++snap_.black_frames;
  else if (y > kWhiteMeanY)
    ++snap_.white_frames;

  // Interval statistics (Welford) over successive pts deltas.
  if (has_prev_pts_) {
    const double delta_ms = static_cast<double>(f.pts_ns - prev_pts_ns_) / 1e6;
    ++interval_n_;
    const double d1 = delta_ms - interval_mean_;
    interval_mean_ += d1 / static_cast<double>(interval_n_);
    interval_m2_ += d1 * (delta_ms - interval_mean_);
    if (interval_n_ == 1) {
      snap_.interval_min_ms = delta_ms;
      snap_.interval_max_ms = delta_ms;
    } else {
      snap_.interval_min_ms = std::min(snap_.interval_min_ms, delta_ms);
      snap_.interval_max_ms = std::max(snap_.interval_max_ms, delta_ms);
    }
  } else {
    first_pts_ns_ = f.pts_ns;
  }
  prev_pts_ns_ = f.pts_ns;
  has_prev_pts_ = true;

  // Per-10-second frame buckets, indexed from the first pts since Reset().
  const int64_t offset_ns = f.pts_ns - first_pts_ns_;
  if (offset_ns >= 0) {
    const size_t bucket = static_cast<size_t>(offset_ns / kBucketNs);
    if (bucket < kMaxBuckets) {
      if (snap_.per10s_frames.size() <= bucket)
        snap_.per10s_frames.resize(bucket + 1, 0);
      ++snap_.per10s_frames[bucket];
    }
  }

  // Recent per-frame Y means (fixed ring, oldest at recent_next_ once full).
  if (recent_.size() < kMaxRecentMeans) {
    recent_.push_back(MeanSample{f.pts_ns, y});
  } else {
    recent_[recent_next_] = MeanSample{f.pts_ns, y};
    recent_next_ = (recent_next_ + 1) % kMaxRecentMeans;
  }
}

FrameTap::Snapshot FrameTap::Take() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Snapshot out = snap_;
  if (interval_n_ > 0) {
    out.interval_mean_ms = interval_mean_;
    out.interval_stddev_ms =
        std::sqrt(interval_m2_ / static_cast<double>(interval_n_));
  }
  return out;
}

std::vector<FrameTap::MeanSample> FrameTap::RecentMeans() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (recent_.size() < kMaxRecentMeans) return recent_;
  std::vector<MeanSample> out;
  out.reserve(recent_.size());
  for (size_t i = 0; i < recent_.size(); ++i)
    out.push_back(recent_[(recent_next_ + i) % kMaxRecentMeans]);
  return out;
}

}  // namespace xmotion
