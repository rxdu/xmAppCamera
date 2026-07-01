/*
 * @file frame_texture.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/frame_texture.hpp"

#include "glad/glad.h"

#include <algorithm>

namespace xmotion {
namespace {

inline uint8_t Clamp8(int v) {
  return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// BT.601 YUV -> RGB.
inline void Yuv2Rgb(int y, int u, int v, uint8_t* out) {
  y -= 16;
  u -= 128;
  v -= 128;
  out[0] = Clamp8((298 * y + 409 * v + 128) >> 8);
  out[1] = Clamp8((298 * y - 100 * u - 208 * v + 128) >> 8);
  out[2] = Clamp8((298 * y + 516 * u + 128) >> 8);
  out[3] = 255;
}

void ConvertYuyvToRgba(const VideoFrame& f, std::vector<uint8_t>* dst) {
  dst->resize(static_cast<size_t>(f.width) * f.height * 4);
  for (int y = 0; y < f.height; ++y) {
    const uint8_t* row = f.data + static_cast<size_t>(y) * f.stride;
    uint8_t* out = dst->data() + static_cast<size_t>(y) * f.width * 4;
    for (int x = 0; x < f.width; x += 2) {
      const int y0 = row[0], u = row[1], y1 = row[2], v = row[3];
      Yuv2Rgb(y0, u, v, out);
      if (x + 1 < f.width) Yuv2Rgb(y1, u, v, out + 4);
      row += 4;
      out += 8;
    }
  }
}

void ConvertNv12ToRgba(const VideoFrame& f, std::vector<uint8_t>* dst) {
  dst->resize(static_cast<size_t>(f.width) * f.height * 4);
  for (int y = 0; y < f.height; ++y) {
    const uint8_t* yrow = f.data + static_cast<size_t>(y) * f.stride;
    const uint8_t* uvrow =
        f.plane1 + static_cast<size_t>(y / 2) * f.stride1;
    uint8_t* out = dst->data() + static_cast<size_t>(y) * f.width * 4;
    for (int x = 0; x < f.width; ++x) {
      const int yy = yrow[x];
      const int u = uvrow[(x / 2) * 2];
      const int v = uvrow[(x / 2) * 2 + 1];
      Yuv2Rgb(yy, u, v, out);
      out += 4;
    }
  }
}

}  // namespace

FrameTexture::~FrameTexture() {
  if (tex_) glDeleteTextures(1, &tex_);
}

void FrameTexture::EnsureTexture() {
  if (!tex_) {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

unsigned FrameTexture::Upload(const VideoFrame& f) {
  if (!f.valid()) return tex_;
  EnsureTexture();
  glBindTexture(GL_TEXTURE_2D, tex_);

  GLenum gl_format = GL_RGBA;
  const uint8_t* pixels = f.data;
  int row_len_px = f.stride;  // in pixels, set per-format below

  switch (f.format) {
    case PixelFormat::kRgba8: gl_format = GL_RGBA; row_len_px = f.stride / 4; break;
    case PixelFormat::kBgra8: gl_format = GL_BGRA; row_len_px = f.stride / 4; break;
    case PixelFormat::kRgb8:  gl_format = GL_RGB;  row_len_px = f.stride / 3; break;
    case PixelFormat::kBgr8:  gl_format = GL_BGR;  row_len_px = f.stride / 3; break;
    case PixelFormat::kGray8: gl_format = GL_RED;  row_len_px = f.stride; break;
    case PixelFormat::kYuyv:
      ConvertYuyvToRgba(f, &rgba_);
      pixels = rgba_.data();
      gl_format = GL_RGBA;
      row_len_px = f.width;
      break;
    case PixelFormat::kNv12:
      ConvertNv12ToRgba(f, &rgba_);
      pixels = rgba_.data();
      gl_format = GL_RGBA;
      row_len_px = f.width;
      break;
    default:
      return tex_;  // unsupported (compressed handled upstream)
  }

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, row_len_px);

  const bool realloc = (f.width != w_ || f.height != h_ || f.format != fmt_);
  if (realloc) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f.width, f.height, 0, gl_format,
                 GL_UNSIGNED_BYTE, pixels);
    w_ = f.width;
    h_ = f.height;
    fmt_ = f.format;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, gl_format,
                    GL_UNSIGNED_BYTE, pixels);
  }
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex_;
}

}  // namespace xmotion
