/*
 * @file rate_counter.hpp
 * @brief EWMA rate (fps) + drop counter. Thread-affinity: call Tick() from one
 *        thread; read snapshots from anywhere via atomics-free copy under the
 *        caller's own synchronization (stats are advisory).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_UTIL_RATE_COUNTER_HPP
#define XMCAM_CORE_UTIL_RATE_COUNTER_HPP

#include <chrono>
#include <cstdint>

namespace xmotion {

class RateCounter {
 public:
  using Clock = std::chrono::steady_clock;

  explicit RateCounter(double smoothing = 0.1) : alpha_(smoothing) {}

  // Record one event at time `now`. Returns the current smoothed rate (Hz).
  double Tick(Clock::time_point now) {
    ++count_;
    if (have_last_) {
      const double dt =
          std::chrono::duration<double>(now - last_).count();
      if (dt > 0.0) {
        const double inst = 1.0 / dt;
        rate_ = have_rate_ ? (alpha_ * inst + (1.0 - alpha_) * rate_) : inst;
        have_rate_ = true;
      }
    }
    last_ = now;
    have_last_ = true;
    return rate_;
  }

  double Tick() { return Tick(Clock::now()); }

  double rate() const { return rate_; }
  uint64_t count() const { return count_; }
  void Reset() {
    rate_ = 0.0;
    count_ = 0;
    have_last_ = have_rate_ = false;
  }

 private:
  double alpha_;
  double rate_ = 0.0;
  uint64_t count_ = 0;
  Clock::time_point last_{};
  bool have_last_ = false;
  bool have_rate_ = false;
};

}  // namespace xmotion

#endif  // XMCAM_CORE_UTIL_RATE_COUNTER_HPP
