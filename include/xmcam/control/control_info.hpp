/*
 * @file control_info.hpp
 * @brief Device-agnostic description of a single V4L2 control: its type, range,
 *        default, menu entries and runtime flags (inactive/read-only/...).
 *        Populated by ControlIntrospector; consumed by ControlSet and UI.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CONTROL_CONTROL_INFO_HPP
#define XMCAM_CONTROL_CONTROL_INFO_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace xmotion {

// Control value semantics, mapped from V4L2_CTRL_TYPE_*.
enum class ControlType {
  kInteger,
  kInteger64,
  kBoolean,
  kMenu,
  kIntegerMenu,
  kBitmask,
  kButton,
  kString,
  kUnknown,
};

const char* ToString(ControlType t);

// One option of a (menu / integer-menu) control. For a plain menu the label is
// the driver-provided name and `value` is the selection index; for an integer
// menu `value` is the underlying integer and the label is its decimal text.
struct ControlMenuItem {
  int64_t value;
  std::string label;
};

// V4L2 control flag bits we care about (mirror of <linux/videodev2.h>).
struct ControlInfo {
  uint32_t id = 0;
  std::string name;
  ControlType type = ControlType::kUnknown;
  int64_t minimum = 0;
  int64_t maximum = 0;
  int64_t step = 0;
  int64_t default_value = 0;
  uint32_t flags = 0;
  std::vector<ControlMenuItem> menu;

  // Currently unsettable because a dependent auto-mode owns it (e.g. exposure
  // time while auto-exposure is on). V4L2_CTRL_FLAG_INACTIVE.
  bool IsInactive() const { return (flags & 0x0010u) != 0; }
  // V4L2_CTRL_FLAG_READ_ONLY.
  bool IsReadOnly() const { return (flags & 0x0004u) != 0; }
  // V4L2_CTRL_FLAG_VOLATILE — value may change without a Set (auto-driven).
  bool IsVolatile() const { return (flags & 0x0080u) != 0; }
  // V4L2_CTRL_FLAG_DISABLED — permanently unsupported on this device.
  bool IsDisabled() const { return (flags & 0x0001u) != 0; }
};

}  // namespace xmotion

#endif  // XMCAM_CONTROL_CONTROL_INFO_HPP
