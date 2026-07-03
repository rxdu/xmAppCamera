/*
 * @file config_reader.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/export/config_reader.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "xmcam/core/pixel_format.hpp"
#include "xmbase/logging/xlogger.hpp"

namespace xmotion {
namespace {

// Local parse-from-string for the fourcc strings ToString() emits. Kept in this
// module on purpose: core/pixel_format only exposes ToString(), and the export
// layer owns the wire format. Case-insensitive on the canonical spellings.
PixelFormat PixelFormatFromString(const std::string& s) {
  if (s == "GRAY8") return PixelFormat::kGray8;
  if (s == "RGB8") return PixelFormat::kRgb8;
  if (s == "RGBA8") return PixelFormat::kRgba8;
  if (s == "BGR8") return PixelFormat::kBgr8;
  if (s == "BGRA8") return PixelFormat::kBgra8;
  if (s == "YUYV") return PixelFormat::kYuyv;
  if (s == "NV12") return PixelFormat::kNv12;
  if (s == "I420") return PixelFormat::kI420;
  if (s == "MJPEG") return PixelFormat::kMjpeg;
  if (s == "H264") return PixelFormat::kH264;
  return PixelFormat::kUnknown;
}

// Parse a control id that may be a hex string ("0x00980900"), a decimal string,
// or a plain YAML integer. Returns false if it is not parseable.
bool ParseControlId(const YAML::Node& node, uint32_t* out) {
  const std::string s = node.as<std::string>("");
  if (s.empty()) return false;
  errno = 0;
  char* end = nullptr;
  // Base 0 -> auto-detect 0x hex vs decimal.
  const unsigned long long v = std::strtoull(s.c_str(), &end, 0);
  if (end == s.c_str() || *end != '\0' || errno != 0) return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

// Parse a control value: integer, or a boolean (true->1, false->0).
bool ParseControlValue(const YAML::Node& node, int64_t* out) {
  try {
    *out = node.as<int64_t>();
    return true;
  } catch (const YAML::Exception&) {
    // fall through to bool
  }
  try {
    *out = node.as<bool>() ? 1 : 0;
    return true;
  } catch (const YAML::Exception&) {
    return false;
  }
}

Status ParseRoot(const YAML::Node& root, CameraConfig* out) {
  if (!root.IsMap())
    return Err(ErrorCode::kInvalidArgument,
               "config root is not a YAML mapping");

  CameraConfig cfg;

  if (root["schema_version"]) {
    cfg.schema_version = root["schema_version"].as<int>(cfg.schema_version);
    if (cfg.schema_version != 1)
      XLOG_WARN("ConfigReader: schema_version {} != 1 (parsing best-effort)",
                cfg.schema_version);
  } else {
    XLOG_WARN("ConfigReader: missing schema_version (assuming {})",
              cfg.schema_version);
  }

  const YAML::Node source = root["source"];
  if (source && source.IsMap()) {
    const std::string type = source["type"].as<std::string>("v4l2");
    if (type == "gstreamer") {
      cfg.source.type = SourceDescriptor::Type::kGstreamer;
      cfg.source.uri = source["uri"].as<std::string>("");
      cfg.pipeline = source["pipeline"].as<std::string>("");
      cfg.source.pipeline = cfg.pipeline;
    } else {
      if (type != "v4l2")
        XLOG_WARN("ConfigReader: unknown source.type '{}', treating as v4l2",
                  type);
      cfg.source.type = SourceDescriptor::Type::kV4l2;
      cfg.source.device = source["device"].as<std::string>("");
      cfg.device_by_path = source["device_by_path"].as<std::string>("");
      cfg.device_by_id = source["device_by_id"].as<std::string>("");
      cfg.card = source["card"].as<std::string>("");
    }
  }

  const YAML::Node format = root["format"];
  if (format && format.IsMap()) {
    if (format["fourcc"])
      cfg.source.format =
          PixelFormatFromString(format["fourcc"].as<std::string>(""));
    cfg.source.width = format["width"].as<int>(0);
    cfg.source.height = format["height"].as<int>(0);
    cfg.source.fps = format["fps"].as<double>(0.0);
  }

  const YAML::Node controls = root["controls"];
  if (controls && controls.IsSequence()) {
    for (const auto& node : controls) {
      if (!node.IsMap()) continue;
      ControlValue cv;
      cv.name = node["name"].as<std::string>("");
      if (node["id"] && !ParseControlId(node["id"], &cv.id))
        XLOG_WARN("ConfigReader: control '{}' has unparseable id", cv.name);
      if (node["value"] && !ParseControlValue(node["value"], &cv.value))
        XLOG_WARN("ConfigReader: control '{}' has unparseable value", cv.name);
      cfg.controls.push_back(std::move(cv));
    }
  }

  *out = std::move(cfg);
  return Ok();
}

}  // namespace

Status ConfigReader::FromYamlString(const std::string& yaml,
                                    CameraConfig* out) {
  if (out == nullptr)
    return Err(ErrorCode::kInvalidArgument, "output pointer is null");
  try {
    const YAML::Node root = YAML::Load(yaml);
    return ParseRoot(root, out);
  } catch (const YAML::Exception& e) {
    XLOG_ERROR("ConfigReader: malformed YAML: {}", e.what());
    return Err(ErrorCode::kInvalidArgument,
               std::string("malformed YAML: ") + e.what());
  }
}

Status ConfigReader::Read(const std::string& path, CameraConfig* out) {
  if (out == nullptr)
    return Err(ErrorCode::kInvalidArgument, "output pointer is null");
  try {
    const YAML::Node root = YAML::LoadFile(path);
    Status st = ParseRoot(root, out);
    if (st.ok())
      XLOG_INFO("ConfigReader: loaded camera config from '{}' ({} controls)",
                path, out->controls.size());
    return st;
  } catch (const YAML::BadFile& e) {
    XLOG_ERROR("ConfigReader: cannot open '{}': {}", path, e.what());
    return Err(ErrorCode::kIo, std::string("cannot read file: ") + path);
  } catch (const YAML::Exception& e) {
    XLOG_ERROR("ConfigReader: malformed YAML in '{}': {}", path, e.what());
    return Err(ErrorCode::kInvalidArgument,
               std::string("malformed YAML: ") + e.what());
  }
}

}  // namespace xmotion
