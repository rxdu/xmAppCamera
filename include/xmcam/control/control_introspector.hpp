/*
 * @file control_introspector.hpp
 * @brief Enumerates the V4L2 control set of an open device via the extended
 *        control API (VIDIOC_QUERY_EXT_CTRL walk), producing ControlInfo
 *        records. Stateless — pure query, no value read/write.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CONTROL_CONTROL_INTROSPECTOR_HPP
#define XMCAM_CONTROL_CONTROL_INTROSPECTOR_HPP

#include <vector>

#include "xmcam/control/control_info.hpp"
#include "xmcam/core/result.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"

namespace xmotion {

class ControlIntrospector {
 public:
  // Walk every user/camera control the device exposes and append it to *out
  // (cleared first). Control-class marker pseudo-controls are skipped. Menu and
  // integer-menu controls get their `menu` list populated. Returns kDeviceError
  // only on a hard ioctl failure; an empty control set is still Ok.
  static Status Enumerate(V4l2Device& dev, std::vector<ControlInfo>* out);
};

}  // namespace xmotion

#endif  // XMCAM_CONTROL_CONTROL_INTROSPECTOR_HPP
