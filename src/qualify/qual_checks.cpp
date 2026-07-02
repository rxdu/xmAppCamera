/*
 * @file qual_checks.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/qualify/qual_checks.hpp"

#include <dirent.h>
#include <linux/videodev2.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

#include "xmcam/core/pixel_format.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

// Sampling cadence for the control-lock hold window.
constexpr int kLockSampleIntervalMs = 100;
// Warm-up deltas discarded from the head of the pts series.
constexpr int kWarmupDeltas = 5;
// Minimum sample counts below which checks are skipped rather than judged.
constexpr size_t kMinTimestampSamples = 30;
constexpr uint64_t kMinFingerprintFrames = 10;
// Timestamp pass thresholds (fractions of the nominal frame period).
constexpr double kMaxStddevRatio = 0.20;
constexpr double kMaxDeltaRatio = 3.0;

std::string FormatDouble(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.3f", v);
  return std::string(buf);
}

// Read a whole small file, trimming trailing whitespace/newlines. Empty string
// if the file does not exist or cannot be read.
std::string ReadFileTrimmed(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) return {};
  std::string content((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
  while (!content.empty() &&
         std::isspace(static_cast<unsigned char>(content.back())))
    content.pop_back();
  return content;
}

// First line of a file ("" if absent).
std::string ReadFirstLine(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) return {};
  std::string line;
  std::getline(ifs, line);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
    line.pop_back();
  return line;
}

// PRETTY_NAME from /etc/os-release, unquoted ("" if absent).
std::string ReadOsPrettyName() {
  std::ifstream ifs("/etc/os-release");
  if (!ifs) return {};
  std::string line;
  while (std::getline(ifs, line)) {
    const std::string key = "PRETTY_NAME=";
    if (line.compare(0, key.size(), key) != 0) continue;
    std::string value = line.substr(key.size());
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
      value = value.substr(1, value.size() - 2);
    return value;
  }
  return {};
}

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

// Resolve the USB identity (vid:pid, bcdDevice, serial) of the camera by
// walking /sys/bus/usb/devices/*/ and matching each device's product (or
// manufacturer) string against the V4L2 card string. bus_info encodes only
// the port path (not the bus number), so the descriptor-string match is the
// more reliable simple strategy; if nothing matches the usb_* fields stay
// empty by design.
void ResolveUsbIdentity(const std::string& card, PlatformInfo* out) {
  if (card.empty()) return;
  const char* base = "/sys/bus/usb/devices";
  DIR* d = ::opendir(base);
  if (!d) return;
  const std::string card_lower = ToLower(card);
  for (dirent* e = ::readdir(d); e; e = ::readdir(d)) {
    const std::string name = e->d_name;
    // Device dirs look like "3-2" or "1-2.4"; skip root hubs ("usb1"),
    // interface dirs ("3-2:1.0") and dot entries.
    if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0])))
      continue;
    if (name.find('-') == std::string::npos ||
        name.find(':') != std::string::npos)
      continue;
    const std::string dir = std::string(base) + "/" + name;
    const std::string product = ReadFileTrimmed(dir + "/product");
    const std::string manufacturer = ReadFileTrimmed(dir + "/manufacturer");
    const std::string product_lower = ToLower(product);
    const std::string manufacturer_lower = ToLower(manufacturer);
    const bool product_match =
        !product_lower.empty() &&
        (card_lower.find(product_lower) != std::string::npos ||
         product_lower.find(card_lower) != std::string::npos);
    const bool manufacturer_match =
        !manufacturer_lower.empty() &&
        card_lower.find(manufacturer_lower) != std::string::npos;
    if (!product_match && !manufacturer_match) continue;

    const std::string vid = ReadFileTrimmed(dir + "/idVendor");
    const std::string pid = ReadFileTrimmed(dir + "/idProduct");
    if (!vid.empty() && !pid.empty()) out->usb_vid_pid = vid + ":" + pid;
    out->usb_bcd_device = ReadFileTrimmed(dir + "/bcdDevice");
    out->usb_serial = ReadFileTrimmed(dir + "/serial");
    XLOG_DEBUG("qualify: matched USB device {} ({}) for card '{}'", name,
               out->usb_vid_pid, card);
    break;
  }
  ::closedir(d);
}

