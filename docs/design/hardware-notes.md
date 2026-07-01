# Hardware Notes — test camera

Ground-truth captured 2026-07-01 from the USB camera on the dev host (via raw V4L2 ioctls; cross-checkable with `v4l2-ctl`). Use this as the reference for validating `V4l2Source` + `ControlIntrospector` against real hardware.

## Device
- **Card:** `Global Shutter Camera` (self-reported USB product string — *not* independently verified as global-shutter). Driver: `uvcvideo`. Bus: `usb-0000:00:14.0-2`.
- **Nodes:** `/dev/video0` = capture; `/dev/video1` = **metadata node** (0 formats, 0 controls) → must be filtered out by requiring `V4L2_CAP_VIDEO_CAPTURE` + non-empty `ENUM_FMT`.
- **Stable handle:** `/dev/v4l/by-id/usb-Global_Shutter_Camera_Global_Shutter_Camera_01.00.00-video-index0`.
- **Access:** current user (not in `video` group) can open the node via the logind ACL — no group change needed on this host.

## Formats (video0)
- **`MJPG` (compressed) — the primary usable format.** Up to **1920×1200 @ 90 fps**; also 1920×1080, 1600×1200, 1280×960, 1280×720, 1024×768, 960×720, 800×600, 640×480, 320×240, each at 90/60/30/25/20/15/10/5 fps. **Requires JPEG decode** (GStreamer `jpegdec`).
- **`YUYV` (raw) — throttled.** 1920×1200 @ 5 fps only; 1280×720 @ 10/5; 640×480 @ 30/25/20/15/10/5; 320×240 @ 90/60/30/…  Useful for a **zero-decode first-light** at 640×480@30 or 320×240@90.

## Controls (20) — validates the introspection design
`brightness`(int -64..64), `contrast`(0..95), `saturation`(0..255), `hue`(-2000..2000), `white_balance_automatic`(bool), `gamma`(64..300), `gain`(0..1023), `power_line_frequency`(menu 0..2), `white_balance_temperature`(int 2800..6500, **flags=0x10 INACTIVE**), `sharpness`(0..7), `backlight_compensation`(36..160), `auto_exposure`(menu 0..3), `exposure_time_absolute`(int 1..10000, **flags=0x10 INACTIVE**), `pan_absolute`, `tilt_absolute`, `focus_absolute`(0..1023), `focus_automatic_continuous`(bool), `zoom_absolute`(0..60). Plus two control-class markers (`User Controls`, `Camera Controls`, flags=0x44) to group/skip in UI.

**Key test case:** `white_balance_temperature` and `exposure_time_absolute` are `INACTIVE` (0x10) because their auto toggles (`white_balance_automatic`, `auto_exposure`) are engaged. Toggling auto off must clear the flag — this is the concrete driver behavior the ControlPanel re-query logic (DESIGN §5.3) must handle.
