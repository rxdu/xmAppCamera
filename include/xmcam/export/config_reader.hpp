/*
 * @file config_reader.hpp
 * @brief Parse the YAML camera-config schema back into a CameraConfig. Unknown
 *        keys are ignored (forward-compatible); a missing/mismatched
 *        schema_version logs a warning but does not fail.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_EXPORT_CONFIG_READER_HPP
#define XMCAM_EXPORT_CONFIG_READER_HPP

#include <string>

#include "xmcam/core/result.hpp"
#include "xmcam/export/camera_config.hpp"

namespace xmotion {

class ConfigReader {
 public:
  // Read and parse the YAML file at `path` into `*out`. Returns kIo if the file
  // cannot be read, kInvalidArgument on malformed YAML.
  static Status Read(const std::string& path, CameraConfig* out);

  // Parse a YAML string into `*out`. Returns kInvalidArgument on malformed YAML.
  static Status FromYamlString(const std::string& yaml, CameraConfig* out);
};

}  // namespace xmotion

#endif  // XMCAM_EXPORT_CONFIG_READER_HPP
