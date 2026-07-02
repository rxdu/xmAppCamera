/*
 * @file qual_report.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/qualify/qual_report.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>

#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

// Sanitize free text for a Markdown table cell: escape pipes, flatten newlines.
std::string MdCell(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '|')
      out += "\\|";
    else if (c == '\n' || c == '\r')
      out += ' ';
    else
      out += c;
  }
  return out;
}

void EmitPlatform(YAML::Emitter& out, const PlatformInfo& p) {
  out << YAML::Key << "platform" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "kernel" << YAML::Value << p.kernel;
  out << YAML::Key << "os_release" << YAML::Value << p.os_release;
  out << YAML::Key << "jetpack" << YAML::Value << p.jetpack;
  out << YAML::Key << "driver" << YAML::Value << p.driver;
  out << YAML::Key << "usb_vid_pid" << YAML::Value << p.usb_vid_pid;
  out << YAML::Key << "usb_bcd_device" << YAML::Value << p.usb_bcd_device;
  out << YAML::Key << "usb_serial" << YAML::Value << p.usb_serial;
  out << YAML::Key << "bus_info" << YAML::Value << p.bus_info;
  out << YAML::EndMap;
}

Status WriteToFile(const std::string& path, const std::string& content,
                   const char* kind) {
  std::ofstream ofs(path, std::ios::trunc);
  if (!ofs) return Err(ErrorCode::kIo, std::string("cannot open ") + path);
  ofs << content;
  ofs.close();
  if (!ofs)
    return Err(ErrorCode::kIo, std::string("failed writing ") + path);
  XLOG_INFO("qualify: wrote {} report to {}", kind, path);
  return Ok();
}

}  // namespace

Status WriteReportYaml(const QualReport& r, const std::string& path) {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "device" << YAML::Value << r.device;
  out << YAML::Key << "card" << YAML::Value << r.card;
  out << YAML::Key << "by_id" << YAML::Value << r.by_id;
  out << YAML::Key << "started_at" << YAML::Value << r.started_at;
  out << YAML::Key << "finished_at" << YAML::Value << r.finished_at;
  EmitPlatform(out, r.platform);

  out << YAML::Key << "results" << YAML::Value << YAML::BeginSeq;
  for (const QualCheckResult& c : r.results) {
    out << YAML::BeginMap;
    out << YAML::Key << "id" << YAML::Value << c.id;
    out << YAML::Key << "title" << YAML::Value << c.title;
    out << YAML::Key << "status" << YAML::Value << ToString(c.status);
    out << YAML::Key << "detail" << YAML::Value << c.detail;
    if (!c.metrics.empty()) {
      out << YAML::Key << "metrics" << YAML::Value << YAML::BeginMap;
      for (const auto& kv : c.metrics)
        out << YAML::Key << kv.first << YAML::Value << kv.second;
      out << YAML::EndMap;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;

  out << YAML::Key << "manual_fields" << YAML::Value << YAML::BeginMap;
  for (const auto& kv : r.manual_fields)
    out << YAML::Key << kv.first << YAML::Value << kv.second;
  out << YAML::EndMap;
  out << YAML::EndMap;

  if (!out.good())
    return Err(ErrorCode::kInternal,
               std::string("YAML emit failed: ") + out.GetLastError());
  return WriteToFile(path, std::string(out.c_str()) + "\n", "YAML");
}

Status WriteReportMarkdown(const QualReport& r, const std::string& path) {
  std::string md;
  md += "# Camera Qualification Report\n\n";
  md += "- **Device:** `" + r.device + "`\n";
  if (!r.card.empty()) md += "- **Card:** " + r.card + "\n";
  if (!r.by_id.empty()) md += "- **By-id:** `" + r.by_id + "`\n";
  if (!r.started_at.empty()) md += "- **Started:** " + r.started_at + "\n";
  if (!r.finished_at.empty()) md += "- **Finished:** " + r.finished_at + "\n";
  md += "\n## Platform\n\n";
  md += "| Field | Value |\n|---|---|\n";
  const std::pair<const char*, const std::string&> platform_rows[] = {
      {"Kernel", r.platform.kernel},
      {"OS", r.platform.os_release},
      {"JetPack", r.platform.jetpack},
      {"Driver", r.platform.driver},
      {"USB VID:PID", r.platform.usb_vid_pid},
      {"USB bcdDevice", r.platform.usb_bcd_device},
      {"USB serial", r.platform.usb_serial},
      {"Bus info", r.platform.bus_info},
  };
  for (const auto& row : platform_rows) {
    md += "| " + std::string(row.first) + " | " + MdCell(row.second) + " |\n";
  }

  md += "\n## Checks\n\n";
  md += "| Check | Status | Detail |\n|---|---|---|\n";
  for (const QualCheckResult& c : r.results) {
    md += "| " + MdCell(c.title) + " | " + ToString(c.status) + " | " +
          MdCell(c.detail) + " |\n";
  }

  bool any_metrics = false;
  for (const QualCheckResult& c : r.results)
    if (!c.metrics.empty()) any_metrics = true;
  if (any_metrics) {
    md += "\n## Metrics\n";
    for (const QualCheckResult& c : r.results) {
      if (c.metrics.empty()) continue;
      md += "\n### " + c.title + " (`" + c.id + "`)\n\n";
      for (const auto& kv : c.metrics)
        md += "- " + kv.first + ": " + kv.second + "\n";
    }
  }

  if (!r.manual_fields.empty()) {
    md += "\n## Manual Fields\n\n";
    md += "| Field | Value |\n|---|---|\n";
    for (const auto& kv : r.manual_fields)
      md += "| " + MdCell(kv.first) + " | " + MdCell(kv.second) + " |\n";
  }

  return WriteToFile(path, md, "Markdown");
}

}  // namespace xmotion
