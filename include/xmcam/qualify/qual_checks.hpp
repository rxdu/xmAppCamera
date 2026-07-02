/*
 * @file qual_checks.hpp
 * @brief Automated checks behind the production-camera qualification
 *        checklist: platform/firmware identification, enumeration dump,
 *        control-lock and control-effect verification (exposure/gain/AWB),
 *        timestamp stability, power-cycle image-fingerprint comparison, soak
 *        evaluation, serial uniqueness, USB link audit, open/close stress and
 *        write->effect latency.
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

// Photometric effect verification: prove a control actually changes the image.
// Run from a worker thread; sleeps settle_ms + sample_ms per sweep step.
//   1. Save original values; if auto_ctrl != 0 set it to auto_manual.
//   2. Derive 4 log-spaced sweep values in [max(minimum, maximum/512) ..
//      maximum/4] from the Find(target_ctrl) range (deduplicated; fewer than
//      3 distinct values -> kSkipped).
//   3. For each value ascending: Set(target_ctrl, v), sleep settle_ms,
//      tap.Reset(), sleep sample_ms, record tap.Take().mean_y.
//   4. PASS if the mean_y series is non-decreasing (epsilon 1.0, saturation
//      plateaus allowed) AND (last - first) >= min_delta.
//   5. Restore originals (target first, then auto); log-warn on restore
//      failure.
// metrics: one "v_<value>" -> mean_y per sweep step, plus delta.
QualCheckResult CheckControlEffect(ControlSet& cs, FrameTap& tap,
                                   const std::string& id,
                                   const std::string& title, uint32_t auto_ctrl,
                                   int64_t auto_manual, uint32_t target_ctrl,
                                   int settle_ms, int sample_ms,
                                   double min_delta);

// Soak evaluation. The CALLER runs the window (tap.Reset(); sleep; Take());
// this only judges the snapshot. PASS iff ALL of: hw_seq_gaps <= 0.1% of
// frames; stuck_frames == 0; black_frames + white_frames == 0;
// interval_stddev_ms < 0.2 * (1000 / nominal_fps) (clause skipped if
// nominal_fps <= 0); every per10s_frames bucket (excluding a possibly-partial
// last bucket) within 15% of the bucket mean; (rss_kb_after - rss_kb_before)
// < 10240 KB. Detail explains the first failing clause. kSkipped if the
// snapshot has fewer than 100 frames.
// metrics: frames, gaps, stuck, black, white, fps_min_bucket, fps_max_bucket,
// rss_growth_kb, interval stats.
QualCheckResult CheckSoak(const FrameTap::Snapshot& s, double nominal_fps,
                          long rss_kb_before, long rss_kb_after,
                          int duration_s);

// Resident-set size of this process in KB, from /proc/self/status VmRSS
// (falling back to /proc/self/statm * page size). 0 on failure.
long ReadRssKb();

// Serial-number sanity for fleet use ("can /dev/v4l/by-id distinguish units
// in bulk?"). FAIL if pi.usb_serial is empty, OR matches a version-like
// pattern (^[0-9]+([._][0-9]+)+$, e.g. "01.00.00"), OR appears more than once
// in all_serials (collision among attached cameras). PASS otherwise; detail
// names the reason.
QualCheckResult CheckSerialUniqueness(const PlatformInfo& pi,
                                      const std::vector<std::string>& all_serials);

// USB link audit from pi.usb_sysfs_path: reads <path>/speed (Mbps) and counts
// sibling non-hub devices under the same parent hub. PASS always unless the
// sysfs path is empty (kSkipped); detail warns when speed < 5000 ("USB2 link -
// check bandwidth budget") or siblings > 0 ("shares a hub with N device(s)").
// metrics: speed_mbps, hub_siblings, sysfs_path.
QualCheckResult CheckUsbLink(const PlatformInfo& pi);

// Driver open/close stress: `cycles` iterations of open(device) + QUERYCAP +
// full VIDIOC_QUERY_EXT_CTRL walk + close. Safe to run while another fd
// streams. FAIL on the first cycle that errors (detail: cycle #, errno text).
// metrics: cycles, total_ms, avg_ms.
QualCheckResult CheckOpenCloseStress(const std::string& device, int cycles);

// Write->effect latency. Forces manual mode via auto_ctrl/auto_manual (saved
// and restored), sets exposure_ctrl LOW (sweep lo, same derivation as
// CheckControlEffect), settles 800 ms and records base = last RecentMeans()
// mean_y. Then sets exposure_ctrl HIGH (sweep hi), notes t0 = steady_clock
// now (ns; same CLOCK_MONOTONIC epoch as the V4L2 pts on Linux) and polls
// tap.RecentMeans() for up to 3 s for the first sample with pts_ns > t0 AND
// mean_y > base + max(10.0, 0.25 * base). Reports latency_ms =
// (hit pts - t0) / 1e6 and frames_after = samples with pts in (t0, hit].
// PASS if found; FAIL "no photometric response within 3s"; kSkipped if the
// controls are missing or the range is too narrow.
QualCheckResult CheckWriteEffectLatency(ControlSet& cs, FrameTap& tap,
                                        uint32_t auto_ctrl, int64_t auto_manual,
                                        uint32_t exposure_ctrl);

}  // namespace xmotion

#endif  // XMCAM_QUALIFY_QUAL_CHECKS_HPP
