/*
 * @file control_probe.cpp
 * @brief Headless dump of a device's V4L2 controls (id, name, type, range,
 *        flags, menu items), followed by a live demonstration of control
 *        interdependency: switch Auto Exposure to Manual Mode and show that
 *        exposure_time_absolute's INACTIVE flag clears.
 *
 * Usage: xmcam_control_probe [device]   (default /dev/video0)
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <linux/videodev2.h>

#include <cstdio>
#include <memory>
#include <string>

#include "xmcam/control/control_set.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"

using namespace xmotion;

namespace {

const char* FlagWord(const ControlInfo& c) {
  if (c.IsDisabled()) return "disabled";
  if (c.IsInactive()) return "inactive";
  if (c.IsReadOnly()) return "read-only";
  return "active";
}

void PrintControls(const ControlSet& set) {
  std::printf("== %zu control(s) ==\n", set.controls().size());
  for (const auto& c : set.controls()) {
    std::printf(
        "  0x%08x %-30s %-12s range=[%lld..%lld] step=%lld default=%lld "
        "flags=0x%04x (%s)\n",
        c.id, c.name.c_str(), ToString(c.type),
        static_cast<long long>(c.minimum), static_cast<long long>(c.maximum),
        static_cast<long long>(c.step),
        static_cast<long long>(c.default_value), c.flags, FlagWord(c));
    for (const auto& item : c.menu) {
      std::printf("        [%lld] %s\n", static_cast<long long>(item.value),
                  item.label.c_str());
    }
  }
}

// Report the INACTIVE state of exposure_time_absolute, or note its absence.
void ReportExposureState(ControlSet& set, const char* when) {
  const ControlInfo* exp = set.Find(V4L2_CID_EXPOSURE_ABSOLUTE);
  if (exp == nullptr) {
    std::printf("  exposure_time_absolute: not exposed by this device\n");
    return;
  }
  std::printf("  exposure_time_absolute %s: flags=0x%04x -> %s\n", when,
              exp->flags, exp->IsInactive() ? "INACTIVE" : "ACTIVE");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string device = argc > 1 ? argv[1] : "/dev/video0";

  auto dev = std::make_shared<V4l2Device>();
  if (auto st = dev->Open(device); !st.ok()) {
    std::printf("Open %s failed: %s\n", device.c_str(), st.message().c_str());
    return 1;
  }

  ControlSet set(dev);
  if (auto st = set.Refresh(); !st.ok()) {
    std::printf("Refresh failed: %s\n", st.message().c_str());
    return 1;
  }
  PrintControls(set);

  // --- Dependency demo: Auto Exposure -> exposure_time_absolute -----------
  std::printf("\n== dependency demo: Auto Exposure -> exposure_time ==\n");
  const ControlInfo* ae = set.Find(V4L2_CID_EXPOSURE_AUTO);
  if (ae == nullptr) {
    std::printf("  Auto Exposure control not exposed — skipping demo\n");
    return 0;
  }

  int64_t ae_value = 0;
  if (auto st = set.Get(V4L2_CID_EXPOSURE_AUTO, &ae_value); !st.ok()) {
    std::printf("  read Auto Exposure failed: %s\n", st.message().c_str());
    return 1;
  }
  std::printf("  Auto Exposure current value = %lld\n",
              static_cast<long long>(ae_value));
  ReportExposureState(set, "before");

  // V4L2_EXPOSURE_MANUAL == 1: hands exposure_time_absolute back to the user.
  const int64_t kManual = V4L2_EXPOSURE_MANUAL;
  std::printf("  setting Auto Exposure -> %lld (Manual Mode)...\n",
              static_cast<long long>(kManual));
  if (auto st = set.Set(V4L2_CID_EXPOSURE_AUTO, kManual); !st.ok()) {
    std::printf("  set Auto Exposure failed: %s\n", st.message().c_str());
    // Restore best-effort and bail.
    (void)set.Set(V4L2_CID_EXPOSURE_AUTO, ae_value);
    return 1;
  }
  ReportExposureState(set, "after ");

  const ControlInfo* exp = set.Find(V4L2_CID_EXPOSURE_ABSOLUTE);
  const bool cleared = exp != nullptr && !exp->IsInactive();
  std::printf("  => exposure_time_absolute INACTIVE flag %s\n",
              cleared ? "CLEARED (now settable)" : "still set / N/A");

  // Restore the camera's original auto-exposure mode.
  if (auto st = set.Set(V4L2_CID_EXPOSURE_AUTO, ae_value); !st.ok())
    std::printf("  (note) failed to restore Auto Exposure: %s\n",
                st.message().c_str());

  return 0;
}
