/*
 * @file pixel_format.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/core/pixel_format.hpp"

namespace xmotion {

bool IsCompressed(PixelFormat f) {
  return f == PixelFormat::kMjpeg || f == PixelFormat::kH264;
}

bool IsPlanar(PixelFormat f) {
  return f == PixelFormat::kNv12 || f == PixelFormat::kI420;
}

int BitsPerPixel(PixelFormat f) {
  switch (f) {
    case PixelFormat::kGray8:
      return 8;
    case PixelFormat::kRgb8:
    case PixelFormat::kBgr8:
      return 24;
    case PixelFormat::kRgba8:
    case PixelFormat::kBgra8:
      return 32;
    case PixelFormat::kYuyv:
      return 16;
    case PixelFormat::kNv12:
    case PixelFormat::kI420:
      return 12;
    default:
      return 0;  // compressed / unknown
  }
}

const char* ToString(PixelFormat f) {
  switch (f) {
    case PixelFormat::kGray8: return "GRAY8";
    case PixelFormat::kRgb8:  return "RGB8";
    case PixelFormat::kRgba8: return "RGBA8";
    case PixelFormat::kBgr8:  return "BGR8";
    case PixelFormat::kBgra8: return "BGRA8";
    case PixelFormat::kYuyv:  return "YUYV";
    case PixelFormat::kNv12:  return "NV12";
    case PixelFormat::kI420:  return "I420";
    case PixelFormat::kMjpeg: return "MJPEG";
    case PixelFormat::kH264:  return "H264";
    default:                  return "UNKNOWN";
  }
}

// V4L2 fourcc constants (from <linux/videodev2.h>), spelled out so core/ needs
// no kernel headers.
namespace {
constexpr uint32_t kV4l2Grey = FourCC('G', 'R', 'E', 'Y');
constexpr uint32_t kV4l2Yuyv = FourCC('Y', 'U', 'Y', 'V');
constexpr uint32_t kV4l2Nv12 = FourCC('N', 'V', '1', '2');
constexpr uint32_t kV4l2Yu12 = FourCC('Y', 'U', '1', '2');  // I420
constexpr uint32_t kV4l2Rgb24 = FourCC('R', 'G', 'B', '3');
constexpr uint32_t kV4l2Bgr24 = FourCC('B', 'G', 'R', '3');
constexpr uint32_t kV4l2Mjpg = FourCC('M', 'J', 'P', 'G');
constexpr uint32_t kV4l2Jpeg = FourCC('J', 'P', 'E', 'G');
constexpr uint32_t kV4l2H264 = FourCC('H', '2', '6', '4');
}  // namespace

PixelFormat FromV4l2Fourcc(uint32_t fourcc) {
  switch (fourcc) {
    case kV4l2Grey: return PixelFormat::kGray8;
    case kV4l2Yuyv: return PixelFormat::kYuyv;
    case kV4l2Nv12: return PixelFormat::kNv12;
    case kV4l2Yu12: return PixelFormat::kI420;
    case kV4l2Rgb24: return PixelFormat::kRgb8;
    case kV4l2Bgr24: return PixelFormat::kBgr8;
    case kV4l2Mjpg:
    case kV4l2Jpeg: return PixelFormat::kMjpeg;
    case kV4l2H264: return PixelFormat::kH264;
    default: return PixelFormat::kUnknown;
  }
}

uint32_t ToV4l2Fourcc(PixelFormat f) {
  switch (f) {
    case PixelFormat::kGray8: return kV4l2Grey;
    case PixelFormat::kYuyv:  return kV4l2Yuyv;
    case PixelFormat::kNv12:  return kV4l2Nv12;
    case PixelFormat::kI420:  return kV4l2Yu12;
    case PixelFormat::kRgb8:  return kV4l2Rgb24;
    case PixelFormat::kBgr8:  return kV4l2Bgr24;
    case PixelFormat::kMjpeg: return kV4l2Mjpg;
    case PixelFormat::kH264:  return kV4l2H264;
    default: return 0;
  }
}

PixelFormat FromGstFormatName(const std::string& name) {
  if (name == "GRAY8") return PixelFormat::kGray8;
  if (name == "RGB") return PixelFormat::kRgb8;
  if (name == "RGBA") return PixelFormat::kRgba8;
  if (name == "BGR") return PixelFormat::kBgr8;
  if (name == "BGRA") return PixelFormat::kBgra8;
  if (name == "YUY2") return PixelFormat::kYuyv;
  if (name == "NV12") return PixelFormat::kNv12;
  if (name == "I420") return PixelFormat::kI420;
  return PixelFormat::kUnknown;
}

}  // namespace xmotion
