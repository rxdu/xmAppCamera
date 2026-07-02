/*
 * @file fanout_sink.hpp
 * @brief Forwards each frame to N registered sinks, so RTSP export, file
 *        recording and the qualification tap can consume one source
 *        concurrently. Add/Remove are render-thread; OnFrame is the producer
 *        thread — guarded by a short uncontended mutex.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_FANOUT_SINK_HPP
#define XMCAM_CORE_FANOUT_SINK_HPP

#include <algorithm>
#include <mutex>
#include <vector>

#include "xmcam/core/frame_sink.hpp"

namespace xmotion {

class FanoutSink : public FrameSink {
 public:
  void Add(FrameSink* sink) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end())
      sinks_.push_back(sink);
  }
  void Remove(FrameSink* sink) {
    std::lock_guard<std::mutex> lk(mtx_);
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink),
                 sinks_.end());
  }
  bool Contains(FrameSink* sink) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return std::find(sinks_.begin(), sinks_.end(), sink) != sinks_.end();
  }
  size_t size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return sinks_.size();
  }

  void OnFrame(const VideoFrame& frame) override {
    std::lock_guard<std::mutex> lk(mtx_);
    for (FrameSink* s : sinks_) s->OnFrame(frame);
  }

 private:
  mutable std::mutex mtx_;
  std::vector<FrameSink*> sinks_;
};

}  // namespace xmotion

#endif  // XMCAM_CORE_FANOUT_SINK_HPP