// Human-readable name for a format entry: the fourcc characters when printable,
// otherwise the PixelFormat name.
std::string FormatName(const FormatDesc& fmt) {
  if (fmt.fourcc != 0) {
    char s[5] = {static_cast<char>(fmt.fourcc & 0xff),
                 static_cast<char>((fmt.fourcc >> 8) & 0xff),
                 static_cast<char>((fmt.fourcc >> 16) & 0xff),
                 static_cast<char>((fmt.fourcc >> 24) & 0xff), '\0'};
    bool printable = true;
    for (int i = 0; i < 4; ++i) {
      if (!std::isprint(static_cast<unsigned char>(s[i]))) printable = false;
    }
    if (printable) return std::string(s);
  }
  return ToString(fmt.format);
}

}  // namespace

Status CollectPlatformInfo(const std::string& device, PlatformInfo* out) {
  if (out == nullptr)
    return Err(ErrorCode::kInvalidArgument, "PlatformInfo output is null");
  *out = PlatformInfo{};

  utsname un{};
  if (::uname(&un) == 0) out->kernel = un.release;
  out->os_release = ReadOsPrettyName();
  out->jetpack = ReadFirstLine("/etc/nv_tegra_release");

  V4l2Device dev;
  Status s = dev.Open(device);
  if (!s.ok()) {
    XLOG_WARN("qualify: cannot open {} for platform info: {}", device,
              s.message());
    return s;
  }

  v4l2_capability cap{};
  if (dev.Ioctl(VIDIOC_QUERYCAP, &cap) != 0) {
    return Err(ErrorCode::kDeviceError,
               "VIDIOC_QUERYCAP failed on " + device + ": " +
                   std::strerror(errno));
  }
  const uint32_t v = cap.version;
  char driver_buf[64];
  std::snprintf(driver_buf, sizeof(driver_buf), "%s %u.%u.%u",
                reinterpret_cast<const char*>(cap.driver), (v >> 16) & 0xffu,
                (v >> 8) & 0xffu, v & 0xffu);
  out->driver = driver_buf;
  out->bus_info = reinterpret_cast<const char*>(cap.bus_info);

  const std::string card = reinterpret_cast<const char*>(cap.card);
  ResolveUsbIdentity(card, out);

  XLOG_INFO("qualify: platform info for {}: kernel={} driver={} usb={}",
            device, out->kernel, out->driver,
            out->usb_vid_pid.empty() ? "n/a" : out->usb_vid_pid);
  return Ok();
}

QualCheckResult CheckEnumeration(const SourceCaps& caps,
                                 const std::vector<ControlInfo>& controls) {
  QualCheckResult r;
  r.id = "enumeration";
  r.title = "Format & control enumeration";
  r.metrics.emplace_back("n_formats", std::to_string(caps.formats.size()));
  r.metrics.emplace_back("n_controls", std::to_string(controls.size()));
  for (const FormatDesc& fmt : caps.formats) {
    r.metrics.emplace_back("fmt_" + FormatName(fmt),
                           std::to_string(fmt.sizes.size()) + " sizes");
  }
  if (caps.empty() || controls.empty()) {
    r.status = QualStatus::kFail;
    r.detail = caps.empty() ? "no formats enumerated" : "no controls enumerated";
    return r;
  }
  r.status = QualStatus::kPass;
  r.detail = std::to_string(caps.formats.size()) + " formats, " +
             std::to_string(controls.size()) + " controls enumerated";
  return r;
}

