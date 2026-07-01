/*
 * @file v4l2_device.hpp
 * @brief RAII wrapper over a V4L2 file descriptor with an EINTR-retrying ioctl
 *        helper. Shared (shared_ptr) so a BufferPool can keep the fd alive past
 *        source Close() until all outstanding frames release.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_PIPELINE_V4L2_DEVICE_HPP
#define XMCAM_PIPELINE_V4L2_DEVICE_HPP

#include <string>

#include "xmcam/core/result.hpp"

namespace xmotion {

class V4l2Device {
 public:
  V4l2Device() = default;
  ~V4l2Device();

  V4l2Device(const V4l2Device&) = delete;
  V4l2Device& operator=(const V4l2Device&) = delete;

  Status Open(const std::string& path);  // O_RDWR | O_NONBLOCK
  void Close();
  bool IsOpen() const { return fd_ >= 0; }
  int fd() const { return fd_; }
  const std::string& path() const { return path_; }

  // ioctl() retrying on EINTR. Returns 0 on success, -1 on error (errno set).
  int Ioctl(unsigned long request, void* arg) const;

 private:
  int fd_ = -1;
  std::string path_;
};

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_V4L2_DEVICE_HPP
