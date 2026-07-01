/*
 * @file config_writer.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/export/config_writer.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <fstream>

#include "xmcam/core/pixel_format.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

// Control ids are serialized as a fixed-width hex *string* (e.g. "0x00980900")
// to match the schema examples and stay stable/readable across drivers. The
// reader accepts both this form and a plain decimal integer.
std::string FormatControlId(uint32_t id) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08x", id);
  return std::string(buf);
}

}  // namespace

std::string ConfigWriter::ToYamlString(const CameraConfig& cfg) {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "schema_version" << YAML::Value << cfg.schema_version;

  // --- source ---
  out << YAML::Key << "source" << YAML::Value << YAML::BeginMap;
  const bool is_v4l2 = cfg.source.type == SourceDescriptor::Type::kV4l2;
  if (is_v4l2) {
    out << YAML::Key << "type" << YAML::Value << "v4l2";
    out << YAML::Key << "device" << YAML::Value << cfg.source.device;
    if (!cfg.card.empty())
      out << YAML::Key << "card" << YAML::Value << cfg.card;
  } else {
    out << YAML::Key << "type" << YAML::Value << "gstreamer";
    if (!cfg.source.uri.empty())
      out << YAML::Key << "uri" << YAML::Value << cfg.source.uri;
    const std::string& pipeline =
        !cfg.pipeline.empty() ? cfg.pipeline : cfg.source.pipeline;
    // Emit as a normal scalar (not a block/literal scalar) so the pipeline
    // string round-trips byte-for-byte; block scalars add a trailing newline.
    if (!pipeline.empty())
      out << YAML::Key << "pipeline" << YAML::Value << pipeline;
  }
  out << YAML::EndMap;

  // --- format ---
  out << YAML::Key << "format" << YAML::Value << YAML::BeginMap;
  if (is_v4l2 && cfg.source.format != PixelFormat::kUnknown)
    out << YAML::Key << "fourcc" << YAML::Value << ToString(cfg.source.format);
  out << YAML::Key << "width" << YAML::Value << cfg.source.width;
  out << YAML::Key << "height" << YAML::Value << cfg.source.height;
  out << YAML::Key << "fps" << YAML::Value << cfg.source.fps;
  out << YAML::EndMap;

  // --- controls ---
  out << YAML::Key << "controls" << YAML::Value << YAML::BeginSeq;
  for (const auto& c : cfg.controls) {
    out << YAML::Flow << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << c.name;
    out << YAML::Key << "id" << YAML::Value << FormatControlId(c.id);
    out << YAML::Key << "value" << YAML::Value << c.value;
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;

  out << YAML::EndMap;
  return std::string(out.c_str());
}

Status ConfigWriter::Write(const CameraConfig& cfg, const std::string& path) {
  std::ofstream ofs(path, std::ios::out | std::ios::trunc);
  if (!ofs) {
    XLOG_ERROR("ConfigWriter: cannot open '{}' for writing", path);
    return Err(ErrorCode::kIo, "cannot open file for writing: " + path);
  }
  ofs << ToYamlString(cfg) << '\n';
  if (!ofs) {
    XLOG_ERROR("ConfigWriter: write failed for '{}'", path);
    return Err(ErrorCode::kIo, "write failed: " + path);
  }
  XLOG_INFO("ConfigWriter: wrote camera config to '{}' ({} controls)", path,
            cfg.controls.size());
  return Ok();
}

}  // namespace xmotion
