/*
 * @file scope_timer.hpp
 * @brief RAII stage timer. Logs elapsed microseconds at TRACE on scope exit, or
 *        writes into a caller-provided sink. Compile-gated with XLOG_TRACE, so
 *        it costs nothing in release builds where trace is disabled.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_UTIL_SCOPE_TIMER_HPP
#define XMCAM_CORE_UTIL_SCOPE_TIMER_HPP

#include <chrono>

#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

class ScopeTimer {
 public:
  using Clock = std::chrono::steady_clock;

  explicit ScopeTimer(const char* stage, double* out_ms = nullptr)
      : stage_(stage), out_ms_(out_ms), start_(Clock::now()) {}

  ~ScopeTimer() {
    const double ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start_)
            .count();
    if (out_ms_) *out_ms_ = ms;
    XLOG_TRACE("[timer] {} {:.3f} ms", stage_, ms);
  }

  ScopeTimer(const ScopeTimer&) = delete;
  ScopeTimer& operator=(const ScopeTimer&) = delete;

 private:
  const char* stage_;
  double* out_ms_;
  Clock::time_point start_;
};

}  // namespace xmotion

#endif  // XMCAM_CORE_UTIL_SCOPE_TIMER_HPP
