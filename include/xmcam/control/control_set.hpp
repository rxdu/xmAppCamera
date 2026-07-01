/*
 * @file control_set.hpp
 * @brief Live, mutable view of a device's V4L2 controls. Wraps introspection
 *        plus get/set of individual control values. After a successful Set it
 *        re-introspects so dependent controls' INACTIVE state stays accurate
 *        (e.g. toggling auto-exposure activates exposure_time_absolute).
 *
 * Thread-safe: all public methods are guarded by an internal mutex.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CONTROL_CONTROL_SET_HPP
#define XMCAM_CONTROL_CONTROL_SET_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "xmcam/control/control_info.hpp"
#include "xmcam/core/result.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"

namespace xmotion {

class ControlSet {
 public:
  explicit ControlSet(std::shared_ptr<V4l2Device> dev);

  // (Re)enumerate the device's controls, replacing the cached list.
  Status Refresh();

  // Cached control list from the last Refresh(). Empty until Refresh() runs.
  const std::vector<ControlInfo>& controls() const { return controls_; }

  // Read the current value of control `id` into *value. int64 controls use the
  // 64-bit field; everything else the 32-bit field.
  Status Get(uint32_t id, int64_t* value) const;

  // Write `value` to control `id`, then re-introspect so dependent controls'
  // active-state reflects the change. Refuses inactive/read-only controls.
  Status Set(uint32_t id, int64_t value);

  // Cached descriptor for `id`, or nullptr if unknown. Pointer is invalidated
  // by the next Refresh()/Set().
  const ControlInfo* Find(uint32_t id) const;

 private:
  Status RefreshLocked();                       // caller holds mutex_
  const ControlInfo* FindLocked(uint32_t id) const;  // caller holds mutex_

  std::shared_ptr<V4l2Device> dev_;
  mutable std::mutex mutex_;
  std::vector<ControlInfo> controls_;
};

}  // namespace xmotion

#endif  // XMCAM_CONTROL_CONTROL_SET_HPP
