/*
 * @file config_writer.hpp
 * @brief Serialize a CameraConfig to the YAML schema in
 *        docs/design/config-schema.md.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_EXPORT_CONFIG_WRITER_HPP
#define XMCAM_EXPORT_CONFIG_WRITER_HPP

#include <string>

#include "xmcam/core/result.hpp"
#include "xmcam/export/camera_config.hpp"

namespace xmotion {

class ConfigWriter {
 public:
  // Write `cfg` as YAML to `path`. Returns kIo on filesystem failure.
  static Status Write(const CameraConfig& cfg, const std::string& path);

  // Render `cfg` to a YAML string (never fails).
  static std::string ToYamlString(const CameraConfig& cfg);
};

}  // namespace xmotion

#endif  // XMCAM_EXPORT_CONFIG_WRITER_HPP