QualCheckResult CheckControlLock(ControlSet& cs, const std::string& check_id,
                                 const std::string& title,
                                 uint32_t auto_ctrl_id,
                                 int64_t auto_manual_value,
                                 uint32_t target_ctrl_id, int64_t target_value,
                                 int hold_ms) {
  QualCheckResult r;
  r.id = check_id;
  r.title = title;

  const ControlInfo* target = cs.Find(target_ctrl_id);
  if (target == nullptr) {
    r.status = QualStatus::kSkipped;
    r.detail = "target control not present on this device";
    return r;
  }
  if (auto_ctrl_id != 0 && cs.Find(auto_ctrl_id) == nullptr) {
    r.status = QualStatus::kSkipped;
    r.detail = "auto-mode control not present on this device";
    return r;
  }

  // Original values, read before any writes so we can restore.
  int64_t orig_target = 0;
  const bool have_orig_target = cs.Get(target_ctrl_id, &orig_target).ok();
  int64_t orig_auto = 0;
  bool have_orig_auto = false;
  if (auto_ctrl_id != 0) have_orig_auto = cs.Get(auto_ctrl_id, &orig_auto).ok();

  // Restore target first (still writable while manual mode is active), then
  // the auto control, which may re-inactivate the target.
  auto restore = [&]() {
    if (have_orig_target) {
      Status rs = cs.Set(target_ctrl_id, orig_target);
      if (!rs.ok())
        XLOG_WARN("qualify[{}]: failed to restore target control: {}",
                  check_id, rs.message());
    }
    if (auto_ctrl_id != 0 && have_orig_auto) {
      Status rs = cs.Set(auto_ctrl_id, orig_auto);
      if (!rs.ok())
        XLOG_WARN("qualify[{}]: failed to restore auto control: {}", check_id,
                  rs.message());
    }
  };

  // Step 1: force manual mode and verify the target becomes active.
  if (auto_ctrl_id != 0) {
    Status s = cs.Set(auto_ctrl_id, auto_manual_value);
    if (!s.ok()) {
      r.status = QualStatus::kFail;
      r.detail = "failed to switch to manual mode: " + s.message();
      restore();
      return r;
    }
    target = cs.Find(target_ctrl_id);  // Set() re-introspected the flags
    if (target != nullptr && target->IsInactive()) {
      r.status = QualStatus::kFail;
      r.detail = "target control still INACTIVE after disabling auto mode";
      restore();
      return r;
    }
  }

  // Step 2: write the manual value.
  Status s = cs.Set(target_ctrl_id, target_value);
  if (!s.ok()) {
    r.status = QualStatus::kFail;
    r.detail = "failed to set target value: " + s.message();
    restore();
    return r;
  }

  // Step 3: hold and verify.
  const int n_samples = std::max(1, hold_ms / kLockSampleIntervalMs);
  bool volatile_seen = false;
  for (int i = 0; i < n_samples; ++i) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLockSampleIntervalMs));
    int64_t value = 0;
    Status g = cs.Get(target_ctrl_id, &value);
    if (!g.ok()) {
      r.status = QualStatus::kFail;
      r.detail = "read-back failed at sample " + std::to_string(i) + ": " +
                 g.message();
      restore();
      return r;
    }
    if (value != target_value) {
      r.status = QualStatus::kFail;
      r.detail = "value drifted to " + std::to_string(value) + " (expected " +
                 std::to_string(target_value) + ") at sample " +
                 std::to_string(i);
      r.metrics.emplace_back("samples", std::to_string(i + 1));
      r.metrics.emplace_back("held_value", std::to_string(value));
      restore();
      return r;
    }
    const ControlInfo* info = cs.Find(target_ctrl_id);
    if (info != nullptr && info->IsVolatile()) volatile_seen = true;
  }

  r.metrics.emplace_back("samples", std::to_string(n_samples));
  r.metrics.emplace_back("held_value", std::to_string(target_value));
  r.metrics.emplace_back("volatile_flag", volatile_seen ? "true" : "false");

  if (volatile_seen) {
    r.status = QualStatus::kFail;
    r.detail = "control reports VOLATILE flag (driver may auto-adjust it)";
  } else {
    r.status = QualStatus::kPass;
    r.detail = "value held at " + std::to_string(target_value) + " for " +
               std::to_string(hold_ms) + " ms (" + std::to_string(n_samples) +
               " samples)";
  }

  // Step 4: restore original state.
  restore();
  return r;
}

