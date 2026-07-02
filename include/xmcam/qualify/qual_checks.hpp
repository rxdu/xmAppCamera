/*
 * @file qual_checks.hpp
 * @brief Automated checks behind the production-camera qualification
 *        checklist: platform/firmware identification, enumeration dump,
 *        control-lock verification (exposure/gain/AWB), timestamp stability,
 *        and power-cycle image-fingerprint comparison.
 *
 *        All blocking functions are meant to run from a worker thread; they
 *        may sleep (CheckControlLock samples over a hold window).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_QUALIFY_QUAL_CHECKS_HPP
#define XMCAM_QUALIFY_QUAL_CHECKS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "xmcam/control/control_info.hpp"
#include "xmcam/control/control_set.hpp"
#include "xmcam/core/result.hpp"
#include "xmcam/core/source_caps.hpp"
#include "xmcam/qualify/frame_tap.hpp"
#include "xmcam/qualify/qual_types.hpp"

namespace xmotion {

// Gather platform + USB identity for `device` (e.g. "/dev/video0"): kernel
// release (uname), /etc/os-release PRETTY_NAME, /etc/nv_tegra_release first
// line, driver name+version and bus_info via VIDIOC_QUERYCAP, and the USB
// idVendor:idProduct / bcdDevice / serial resolved from sysfs by matching the
// device's product/manufacturer strings against the QUERYCAP card string.
// USB fields stay empty when no sysfs match is found (not an error). Run from
// a worker thread. Returns an error if the device cannot be opened/queried;
// host-side fields gathered up to that point are still filled in *out.
Status CollectPlatformInfo(const std::string& device, PlatformInfo* out);

// Enumeration dump check: PASS if caps non-empty and controls list non-empty.
// metrics: n_formats, n_controls, plus one "fmt_<NAME>" -> "N sizes" metric
// per enumerated format.
QualCheckResult CheckEnumeration(const SourceCaps& caps,
                                 const std::vector<ControlInfo>& controls);

// Generic control-lock check (covers exposure/gain/AWB). Run from a worker
// thread; sleeps ~100 ms between samples over the hold window.
//   1. If auto_ctrl_id != 0: set it to auto_manual_value, then verify the
//      target control is no longer INACTIVE.
//   2. Set target_ctrl_id to target_value.
//   3. Over hold_ms milliseconds (sampling every ~100 ms), verify the value
//      stays == target_value and the control never reports VOLATILE.
//   4. Restore the target's and auto control's original values.
// PASS if held for the whole window; FAIL with detail on first deviation;
// kSkipped if the target or auto control is missing on this device.
// metrics: samples, held_value, volatile_flag.
QualCheckResult CheckControlLock(ControlSet& cs, const std::string& check_id,
                                 const std::string& title,
                                 uint32_t auto_ctrl_id,
                                 int64_t auto_manual_value,
                                 uint32_t target_ctrl_id, int64_t target_value,
                                 int hold_ms);

// Timestamp stability from a pts series (ns): computes inter-frame deltas and
// discards the first 5 (pipeline warm-up). metrics: n, mean_ms, stddev_ms,
// min_ms, max_ms, monotonic (true/false), nominal_ms. PASS if monotonic AND
// stddev < 20% of the nominal period AND max < 3x nominal. kSkipped if fewer
// than 30 samples.
QualCheckResult CheckTimestampStability(const std::vector<int64_t>& pts_ns,
                                        double nominal_fps);

// Image fingerprint compare (power-cycle identity): PASS if the |mean_y|,
// |mean_u| and |mean_v| differences are each <= tolerance_pct percent of 255
// and width/height match. metrics: before/after means, deltas. kSkipped if
// either snapshot has fewer than 10 frames.
QualCheckResult CompareImageFingerprint(const FrameTap::Snapshot& before,
                                        const FrameTap::Snapshot& after,
                                        double tolerance_pct);

}  // namespace xmotion

#endif  // XMCAM_QUALIFY_QUAL_CHECKS_HPP
