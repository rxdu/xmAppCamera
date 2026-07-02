/*
 * @file camera_config.hpp
 * @brief In-memory model of an exported camera config: a source descriptor plus
 *        the control values captured at export time. Serialized to YAML by
 *        ConfigWriter and parsed back by ConfigReader (see
 *        docs/design/config-schema.md).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_EXPORT_CAMERA_CONFIG_HPP
#define XMCAM_EXPORT_CAMERA_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "xmcam/core/source_descriptor.hpp"

namespace xmotion {

// One V4L2 control value, keyed by portable name with the numeric id kept for
// exact re-apply on the same driver.
struct ControlValue {
  std::string name;
  uint32_t id = 0;
  int64_t value = 0;
};

// A complete exported camera configuration.
struct CameraConfig {
  int schema_version = 1;
  SourceDescriptor source;
  // Stable device handles alongside source.device (v4l2 only): by-id is USB-
  // serial-based (ambiguous when units share a serial — see the qualification
  // serial-uniqueness check); by-path is physical-port-based and preferred on
  // fixed wiring. Consumers should try by-path, then by-id, then device.
  std::string device_by_id;
  std::string device_by_path;
  // Human-readable card name (from VIDIOC_QUERYCAP); informational, v4l2 only.
  std::string card;
  // Explicit GStreamer pipeline (mirrors source.pipeline; kept for clarity when
  // constructing configs by hand).
  std::string pipeline;
  std::vector<ControlValue> controls;
};

}  // namespace xmotion

#endif  // XMCAM_EXPORT_CAMERA_CONFIG_HPP