QualCheckResult CheckTimestampStability(const std::vector<int64_t>& pts_ns,
                                        double nominal_fps) {
  QualCheckResult r;
  r.id = "timestamp_stability";
  r.title = "Timestamp stability";

  if (pts_ns.size() < kMinTimestampSamples) {
    r.status = QualStatus::kSkipped;
    r.detail = "need at least " + std::to_string(kMinTimestampSamples) +
               " samples, got " + std::to_string(pts_ns.size());
    return r;
  }
  if (nominal_fps <= 0.0) {
    r.status = QualStatus::kSkipped;
    r.detail = "invalid nominal fps (" + FormatDouble(nominal_fps) + ")";
    return r;
  }

  // Deltas in ms, discarding the pipeline warm-up.
  std::vector<double> deltas_ms;
  deltas_ms.reserve(pts_ns.size() - 1);
  bool monotonic = true;
  for (size_t i = static_cast<size_t>(kWarmupDeltas) + 1; i < pts_ns.size();
       ++i) {
    const int64_t delta = pts_ns[i] - pts_ns[i - 1];
    if (delta <= 0) monotonic = false;
    deltas_ms.push_back(static_cast<double>(delta) / 1e6);
  }

  double mean = 0;
  for (double d : deltas_ms) mean += d;
  mean /= static_cast<double>(deltas_ms.size());
  double var = 0;
  for (double d : deltas_ms) var += (d - mean) * (d - mean);
  var /= static_cast<double>(deltas_ms.size());
  const double stddev = std::sqrt(var);
  const double min_ms =
      *std::min_element(deltas_ms.begin(), deltas_ms.end());
  const double max_ms =
      *std::max_element(deltas_ms.begin(), deltas_ms.end());
  const double nominal_ms = 1000.0 / nominal_fps;

  r.metrics.emplace_back("n", std::to_string(deltas_ms.size()));
  r.metrics.emplace_back("mean_ms", FormatDouble(mean));
  r.metrics.emplace_back("stddev_ms", FormatDouble(stddev));
  r.metrics.emplace_back("min_ms", FormatDouble(min_ms));
  r.metrics.emplace_back("max_ms", FormatDouble(max_ms));
  r.metrics.emplace_back("monotonic", monotonic ? "true" : "false");
  r.metrics.emplace_back("nominal_ms", FormatDouble(nominal_ms));

  if (!monotonic) {
    r.status = QualStatus::kFail;
    r.detail = "timestamps are not monotonically increasing";
  } else if (stddev >= kMaxStddevRatio * nominal_ms) {
    r.status = QualStatus::kFail;
    r.detail = "jitter too high: stddev " + FormatDouble(stddev) +
               " ms >= 20% of nominal " + FormatDouble(nominal_ms) + " ms";
  } else if (max_ms >= kMaxDeltaRatio * nominal_ms) {
    r.status = QualStatus::kFail;
    r.detail = "frame gap too large: max delta " + FormatDouble(max_ms) +
               " ms >= 3x nominal " + FormatDouble(nominal_ms) + " ms";
  } else {
    r.status = QualStatus::kPass;
    r.detail = "stable: mean " + FormatDouble(mean) + " ms, stddev " +
               FormatDouble(stddev) + " ms over " +
               std::to_string(deltas_ms.size()) + " intervals";
  }
  return r;
}

QualCheckResult CompareImageFingerprint(const FrameTap::Snapshot& before,
                                        const FrameTap::Snapshot& after,
                                        double tolerance_pct) {
  QualCheckResult r;
  r.id = "image_fingerprint";
  r.title = "Image fingerprint compare";

  if (before.frames < kMinFingerprintFrames ||
      after.frames < kMinFingerprintFrames) {
    r.status = QualStatus::kSkipped;
    r.detail = "need at least " + std::to_string(kMinFingerprintFrames) +
               " frames in each snapshot (got " +
               std::to_string(before.frames) + " / " +
               std::to_string(after.frames) + ")";
    return r;
  }

  const double tol = tolerance_pct / 100.0 * 255.0;
  const double dy = std::fabs(after.mean_y - before.mean_y);
  const double du = std::fabs(after.mean_u - before.mean_u);
  const double dv = std::fabs(after.mean_v - before.mean_v);
  const bool dims_match =
      before.width == after.width && before.height == after.height;

  r.metrics.emplace_back("before_mean_y", FormatDouble(before.mean_y));
  r.metrics.emplace_back("before_mean_u", FormatDouble(before.mean_u));
  r.metrics.emplace_back("before_mean_v", FormatDouble(before.mean_v));
  r.metrics.emplace_back("after_mean_y", FormatDouble(after.mean_y));
  r.metrics.emplace_back("after_mean_u", FormatDouble(after.mean_u));
  r.metrics.emplace_back("after_mean_v", FormatDouble(after.mean_v));
  r.metrics.emplace_back("delta_y", FormatDouble(dy));
  r.metrics.emplace_back("delta_u", FormatDouble(du));
  r.metrics.emplace_back("delta_v", FormatDouble(dv));
  r.metrics.emplace_back("tolerance", FormatDouble(tol));

  if (!dims_match) {
    r.status = QualStatus::kFail;
    r.detail = "frame size changed: " + std::to_string(before.width) + "x" +
               std::to_string(before.height) + " -> " +
               std::to_string(after.width) + "x" +
               std::to_string(after.height);
  } else if (dy > tol || du > tol || dv > tol) {
    r.status = QualStatus::kFail;
    r.detail = "plane mean drift exceeds " + FormatDouble(tolerance_pct) +
               "% of 255 (dY=" + FormatDouble(dy) + " dU=" + FormatDouble(du) +
               " dV=" + FormatDouble(dv) + ", tol=" + FormatDouble(tol) + ")";
  } else {
    r.status = QualStatus::kPass;
    r.detail = "image fingerprint matches within " +
               FormatDouble(tolerance_pct) + "% tolerance";
  }
  return r;
}

}  // namespace xmotion
