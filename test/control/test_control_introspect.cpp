// Control introspection is hardware-dependent: skip cleanly when /dev/video0 is
// absent or unopenable so CI stays green, but assert real invariants when a UVC
// camera is present.
#include "xmcam/control/control_introspector.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "xmcam/control/control_set.hpp"
#include "xmcam/pipeline/v4l2_device.hpp"

using namespace xmotion;

namespace {

// Open /dev/video0 or SKIP the calling test if it is unavailable.
#define OPEN_OR_SKIP(dev)                                        \
  do {                                                          \
    if (!(dev).Open("/dev/video0").ok())                       \
      GTEST_SKIP() << "/dev/video0 not present or not openable"; \
  } while (0)

}  // namespace

TEST(ControlIntrospect, EnumeratesRealControls) {
  V4l2Device dev;
  OPEN_OR_SKIP(dev);

  std::vector<ControlInfo> controls;
  ASSERT_TRUE(ControlIntrospector::Enumerate(dev, &controls).ok());

  // The reference UVC camera exposes ~20 controls; require a healthy floor.
  EXPECT_GE(controls.size(), 15u) << "too few controls enumerated";

  // No control-class marker must leak through as a value control.
  for (const auto& c : controls) {
    EXPECT_FALSE(c.name.empty());
    EXPECT_NE(c.type, ControlType::kUnknown) << c.name;
  }

  // A Brightness-like integer control with a real range must exist.
  bool found_int_range = false;
  for (const auto& c : controls) {
    if (c.type == ControlType::kInteger && c.minimum < c.maximum) {
      found_int_range = true;
      break;
    }
  }
  EXPECT_TRUE(found_int_range) << "no integer control with min<max found";
}

TEST(ControlIntrospect, MenuControlHasItems) {
  V4l2Device dev;
  OPEN_OR_SKIP(dev);

  std::vector<ControlInfo> controls;
  ASSERT_TRUE(ControlIntrospector::Enumerate(dev, &controls).ok());

  // At least one menu control (e.g. Auto Exposure / Power Line Frequency) must
  // have its entries populated.
  bool menu_with_items = false;
  for (const auto& c : controls) {
    if (c.type == ControlType::kMenu && !c.menu.empty()) {
      menu_with_items = true;
      for (const auto& item : c.menu) EXPECT_FALSE(item.label.empty());
    }
  }
  EXPECT_TRUE(menu_with_items) << "no populated menu control found";
}

TEST(ControlSetTest, RefreshAndFind) {
  auto dev = std::make_shared<V4l2Device>();
  if (!dev->Open("/dev/video0").ok())
    GTEST_SKIP() << "/dev/video0 not present or not openable";

  ControlSet set(dev);
  ASSERT_TRUE(set.Refresh().ok());
  EXPECT_GE(set.controls().size(), 15u);

  // Find() must agree with the cached list.
  const auto& first = set.controls().front();
  const ControlInfo* got = set.Find(first.id);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->id, first.id);
  EXPECT_EQ(set.Find(0xDEADBEEFu), nullptr);
}
