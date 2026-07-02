/*
 * @file qual_checks.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/qualify/qual_checks.hpp"

#include <dirent.h>
#include <linux/videodev2.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
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
// Control-effect sweep: number of log-spaced points and the tolerance under
// which a step down in mean_y still counts as a (saturation) plateau.
constexpr int kSweepPoints = 4;
constexpr double kEffectEpsilonY = 1.0;
// Soak pass thresholds.
constexpr uint64_t kMinSoakFrames = 100;
constexpr double kSoakGapRatio = 0.001;         // hw_seq gaps vs frames
constexpr double kSoakStddevRatio = 0.20;       // interval jitter vs nominal
constexpr double kSoakBucketTolerance = 0.15;   // per-10s bucket vs mean
constexpr long kSoakMaxRssGrowthKb = 10240;
// USB link audit: below this sysfs `speed` (Mbps) the link is USB2.
constexpr double kUsbSuperSpeedMbps = 5000.0;
// Write->effect latency check timing.
constexpr int kLatencySettleMs = 800;
constexpr int kLatencyTimeoutMs = 3000;
constexpr int kLatencyPollMs = 20;

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
    out->usb_sysfs_path = dir;
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

// Log-spaced sweep values across the photometrically useful part of an
// integer control's range: kSweepPoints points in [max(minimum, maximum/512)
// .. maximum/4], snapped to the control's step, clamped and deduplicated
// (ascending). Fewer than 3 distinct values means the range is too narrow to
// prove an effect.
std::vector<int64_t> DeriveSweepValues(const ControlInfo& info) {
  std::vector<int64_t> out;
  if (info.maximum <= 0) return out;
  const double lo = std::max(static_cast<double>(info.minimum),
                             static_cast<double>(info.maximum) / 512.0);
  const double hi = static_cast<double>(info.maximum) / 4.0;
  if (lo <= 0.0 || lo > hi) return out;
  for (int i = 0; i < kSweepPoints; ++i) {
    const double t = static_cast<double>(i) / (kSweepPoints - 1);
    int64_t v = std::llround(lo * std::pow(hi / lo, t));
    if (info.step > 1)
      v = info.minimum + (v - info.minimum) / info.step * info.step;
    v = std::max(info.minimum, std::min(info.maximum, v));
    if (out.empty() || out.back() != v) out.push_back(v);
  }
  return out;
}

// Count of '.' characters, i.e. the depth of a USB devpath like "3-2.4".
size_t UsbPathDepth(const std::string& name) {
  return static_cast<size_t>(std::count(name.begin(), name.end(), '.'));
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

QualCheckResult CheckControlEffect(ControlSet& cs, FrameTap& tap,
                                   const std::string& id,
                                   const std::string& title, uint32_t auto_ctrl,
                                   int64_t auto_manual, uint32_t target_ctrl,
                                   int settle_ms, int sample_ms,
                                   double min_delta) {
  QualCheckResult r;
  r.id = id;
  r.title = title;

  const ControlInfo* target = cs.Find(target_ctrl);
  if (target == nullptr) {
    r.status = QualStatus::kSkipped;
    r.detail = "target control not present on this device";
    return r;
  }
  if (auto_ctrl != 0 && cs.Find(auto_ctrl) == nullptr) {
    r.status = QualStatus::kSkipped;
    r.detail = "auto-mode control not present on this device";
    return r;
  }
  // Copy the range now: any Set() re-introspects and invalidates `target`.
  const ControlInfo range = *target;

  const std::vector<int64_t> sweep = DeriveSweepValues(range);
  if (sweep.size() < 3) {
    r.status = QualStatus::kSkipped;
    r.detail = "control range too narrow for a log-spaced sweep";
    return r;
  }

  // Original values, read before any writes so we can restore.
  int64_t orig_target = 0;
  const bool have_orig_target = cs.Get(target_ctrl, &orig_target).ok();
  int64_t orig_auto = 0;
  bool have_orig_auto = false;
  if (auto_ctrl != 0) have_orig_auto = cs.Get(auto_ctrl, &orig_auto).ok();

  // Restore target first (still writable while manual mode is active), then
  // the auto control, which may re-inactivate the target.
  auto restore = [&]() {
    if (have_orig_target) {
      Status rs = cs.Set(target_ctrl, orig_target);
      if (!rs.ok())
        XLOG_WARN("qualify[{}]: failed to restore target control: {}", id,
                  rs.message());
    }
    if (auto_ctrl != 0 && have_orig_auto) {
      Status rs = cs.Set(auto_ctrl, orig_auto);
      if (!rs.ok())
        XLOG_WARN("qualify[{}]: failed to restore auto control: {}", id,
                  rs.message());
    }
  };

  if (auto_ctrl != 0) {
    Status s = cs.Set(auto_ctrl, auto_manual);
    if (!s.ok()) {
      r.status = QualStatus::kFail;
      r.detail = "failed to switch to manual mode: " + s.message();
      restore();
      return r;
    }
  }

  // Sweep ascending: settle after each write, then sample a fresh window.
  std::vector<double> means;
  means.reserve(sweep.size());
  for (int64_t v : sweep) {
    Status s = cs.Set(target_ctrl, v);
    if (!s.ok()) {
      r.status = QualStatus::kFail;
      r.detail = "failed to set value " + std::to_string(v) + ": " +
                 s.message();
      restore();
      return r;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
    tap.Reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(sample_ms));
    const double mean_y = tap.Take().mean_y;
    means.push_back(mean_y);
    r.metrics.emplace_back("v_" + std::to_string(v), FormatDouble(mean_y));
  }
  restore();

  const double delta = means.back() - means.front();
  r.metrics.emplace_back("delta", FormatDouble(delta));

  for (size_t i = 1; i < means.size(); ++i) {
    if (means[i] < means[i - 1] - kEffectEpsilonY) {
      r.status = QualStatus::kFail;
      r.detail = "mean_y decreased across sweep step " + std::to_string(i) +
                 " (" + FormatDouble(means[i - 1]) + " -> " +
                 FormatDouble(means[i]) + ")";
      return r;
    }
  }
  if (delta < min_delta) {
    r.status = QualStatus::kFail;
    r.detail = "insufficient photometric response: delta " +
               FormatDouble(delta) + " < required " + FormatDouble(min_delta);
    return r;
  }
  r.status = QualStatus::kPass;
  r.detail = "mean_y rose monotonically by " + FormatDouble(delta) + " over " +
             std::to_string(sweep.size()) + " sweep values";
  return r;
}

QualCheckResult CheckSoak(const FrameTap::Snapshot& s, double nominal_fps,
                          long rss_kb_before, long rss_kb_after,
                          int duration_s) {
  QualCheckResult r;
  r.id = "soak";
  r.title = "Soak stability";

  if (s.frames < kMinSoakFrames) {
    r.status = QualStatus::kSkipped;
    r.detail = "need at least " + std::to_string(kMinSoakFrames) +
               " frames, got " + std::to_string(s.frames);
    return r;
  }

  // Bucket statistics, excluding a possibly-partial last bucket.
  std::vector<uint32_t> full_buckets = s.per10s_frames;
  if (!full_buckets.empty()) full_buckets.pop_back();
  uint32_t bucket_min = 0, bucket_max = 0;
  double bucket_mean = 0;
  if (!full_buckets.empty()) {
    bucket_min = *std::min_element(full_buckets.begin(), full_buckets.end());
    bucket_max = *std::max_element(full_buckets.begin(), full_buckets.end());
    uint64_t sum = 0;
    for (uint32_t b : full_buckets) sum += b;
    bucket_mean =
        static_cast<double>(sum) / static_cast<double>(full_buckets.size());
  }
  const long rss_growth_kb = rss_kb_after - rss_kb_before;

  r.metrics.emplace_back("duration_s", std::to_string(duration_s));
  r.metrics.emplace_back("frames", std::to_string(s.frames));
  r.metrics.emplace_back("gaps", std::to_string(s.hw_seq_gaps));
  r.metrics.emplace_back("stuck", std::to_string(s.stuck_frames));
  r.metrics.emplace_back("black", std::to_string(s.black_frames));
  r.metrics.emplace_back("white", std::to_string(s.white_frames));
  r.metrics.emplace_back("fps_min_bucket", FormatDouble(bucket_min / 10.0));
  r.metrics.emplace_back("fps_max_bucket", FormatDouble(bucket_max / 10.0));
  r.metrics.emplace_back("rss_growth_kb", std::to_string(rss_growth_kb));
  r.metrics.emplace_back("interval_mean_ms", FormatDouble(s.interval_mean_ms));
  r.metrics.emplace_back("interval_stddev_ms",
                         FormatDouble(s.interval_stddev_ms));
  r.metrics.emplace_back("interval_min_ms", FormatDouble(s.interval_min_ms));
  r.metrics.emplace_back("interval_max_ms", FormatDouble(s.interval_max_ms));

  // Judge the clauses in order; the detail names the first one that fails.
  const double max_gaps = kSoakGapRatio * static_cast<double>(s.frames);
  if (static_cast<double>(s.hw_seq_gaps) > max_gaps) {
    r.status = QualStatus::kFail;
    r.detail = "dropped frames: " + std::to_string(s.hw_seq_gaps) +
               " hw sequence gaps > 0.1% of " + std::to_string(s.frames) +
               " frames";
    return r;
  }
  if (s.stuck_frames > 0) {
    r.status = QualStatus::kFail;
    r.detail = std::to_string(s.stuck_frames) +
               " stuck frame(s): identical subsampled checksum repeated";
    return r;
  }
  if (s.black_frames + s.white_frames > 0) {
    r.status = QualStatus::kFail;
    r.detail = std::to_string(s.black_frames) + " black / " +
               std::to_string(s.white_frames) + " white frame(s) observed";
    return r;
  }
  if (nominal_fps > 0.0) {
    const double nominal_ms = 1000.0 / nominal_fps;
    if (s.interval_stddev_ms >= kSoakStddevRatio * nominal_ms) {
      r.status = QualStatus::kFail;
      r.detail = "interval jitter too high: stddev " +
                 FormatDouble(s.interval_stddev_ms) + " ms >= 20% of nominal " +
                 FormatDouble(nominal_ms) + " ms";
      return r;
    }
  }
  for (size_t i = 0; i < full_buckets.size(); ++i) {
    if (std::fabs(full_buckets[i] - bucket_mean) >
        kSoakBucketTolerance * bucket_mean) {
      r.status = QualStatus::kFail;
      r.detail = "frame rate sagged in 10s bucket " + std::to_string(i) +
                 ": " + std::to_string(full_buckets[i]) +
                 " frames vs mean " + FormatDouble(bucket_mean);
      return r;
    }
  }
  if (rss_growth_kb >= kSoakMaxRssGrowthKb) {
    r.status = QualStatus::kFail;
    r.detail = "RSS grew by " + std::to_string(rss_growth_kb) + " KB (limit " +
               std::to_string(kSoakMaxRssGrowthKb) + " KB)";
    return r;
  }

  r.status = QualStatus::kPass;
  r.detail = std::to_string(s.frames) + " frames over " +
             std::to_string(duration_s) +
             " s with no gaps, stuck/black/white frames, rate sag or RSS "
             "growth";
  return r;
}

long ReadRssKb() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    const std::string key = "VmRSS:";
    if (line.compare(0, key.size(), key) != 0) continue;
    return std::atol(line.c_str() + key.size());
  }
  // Fallback: /proc/self/statm field 2 is the resident set in pages.
  std::ifstream statm("/proc/self/statm");
  long size_pages = 0, resident_pages = 0;
  if (statm >> size_pages >> resident_pages)
    return resident_pages * (::sysconf(_SC_PAGESIZE) / 1024);
  return 0;
}

QualCheckResult CheckSerialUniqueness(
    const PlatformInfo& pi, const std::vector<std::string>& all_serials) {
  QualCheckResult r;
  r.id = "serial_uniqueness";
  r.title = "USB serial uniqueness";

  if (pi.usb_serial.empty()) {
    r.status = QualStatus::kFail;
    r.detail =
        "no USB serial descriptor - /dev/v4l/by-id cannot distinguish units";
    return r;
  }
  r.metrics.emplace_back("serial", pi.usb_serial);

  // "01.00.00"-style strings are firmware versions baked into the descriptor,
  // shared by every unit of the model.
  static const std::regex kVersionLike("^[0-9]+([._][0-9]+)+$");
  if (std::regex_match(pi.usb_serial, kVersionLike)) {
    r.status = QualStatus::kFail;
    r.detail = "serial \"" + pi.usb_serial +
               "\" looks like a firmware version string - likely shared "
               "across units";
    return r;
  }

  const long occurrences =
      std::count(all_serials.begin(), all_serials.end(), pi.usb_serial);
  r.metrics.emplace_back("occurrences", std::to_string(occurrences));
  if (occurrences > 1) {
    r.status = QualStatus::kFail;
    r.detail = "serial \"" + pi.usb_serial + "\" appears " +
               std::to_string(occurrences) + " times among attached cameras";
    return r;
  }

  r.status = QualStatus::kPass;
  r.detail = "serial \"" + pi.usb_serial +
             "\" is present, unit-like and unique among attached cameras";
  return r;
}

QualCheckResult CheckUsbLink(const PlatformInfo& pi) {
  QualCheckResult r;
  r.id = "usb_link";
  r.title = "USB link audit";

  if (pi.usb_sysfs_path.empty()) {
    r.status = QualStatus::kSkipped;
    r.detail = "no sysfs device path resolved for this camera";
    return r;
  }

  const std::string speed_str = ReadFileTrimmed(pi.usb_sysfs_path + "/speed");
  const double speed_mbps =
      speed_str.empty() ? 0.0 : std::atof(speed_str.c_str());

  // Siblings: same-depth device dirs under the same parent hub. "3-2.4" ->
  // siblings match "3-2.*"; a root-port device "3-2" -> siblings match "3-*".
  const size_t slash = pi.usb_sysfs_path.find_last_of('/');
  const std::string self_name = slash == std::string::npos
                                    ? pi.usb_sysfs_path
                                    : pi.usb_sysfs_path.substr(slash + 1);
  const size_t last_dot = self_name.find_last_of('.');
  const size_t dash = self_name.find('-');
  std::string parent_prefix;
  if (last_dot != std::string::npos)
    parent_prefix = self_name.substr(0, last_dot + 1);
  else if (dash != std::string::npos)
    parent_prefix = self_name.substr(0, dash + 1);
  const size_t self_depth = UsbPathDepth(self_name);

  int siblings = 0;
  const char* base = "/sys/bus/usb/devices";
  if (DIR* d = ::opendir(base)) {
    for (dirent* e = ::readdir(d); e; e = ::readdir(d)) {
      const std::string name = e->d_name;
      if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0])))
        continue;
      if (name.find(':') != std::string::npos || name == self_name) continue;
      if (parent_prefix.empty() ||
          name.compare(0, parent_prefix.size(), parent_prefix) != 0)
        continue;
      if (UsbPathDepth(name) != self_depth) continue;  // child, not sibling
      // Skip hubs (bDeviceClass 09): they extend the tree, not the budget.
      const std::string dev_class =
          ReadFileTrimmed(std::string(base) + "/" + name + "/bDeviceClass");
      if (dev_class == "09") continue;
      ++siblings;
    }
    ::closedir(d);
  }

  r.metrics.emplace_back("speed_mbps", speed_str.empty() ? "n/a" : speed_str);
  r.metrics.emplace_back("hub_siblings", std::to_string(siblings));
  r.metrics.emplace_back("sysfs_path", pi.usb_sysfs_path);

  // Informational check: always PASS, but flag bandwidth-sharing hazards.
  std::string warnings;
  if (speed_mbps > 0.0 && speed_mbps < kUsbSuperSpeedMbps)
    warnings = "USB2 link - check bandwidth budget";
  if (siblings > 0) {
    if (!warnings.empty()) warnings += "; ";
    warnings +=
        "shares a hub with " + std::to_string(siblings) + " device(s)";
  }
  r.status = QualStatus::kPass;
  r.detail = warnings.empty()
                 ? "link speed " + (speed_str.empty() ? "n/a" : speed_str) +
                       " Mbps, no non-hub siblings on the parent hub"
                 : warnings;
  return r;
}

QualCheckResult CheckOpenCloseStress(const std::string& device, int cycles) {
  QualCheckResult r;
  r.id = "open_close_stress";
  r.title = "Driver open/close stress";

  const auto t_start = std::chrono::steady_clock::now();
  for (int i = 0; i < cycles; ++i) {
    V4l2Device dev;
    Status s = dev.Open(device);
    if (!s.ok()) {
      r.status = QualStatus::kFail;
      r.detail = "open failed at cycle " + std::to_string(i) + ": " +
                 s.message();
      return r;
    }
    v4l2_capability cap{};
    if (dev.Ioctl(VIDIOC_QUERYCAP, &cap) != 0) {
      r.status = QualStatus::kFail;
      r.detail = "VIDIOC_QUERYCAP failed at cycle " + std::to_string(i) +
                 ": " + std::strerror(errno);
      return r;
    }
    // Full extended-control walk. The enumeration terminates with EINVAL
    // (ENOTTY on drivers without ext-ctrl support); anything else is an error.
    v4l2_query_ext_ctrl qc{};
    qc.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    while (dev.Ioctl(VIDIOC_QUERY_EXT_CTRL, &qc) == 0)
      qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    if (errno != EINVAL && errno != ENOTTY) {
      r.status = QualStatus::kFail;
      r.detail = "control walk failed at cycle " + std::to_string(i) + ": " +
                 std::strerror(errno);
      return r;
    }
  }
  const double total_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - t_start)
          .count();

  r.metrics.emplace_back("cycles", std::to_string(cycles));
  r.metrics.emplace_back("total_ms", FormatDouble(total_ms));
  r.metrics.emplace_back(
      "avg_ms", FormatDouble(cycles > 0 ? total_ms / cycles : 0.0));
  r.status = QualStatus::kPass;
  r.detail = std::to_string(cycles) + " open/query/close cycles in " +
             FormatDouble(total_ms) + " ms";
  return r;
}

QualCheckResult CheckWriteEffectLatency(ControlSet& cs, FrameTap& tap,
                                        uint32_t auto_ctrl,
                                        int64_t auto_manual,
                                        uint32_t exposure_ctrl) {
  QualCheckResult r;
  r.id = "write_effect_latency";
  r.title = "Control write->effect latency";

  const ControlInfo* target = cs.Find(exposure_ctrl);
  if (target == nullptr) {
    r.status = QualStatus::kSkipped;
    r.detail = "exposure control not present on this device";
    return r;
  }
  if (auto_ctrl != 0 && cs.Find(auto_ctrl) == nullptr) {
    r.status = QualStatus::kSkipped;
    r.detail = "auto-mode control not present on this device";
    return r;
  }
  const std::vector<int64_t> sweep = DeriveSweepValues(*target);
  if (sweep.size() < 3) {
    r.status = QualStatus::kSkipped;
    r.detail = "control range too narrow for a low/high sweep";
    return r;
  }
  const int64_t lo = sweep.front();
  const int64_t hi = sweep.back();

  int64_t orig_target = 0;
  const bool have_orig_target = cs.Get(exposure_ctrl, &orig_target).ok();
  int64_t orig_auto = 0;
  bool have_orig_auto = false;
  if (auto_ctrl != 0) have_orig_auto = cs.Get(auto_ctrl, &orig_auto).ok();

  auto restore = [&]() {
    if (have_orig_target) {
      Status rs = cs.Set(exposure_ctrl, orig_target);
      if (!rs.ok())
        XLOG_WARN("qualify[{}]: failed to restore target control: {}", r.id,
                  rs.message());
    }
    if (auto_ctrl != 0 && have_orig_auto) {
      Status rs = cs.Set(auto_ctrl, orig_auto);
      if (!rs.ok())
        XLOG_WARN("qualify[{}]: failed to restore auto control: {}", r.id,
                  rs.message());
    }
  };

  if (auto_ctrl != 0) {
    Status s = cs.Set(auto_ctrl, auto_manual);
    if (!s.ok()) {
      r.status = QualStatus::kFail;
      r.detail = "failed to switch to manual mode: " + s.message();
      restore();
      return r;
    }
  }

  // Baseline at the LOW exposure after a settle window.
  Status s = cs.Set(exposure_ctrl, lo);
  if (!s.ok()) {
    r.status = QualStatus::kFail;
    r.detail = "failed to set low exposure: " + s.message();
    restore();
    return r;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(kLatencySettleMs));
  std::vector<FrameTap::MeanSample> recent = tap.RecentMeans();
  if (recent.empty()) {
    r.status = QualStatus::kFail;
    r.detail = "no frames observed during the settle window";
    restore();
    return r;
  }
  const double base = recent.back().mean_y;
  const double threshold = base + std::max(10.0, 0.25 * base);
  r.metrics.emplace_back("base_mean_y", FormatDouble(base));
  r.metrics.emplace_back("threshold", FormatDouble(threshold));

  // Step HIGH and wait for the first frame whose pts is after the write and
  // whose Y mean crosses the threshold. steady_clock is CLOCK_MONOTONIC on
  // Linux, the same epoch as the V4L2 frame pts.
  s = cs.Set(exposure_ctrl, hi);
  const int64_t t0 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
  if (!s.ok()) {
    r.status = QualStatus::kFail;
    r.detail = "failed to set high exposure: " + s.message();
    restore();
    return r;
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kLatencyTimeoutMs);
  int64_t hit_pts = -1;
  while (std::chrono::steady_clock::now() < deadline) {
    recent = tap.RecentMeans();
    for (const FrameTap::MeanSample& m : recent) {  // oldest first
      if (m.pts_ns > t0 && m.mean_y > threshold) {
        hit_pts = m.pts_ns;
        break;
      }
    }
    if (hit_pts >= 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(kLatencyPollMs));
  }
  restore();

  if (hit_pts < 0) {
    r.status = QualStatus::kFail;
    r.detail = "no photometric response within 3s";
    return r;
  }
  int frames_after = 0;
  for (const FrameTap::MeanSample& m : recent)
    if (m.pts_ns > t0 && m.pts_ns <= hit_pts) ++frames_after;
  const double latency_ms = static_cast<double>(hit_pts - t0) / 1e6;
  r.metrics.emplace_back("latency_ms", FormatDouble(latency_ms));
  r.metrics.emplace_back("frames_after", std::to_string(frames_after));
  r.status = QualStatus::kPass;
  r.detail = "photometric response " + FormatDouble(latency_ms) +
             " ms after the write (" + std::to_string(frames_after) +
             " frame(s) captured in between)";
  return r;
}

}  // namespace xmotion
