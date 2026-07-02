/*
 * @file qual_report.hpp
 * @brief Writers for the qualification report: machine-readable YAML and a
 *        human-readable Markdown summary.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_QUALIFY_QUAL_REPORT_HPP
#define XMCAM_QUALIFY_QUAL_REPORT_HPP

#include <string>

#include "xmcam/core/result.hpp"
#include "xmcam/qualify/qual_types.hpp"

namespace xmotion {

// Write the report as YAML (device/platform identity, per-check results with
// metrics, manual fields).
Status WriteReportYaml(const QualReport& r, const std::string& path);

// Write a human-readable Markdown report: header (device/platform), a
// |check|status|detail| table, per-check metrics sub-lists, and the manual
// fields section.
Status WriteReportMarkdown(const QualReport& r, const std::string& path);

}  // namespace xmotion

#endif  // XMCAM_QUALIFY_QUAL_REPORT_HPP
