/*
 * @file pixel_format.hpp
 * @brief Pixel format enum + conversions between V4L2 fourcc, GStreamer format
 *        names, and OpenGL upload parameters. No OpenCV.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_PIXEL_FORMAT_HPP
#define XMCAM_CORE_PIXEL_FORMAT_HPP

#include <cstdint>
#include <string>

namespace xmotion {

// The frame formats xmAppCamera moves end-to-end. kMjpeg/kH264 are *compressed*
// markers used at negotiation time — they are never handed to the uploader;
// they are decoded first (to a packed/planar raw format below).
enum class PixelFormat {
  kUnknown,
  kGray8,   // 1 plane, 8bpp
  kRgb8,    // packed RGB   24bpp
  kRgba8,   // packed RGBA  32bpp
  kBgr8,    // packed BGR   24bpp
  kBgra8,   // packed BGRA  32bpp
  kYuyv,    // packed YUV422 (YUYV / YUY2) 16bpp
  kNv12,    // planar Y + interleaved UV  12bpp
  kI420,    // planar Y + U + V           12bpp
  // compressed negotiation markers (decoded before display):
  kMjpeg,
  kH264,
};

// Compose a V4L2 / GStreamer style fourcc at compile time.
constexpr uint32_t FourCC(char a, char b, char c, char d) {
  return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
         (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

bool IsCompressed(PixelFormat f);
bool IsPlanar(PixelFormat f);
// Average bits per pixel over the whole frame (e.g. NV12 -> 12). 0 if unknown.
int BitsPerPixel(PixelFormat f);
const char* ToString(PixelFormat f);

// V4L2 fourcc (e.g. V4L2_PIX_FMT_YUYV) <-> PixelFormat.
PixelFormat FromV4l2Fourcc(uint32_t fourcc);
uint32_t ToV4l2Fourcc(PixelFormat f);  // 0 if not expressible

// GStreamer video/x-raw format string (e.g. "RGBA", "NV12", "YUY2").
PixelFormat FromGstFormatName(const std::string& name);

}  // namespace xmotion

#endif  // XMCAM_CORE_PIXEL_FORMAT_HPP
