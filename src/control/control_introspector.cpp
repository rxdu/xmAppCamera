/*
 * @file control_introspector.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/control/control_introspector.hpp"

#include <linux/videodev2.h>

#include <cerrno>
#include <cstring>

#include "xmbase/logging/xlogger.hpp"

namespace xmotion {

const char* ToString(ControlType t) {
  switch (t) {
    case ControlType::kInteger:
      return "integer";
    case ControlType::kInteger64:
      return "integer64";
    case ControlType::kBoolean:
      return "boolean";
    case ControlType::kMenu:
      return "menu";
    case ControlType::kIntegerMenu:
      return "integer-menu";
    case ControlType::kBitmask:
      return "bitmask";
    case ControlType::kButton:
      return "button";
    case ControlType::kString:
      return "string";
    case ControlType::kUnknown:
    default:
      return "unknown";
  }
}

namespace {

ControlType MapType(uint32_t v4l2_type) {
  switch (v4l2_type) {
    case V4L2_CTRL_TYPE_INTEGER:
      return ControlType::kInteger;
    case V4L2_CTRL_TYPE_INTEGER64:
      return ControlType::kInteger64;
    case V4L2_CTRL_TYPE_BOOLEAN:
      return ControlType::kBoolean;
    case V4L2_CTRL_TYPE_MENU:
      return ControlType::kMenu;
    case V4L2_CTRL_TYPE_INTEGER_MENU:
      return ControlType::kIntegerMenu;
    case V4L2_CTRL_TYPE_BITMASK:
      return ControlType::kBitmask;
    case V4L2_CTRL_TYPE_BUTTON:
      return ControlType::kButton;
    case V4L2_CTRL_TYPE_STRING:
      return ControlType::kString;
    default:
      return ControlType::kUnknown;
  }
}

// Enumerate menu entries for a (menu / integer-menu) control. Missing indices
// (QUERYMENU returns EINVAL) are simply skipped — menus are often sparse.
void PopulateMenu(V4l2Device& dev, const v4l2_query_ext_ctrl& qec,
                  ControlInfo* info) {
  bool integer_menu = qec.type == V4L2_CTRL_TYPE_INTEGER_MENU;
  for (int64_t idx = qec.minimum; idx <= qec.maximum; ++idx) {
    v4l2_querymenu qm{};
    qm.id = qec.id;
    qm.index = static_cast<uint32_t>(idx);
    if (dev.Ioctl(VIDIOC_QUERYMENU, &qm) != 0) continue;  // sparse: skip gaps

    ControlMenuItem item;
    if (integer_menu) {
      item.value = qm.value;
      item.label = std::to_string(qm.value);
    } else {
      item.value = idx;
      item.label = reinterpret_cast<const char*>(qm.name);
    }
    info->menu.push_back(std::move(item));
  }
}

// Perform one full NEXT_CTRL walk with the given extra flags OR'd into id.
// Returns the number of (non-class) controls appended to *out.
size_t WalkControls(V4l2Device& dev, uint32_t walk_flags,
                    std::vector<ControlInfo>* out) {
  size_t added = 0;
  uint32_t id = 0;
  while (true) {
    v4l2_query_ext_ctrl qec{};
    qec.id = id | walk_flags;
    if (dev.Ioctl(VIDIOC_QUERY_EXT_CTRL, &qec) != 0) {
      // EINVAL / ENOTTY simply means "no more controls" (or API unsupported).
      break;
    }
    // Advance the cursor before any continue; the returned id is the current
    // control, NEXT_CTRL uses it as the "greater than" seed on the next call.
    id = qec.id;

    if (qec.type == V4L2_CTRL_TYPE_CTRL_CLASS) continue;  // marker, not a value
    if (qec.flags & V4L2_CTRL_FLAG_DISABLED) {
      XLOG_DEBUG("control skip disabled: {}", qec.name);
      // Still record it so callers can show it greyed out.
    }

    ControlInfo info;
    info.id = qec.id;
    info.name = reinterpret_cast<const char*>(qec.name);
    info.type = MapType(qec.type);
    info.minimum = qec.minimum;
    info.maximum = qec.maximum;
    info.step = static_cast<int64_t>(qec.step);
    info.default_value = qec.default_value;
    info.flags = qec.flags;
    if (info.type == ControlType::kMenu ||
        info.type == ControlType::kIntegerMenu) {
      PopulateMenu(dev, qec, &info);
    }
    out->push_back(std::move(info));
    ++added;
  }
  return added;
}

}  // namespace

Status ControlIntrospector::Enumerate(V4l2Device& dev,
                                      std::vector<ControlInfo>* out) {
  if (out == nullptr)
    return Err(ErrorCode::kInvalidArgument, "Enumerate: out is null");
  if (!dev.IsOpen())
    return Err(ErrorCode::kInvalidArgument, "Enumerate: device not open");
  out->clear();

  // Preferred: walk both classic and compound controls in a single pass.
  const uint32_t kNextBoth =
      V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
  size_t n = WalkControls(dev, kNextBoth, out);

  // Fall back to a plain NEXT_CTRL walk on older kernels that reject the
  // compound flag (yielding nothing above).
  if (n == 0) {
    out->clear();
    n = WalkControls(dev, V4L2_CTRL_FLAG_NEXT_CTRL, out);
  }

  XLOG_INFO("control introspect {}: {} control(s)", dev.path(), n);
  return Ok();
}

}  // namespace xmotion
