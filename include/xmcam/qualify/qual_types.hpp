/*
 * @file qual_types.hpp
 * @brief Data types for the production-camera qualification checklist:
 *        per-check results, platform/firmware identity, and the aggregate
 *        report consumed by the YAML/Markdown writers and the GUI panel.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_QUALIFY_QUAL_TYPES_HPP
#define XMCAM_QUALIFY_QUAL_TYPES_HPP

#include <string>
#include <utility>
#include <vector>

namespace xmotion {

// Lifecycle / outcome of one qualification check. kActionRequired marks steps
// that need an operator action (e.g. power-cycle the camera) before re-run.
enum class QualStatus {
  kPending,
  kRunning,
  kPass,
  kFail,
  kSkipped,
  kActionRequired,
};

const char* ToString(QualStatus s);

// Result of one automated (or operator-assisted) qualification check.
struct QualCheckResult {
  std::string id;      // machine id, e.g. "exposure_lock"
  std::string title;   // human title
  QualStatus status = QualStatus::kPending;
  std::string detail;  // outcome text / failure reason / operator instruction
  std::vector<std::pair<std::string, std::string>> metrics;  // key -> value
};

// Host + camera identity gathered by CollectPlatformInfo(). Fields that cannot
// be resolved on this host are left empty rather than failing the collection.
struct PlatformInfo {
  std::string kernel;          // uname -r equivalent
  std::string os_release;      // PRETTY_NAME from /etc/os-release ("" if none)
  std::string jetpack;         // first line of /etc/nv_tegra_release ("" if not Tegra)
  std::string driver;          // driver name + version from VIDIOC_QUERYCAP
  std::string usb_vid_pid;     // "1234:abcd" ("" if not resolvable)
  std::string usb_bcd_device;  // firmware revision from sysfs bcdDevice ("" if none)
  std::string usb_serial;      // sysfs serial ("" if none)
  std::string bus_info;        // QUERYCAP bus_info
};

// Aggregate qualification report for one camera unit.
struct QualReport {
  std::string device, card, by_id;
  PlatformInfo platform;
  std::vector<QualCheckResult> results;
  std::vector<std::pair<std::string, std::string>> manual_fields;
  std::string started_at, finished_at;  // caller-supplied strings
};

}  // namespace xmotion

#endif  // XMCAM_QUALIFY_QUAL_TYPES_HPP
