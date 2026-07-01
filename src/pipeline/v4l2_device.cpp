/*
 * @file v4l2_device.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/pipeline/v4l2_device.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

V4l2Device::~V4l2Device() { Close(); }

Status V4l2Device::Open(const std::string& path) {
  Close();
  int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    return Err(ErrorCode::kIo,
               "open " + path + ": " + std::strerror(errno));
  }
  fd_ = fd;
  path_ = path;
  XLOG_DEBUG("V4l2Device opened {} (fd={})", path_, fd_);
  return Ok();
}

void V4l2Device::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    XLOG_DEBUG("V4l2Device closed {} (fd={})", path_, fd_);
    fd_ = -1;
  }
}

int V4l2Device::Ioctl(unsigned long request, void* arg) const {
  int r;
  do {
    r = ::ioctl(fd_, request, arg);
  } while (r == -1 && errno == EINTR);
  return r;
}

}  // namespace xmotion
