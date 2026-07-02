/*
 * @file frame_tap.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/qualify/frame_tap.hpp"

#include <cstddef>

#include "xmcam/core/pixel_format.hpp"

namespace xmotion {
namespace {

// Sample every 8th row/column to keep OnFrame cheap on the producer thread.
constexpr int kSubsampleStep = 8;
// Cap on the recorded pts series; enough for minutes of timestamps at 30 fps.
constexpr size_t kMaxPtsEntries = 4096;

// Mean of a subsampled grid over one plane. `width` is in bytes (samples per
// row), so packed formats can pass width * bytes-per-pixel.
double PlaneMean(const uint8_t* base, int stride, int width, int height) {
  if (base == nullptr || width <= 0 || height <= 0) return 0.0;
  uint64_t sum = 0;
  uint64_t count = 0;
  for (int y = 0; y < height; y += kSubsampleStep) {
    const uint8_t* row = base + static_cast<size_t>(y) * stride;
    for (int x = 0; x < width; x += kSubsampleStep) {
      sum += row[x];
      ++count;
    }
  }
  return count > 0 ? static_cast<double>(sum) / static_cast<double>(count)
                   : 0.0;
}

}  // namespace

void FrameTap::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  snap_ = Snapshot{};
}

void FrameTap::OnFrame(const VideoFrame& f) {
  if (!f.valid()) return;

  // Compute the (subsampled) per-frame means outside the lock.
  double y = 0, u = 0, v = 0;
  if (f.format == PixelFormat::kI420 && f.plane1 != nullptr &&
      f.plane2 != nullptr) {
    y = PlaneMean(f.data, f.stride, f.width, f.height);
    u = PlaneMean(f.plane1, f.stride1, f.width / 2, f.height / 2);
    v = PlaneMean(f.plane2, f.stride2, f.width / 2, f.height / 2);
  } else {
    // Packed (or non-I420 planar) formats: mean of plane-0 bytes only. For
    // planar formats plane 0 is the Y plane at 1 byte per pixel.
    int bytes_per_px = 1;
    if (!IsPlanar(f.format)) {
      const int bpp = BitsPerPixel(f.format);
      if (bpp >= 8) bytes_per_px = bpp / 8;
    }
    y = PlaneMean(f.data, f.stride, f.width * bytes_per_px, f.height);
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
}

FrameTap::Snapshot FrameTap::Take() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snap_;
}

}  // namespace xmotion
