/*
 * @file yuv_converter.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/yuv_converter.hpp"

#include "glad/glad.h"

#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

const char* kVert = R"(#version 330 core
out vec2 vUv;
void main() {
  // Fullscreen triangle; flip V so the image is upright in the FBO.
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = vec2(p.x, 1.0 - p.y);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

const char* kFrag = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D yTex;
uniform sampler2D uTex;  // I420 U (r) or NV12 UV (rg)
uniform sampler2D vTex;  // I420 V (r)
uniform int mode;        // 0 = I420, 1 = NV12
void main() {
  float y = texture(yTex, vUv).r;
  float u, v;
  if (mode == 1) { vec2 uv = texture(uTex, vUv).rg; u = uv.r; v = uv.g; }
  else { u = texture(uTex, vUv).r; v = texture(vTex, vUv).r; }
  // BT.601 limited-range YUV -> RGB.
  y = (y - 0.0625) * 1.164;
  u -= 0.5; v -= 0.5;
  float r = y + 1.596 * v;
  float g = y - 0.391 * u - 0.813 * v;
  float b = y + 2.018 * u;
  frag = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
})";

unsigned Compile(unsigned type, const char* src) {
  unsigned s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  int ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    XLOG_ERROR("YuvConverter shader compile: {}", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

}  // namespace

bool YuvConverter::Supports(PixelFormat f) {
  return f == PixelFormat::kI420 || f == PixelFormat::kNv12;
}

YuvConverter::~YuvConverter() {
  if (program_) glDeleteProgram(program_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
  unsigned texs[] = {rgba_tex_, y_tex_, u_tex_, v_tex_};
  glDeleteTextures(4, texs);
}

bool YuvConverter::EnsureResources() {
  if (init_) return program_ != 0;
  init_ = true;

  unsigned vs = Compile(GL_VERTEX_SHADER, kVert);
  unsigned fs = Compile(GL_FRAGMENT_SHADER, kFrag);
  if (!vs || !fs) {
    failed_ = true;
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  int ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    XLOG_ERROR("YuvConverter program link failed");
    glDeleteProgram(program_);
    program_ = 0;
    failed_ = true;
    return false;
  }
  glGenVertexArrays(1, &vao_);
  glGenTextures(1, &y_tex_);
  glGenTextures(1, &u_tex_);
  glGenTextures(1, &v_tex_);

  glUseProgram(program_);
  glUniform1i(glGetUniformLocation(program_, "yTex"), 0);
  glUniform1i(glGetUniformLocation(program_, "uTex"), 1);
  glUniform1i(glGetUniformLocation(program_, "vTex"), 2);
  loc_mode_ = glGetUniformLocation(program_, "mode");
  return true;
}

void YuvConverter::EnsureFbo(int w, int h) {
  if (fbo_ && fbo_w_ == w && fbo_h_ == h) return;
  if (!fbo_) glGenFramebuffers(1, &fbo_);
  if (!rgba_tex_) glGenTextures(1, &rgba_tex_);
  glBindTexture(GL_TEXTURE_2D, rgba_tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         rgba_tex_, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  fbo_w_ = w;
  fbo_h_ = h;
}

void YuvConverter::UploadPlane(unsigned tex, int unit, const uint8_t* data,
                               int w, int h, int stride, unsigned gl_format) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  const int bpp = (gl_format == GL_RG) ? 2 : 1;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / bpp);
  glTexImage2D(GL_TEXTURE_2D, 0, gl_format == GL_RG ? GL_RG8 : GL_R8, w, h, 0,
               gl_format, GL_UNSIGNED_BYTE, data);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

unsigned YuvConverter::Convert(const VideoFrame& f) {
  if (failed_ || !f.valid() || !Supports(f.format)) return 0;
  if (!EnsureResources()) return 0;
  EnsureFbo(f.width, f.height);

  const int cw = (f.width + 1) / 2;
  const int ch = (f.height + 1) / 2;
  UploadPlane(y_tex_, 0, f.data, f.width, f.height, f.stride, GL_RED);
  if (f.format == PixelFormat::kNv12) {
    UploadPlane(u_tex_, 1, f.plane1, cw, ch, f.stride1, GL_RG);
  } else {  // I420
    UploadPlane(u_tex_, 1, f.plane1, cw, ch, f.stride1, GL_RED);
    UploadPlane(v_tex_, 2, f.plane2, cw, ch, f.stride2, GL_RED);
  }

  // Render the conversion into the FBO.
  GLint prev_fbo = 0, prev_vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, prev_vp);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, f.width, f.height);
  glUseProgram(program_);
  glUniform1i(loc_mode_, f.format == PixelFormat::kNv12 ? 1 : 0);
  glBindVertexArray(vao_);
  glDisable(GL_DEPTH_TEST);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);

  glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
  glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
  glActiveTexture(GL_TEXTURE0);
  return rgba_tex_;
}

}  // namespace xmotion
