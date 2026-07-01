/*
 * @file source_stats.hpp
 * @brief Per-source performance counters surfaced to logs + StatsPanel.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_SOURCE_STATS_HPP
#define XMCAM_CORE_SOURCE_STATS_HPP

#include <cstdint>
#include <string>

namespace xmotion {

struct SourceStats {
  double capture_fps = 0.0;   // frames dequeued from the source
  double display_fps = 0.0;   // frames uploaded/presented
  uint64_t frames = 0;        // total frames produced
  uint64_t dropped = 0;       // frames dropped (latest-only, or overrun)
  int queue_depth = 0;        // in-flight frames
  double decode_ms = 0.0;     // last decode stage duration
  double upload_ms = 0.0;     // last GL upload duration
  double latency_ms = 0.0;    // glass-to-glass estimate (present - pts)
  std::string decoder;        // active decoder element / "raw"
};

}  // namespace xmotion

#endif  // XMCAM_CORE_SOURCE_STATS_HPP
