/*
 * @file control_set.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/control/control_set.hpp"

#include <linux/videodev2.h>

#include <cerrno>
#include <cstring>
#include <unordered_map>

#include "xmcam/control/control_introspector.hpp"
#include "xmbase/logging/xlogger.hpp"

namespace xmotion {
namespace {

bool IsWideType(ControlType t) {
  // Controls whose value lives in the 64-bit union field of v4l2_ext_control.
  return t == ControlType::kInteger64;
}

}  // namespace

ControlSet::ControlSet(std::shared_ptr<V4l2Device> dev) : dev_(std::move(dev)) {}

Status ControlSet::Refresh() {
  std::lock_guard<std::mutex> lk(mutex_);
  return RefreshLocked();
}

Status ControlSet::RefreshLocked() {
  if (!dev_ || !dev_->IsOpen())
    return Err(ErrorCode::kInvalidArgument, "Refresh: device not open");
  return ControlIntrospector::Enumerate(*dev_, &controls_);
}

const ControlInfo* ControlSet::Find(uint32_t id) const {
  std::lock_guard<std::mutex> lk(mutex_);
  return FindLocked(id);
}

const ControlInfo* ControlSet::FindLocked(uint32_t id) const {
  for (const auto& c : controls_) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

Status ControlSet::Get(uint32_t id, int64_t* value) const {
  if (value == nullptr)
    return Err(ErrorCode::kInvalidArgument, "Get: value is null");
  std::lock_guard<std::mutex> lk(mutex_);
  if (!dev_ || !dev_->IsOpen())
    return Err(ErrorCode::kInvalidArgument, "Get: device not open");

  const ControlInfo* info = FindLocked(id);
  const bool wide = info != nullptr && IsWideType(info->type);

  v4l2_ext_control ctrl{};
  ctrl.id = id;
  v4l2_ext_controls ctrls{};
  ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
  ctrls.count = 1;
  ctrls.controls = &ctrl;

  if (dev_->Ioctl(VIDIOC_G_EXT_CTRLS, &ctrls) != 0)
    return Err(ErrorCode::kDeviceError,
               std::string("G_EXT_CTRLS: ") + std::strerror(errno));

  *value = wide ? ctrl.value64 : static_cast<int64_t>(ctrl.value);
  return Ok();
}

Status ControlSet::Set(uint32_t id, int64_t value) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!dev_ || !dev_->IsOpen())
    return Err(ErrorCode::kInvalidArgument, "Set: device not open");

  const ControlInfo* info = FindLocked(id);
  if (info == nullptr)
    return Err(ErrorCode::kNotFound, "Set: unknown control id");
  if (info->IsReadOnly())
    return Err(ErrorCode::kInvalidArgument, "Set: control is read-only");
  if (info->IsInactive())
    return Err(ErrorCode::kInvalidArgument,
               "Set: control is inactive (an auto-mode owns it)");
  if (info->IsDisabled())
    return Err(ErrorCode::kUnsupported, "Set: control is disabled");

  const bool wide = IsWideType(info->type);

  v4l2_ext_control ctrl{};
  ctrl.id = id;
  if (wide)
    ctrl.value64 = value;
  else
    ctrl.value = static_cast<int32_t>(value);

  v4l2_ext_controls ctrls{};
  ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
  ctrls.count = 1;
  ctrls.controls = &ctrl;

  if (dev_->Ioctl(VIDIOC_S_EXT_CTRLS, &ctrls) != 0)
    return Err(ErrorCode::kDeviceError,
               std::string("S_EXT_CTRLS: ") + std::strerror(errno));

  // Snapshot active-state, then re-introspect so dependent controls (exposure,
  // white balance temperature) reflect any auto-mode toggle we just performed.
  std::unordered_map<uint32_t, bool> was_inactive;
  was_inactive.reserve(controls_.size());
  for (const auto& c : controls_) was_inactive[c.id] = c.IsInactive();

  Status st = RefreshLocked();
  if (!st.ok()) return st;

  for (const auto& c : controls_) {
    auto it = was_inactive.find(c.id);
    if (it != was_inactive.end() && it->second != c.IsInactive()) {
      XLOG_DEBUG("control '{}' active-state changed: now {}", c.name,
                 c.IsInactive() ? "INACTIVE" : "ACTIVE");
    }
  }
  return Ok();
}

}  // namespace xmotion
