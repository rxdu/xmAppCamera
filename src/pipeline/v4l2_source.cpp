/*
 * @file v4l2_source.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/pipeline/v4l2_source.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "xmcam/core/util/scope_timer.hpp"
#include "xmsigma/logging/xlogger.hpp"

#ifdef XMCAM_WITH_GSTREAMER
#include "xmcam/pipeline/gst_jpeg_decoder.hpp"
#endif

namespace xmotion {
namespace {

constexpr unsigned kBufferCount = 4;

int64_t TimevalToNs(const timeval& tv) {
  return static_cast<int64_t>(tv.tv_sec) * 1000000000LL +
         static_cast<int64_t>(tv.tv_usec) * 1000LL;
}

// List /dev/video* device node paths (sorted).
std::vector<std::string> ListVideoNodes() {
  std::vector<std::string> nodes;
  DIR* d = ::opendir("/dev");
  if (!d) return nodes;
  for (dirent* e = ::readdir(d); e; e = ::readdir(d)) {
    if (std::strncmp(e->d_name, "video", 5) == 0)
      nodes.push_back(std::string("/dev/") + e->d_name);
  }
  ::closedir(d);
  std::sort(nodes.begin(), nodes.end());
  return nodes;
}

// Map a /dev/videoN to a stable symlink under `dir`, if any.
std::string ResolveStableLink(const char* dir, const std::string& node) {
  DIR* d = ::opendir(dir);
  if (!d) return {};
  std::string result;
  for (dirent* e = ::readdir(d); e; e = ::readdir(d)) {
    if (e->d_name[0] == '.') continue;
    std::string link = std::string(dir) + "/" + e->d_name;
    char resolved[PATH_MAX];
    if (::realpath(link.c_str(), resolved) && node == resolved) {
      result = link;
      break;
    }
  }
  ::closedir(d);
  return result;
}

// by-id encodes the USB serial (ambiguous when units share a serial);
// by-path encodes the physical port chain (stable on fixed wiring).
std::string ResolveByIdPath(const std::string& node) {
  return ResolveStableLink("/dev/v4l/by-id", node);
}
std::string ResolveByPathPath(const std::string& node) {
  return ResolveStableLink("/dev/v4l/by-path", node);
}

}  // namespace

// ---------------------------------------------------------------------------
// V4l2BufferPool
// ---------------------------------------------------------------------------
V4l2BufferPool::~V4l2BufferPool() {
  // Stop streaming then unmap. By now no frame leases remain (they hold a
  // shared_ptr to this pool), so unmapping is safe.
  if (dev_ && dev_->IsOpen()) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dev_->Ioctl(VIDIOC_STREAMOFF, &type);
  }
  for (auto& s : slots_) {
    if (s.start && s.start != MAP_FAILED) ::munmap(s.start, s.length);
  }
  XLOG_DEBUG("V4l2BufferPool destroyed ({} slots unmapped)", slots_.size());
}

void V4l2BufferPool::Requeue(unsigned index) {
  if (!dev_ || !dev_->IsOpen()) return;
  v4l2_buffer buf{};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = index;
  // Errors here are expected after STREAMOFF; ignore.
  dev_->Ioctl(VIDIOC_QBUF, &buf);
}

// ---------------------------------------------------------------------------
// Enumeration / caps
// ---------------------------------------------------------------------------
Status V4l2Source::QueryCaps(const std::string& device, SourceCaps* out) {
  V4l2Device dev;
  Status st = dev.Open(device);
  if (!st.ok()) return st;

  v4l2_capability cap{};
  if (dev.Ioctl(VIDIOC_QUERYCAP, &cap) != 0)
    return Err(ErrorCode::kDeviceError, "QUERYCAP failed");
  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    return Err(ErrorCode::kUnsupported, "not a capture device");

  v4l2_fmtdesc fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  for (fmt.index = 0; dev.Ioctl(VIDIOC_ENUM_FMT, &fmt) == 0; ++fmt.index) {
    PixelFormat pf = FromV4l2Fourcc(fmt.pixelformat);
    bool compressed = (fmt.flags & V4L2_FMT_FLAG_COMPRESSED) != 0;
    FormatDesc& fd = out->AddFormat(
        pf, fmt.pixelformat,
        reinterpret_cast<const char*>(fmt.description), compressed);

    v4l2_frmsizeenum fs{};
    fs.pixel_format = fmt.pixelformat;
    for (fs.index = 0; dev.Ioctl(VIDIOC_ENUM_FRAMESIZES, &fs) == 0; ++fs.index) {
      if (fs.type != V4L2_FRMSIZE_TYPE_DISCRETE) break;
      FrameSize sz;
      sz.width = fs.discrete.width;
      sz.height = fs.discrete.height;

      v4l2_frmivalenum fi{};
      fi.pixel_format = fmt.pixelformat;
      fi.width = fs.discrete.width;
      fi.height = fs.discrete.height;
      for (fi.index = 0;
           dev.Ioctl(VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0; ++fi.index) {
        if (fi.type != V4L2_FRMIVAL_TYPE_DISCRETE) break;
        if (fi.discrete.numerator)
          sz.fps.push_back(static_cast<double>(fi.discrete.denominator) /
                           fi.discrete.numerator);
      }
      fd.sizes.push_back(std::move(sz));
    }
  }
  return Ok();
}

std::vector<DeviceInfo> V4l2Source::Enumerate() {
  std::vector<DeviceInfo> result;
  for (const auto& node : ListVideoNodes()) {
    V4l2Device dev;
    if (!dev.Open(node).ok()) continue;
    v4l2_capability cap{};
    if (dev.Ioctl(VIDIOC_QUERYCAP, &cap) != 0) continue;
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) continue;

    DeviceInfo info;
    info.device = node;
    info.card = reinterpret_cast<const char*>(cap.card);
    info.bus = reinterpret_cast<const char*>(cap.bus_info);
    dev.Close();

    // Filter out capture nodes that expose no pixel formats (e.g. the UVC
    // metadata node) — they cannot produce frames.
    SourceCaps caps;
    if (!QueryCaps(node, &caps).ok() || caps.empty()) {
      XLOG_DEBUG("V4l2 skip {} (no capture formats)", node);
      continue;
    }
    info.caps = std::move(caps);
    info.by_id = ResolveByIdPath(node);
    info.by_path = ResolveByPathPath(node);
    XLOG_INFO("V4l2 device: {} [{}] formats={}", info.device, info.card,
              info.caps.formats.size());
    result.push_back(std::move(info));
  }
  return result;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
V4l2Source::V4l2Source() = default;
V4l2Source::~V4l2Source() { Close(); }

Status V4l2Source::Open(const SourceDescriptor& desc) {
  Close();
  desc_ = desc;
  dev_ = std::make_shared<V4l2Device>();
  Status st = dev_->Open(desc.device);
  if (!st.ok()) return st;

  caps_ = SourceCaps{};
  st = QueryCaps(desc.device, &caps_);
  if (!st.ok()) return st;

  // Remember the stable identities for hot-plug recovery: after a replug the
  // node number may change (/dev/video0 -> /dev/video2); the by-path link is
  // stable per physical port, the by-id link per USB serial.
  char resolved[PATH_MAX];
  std::string node = desc.device;
  if (::realpath(desc.device.c_str(), resolved)) node = resolved;
  by_path_path_ = ResolveByPathPath(node);
  by_id_path_ = ResolveByIdPath(node);
  {
    v4l2_capability cap{};
    if (dev_->Ioctl(VIDIOC_QUERYCAP, &cap) == 0)
      card_ = reinterpret_cast<const char*>(cap.card);
  }

  return NegotiateFormat();
}

Status V4l2Source::NegotiateFormat() {
  // Choose a raw format for now (MJPEG decode branch wired in Phase 1.5).
  PixelFormat want = desc_.format;
  int w = desc_.width, h = desc_.height;
  double fps = desc_.fps;

  if (want == PixelFormat::kUnknown) {
    // Unspecified: default to the first non-compressed format we support (the
    // zero-decode path). Callers may explicitly request MJPEG, which is routed
    // through the GStreamer decode branch in Start().
    for (const auto& f : caps_.formats) {
      if (!f.compressed && ToV4l2Fourcc(f.format) != 0) {
        want = f.format;
        break;
      }
    }
  }
  if (want == PixelFormat::kUnknown)
    return Err(ErrorCode::kUnsupported, "no raw format available");
  if (w == 0 || h == 0) {
    w = 640;
    h = 480;
  }

  v4l2_format fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = w;
  fmt.fmt.pix.height = h;
  fmt.fmt.pix.pixelformat = ToV4l2Fourcc(want);
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  if (dev_->Ioctl(VIDIOC_S_FMT, &fmt) != 0)
    return Err(ErrorCode::kDeviceError,
               std::string("S_FMT: ") + std::strerror(errno));

  neg_format_ = FromV4l2Fourcc(fmt.fmt.pix.pixelformat);
  neg_w_ = fmt.fmt.pix.width;
  neg_h_ = fmt.fmt.pix.height;
  neg_stride_ = fmt.fmt.pix.bytesperline;

  if (fps > 0.0) {
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(fps);
    dev_->Ioctl(VIDIOC_S_PARM, &parm);  // best-effort
  }

  XLOG_INFO("V4l2 negotiated {} {}x{} stride={}", ToString(neg_format_), neg_w_,
            neg_h_, neg_stride_);
  return Ok();
}

Status V4l2Source::SetupBuffers(unsigned count) {
  v4l2_requestbuffers req{};
  req.count = count;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (dev_->Ioctl(VIDIOC_REQBUFS, &req) != 0)
    return Err(ErrorCode::kDeviceError, "REQBUFS failed");
  if (req.count < 2)
    return Err(ErrorCode::kDeviceError, "insufficient buffer memory");

  std::vector<V4l2BufferPool::Slot> slots(req.count);
  for (unsigned i = 0; i < req.count; ++i) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (dev_->Ioctl(VIDIOC_QUERYBUF, &buf) != 0)
      return Err(ErrorCode::kDeviceError, "QUERYBUF failed");
    void* p = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                     dev_->fd(), buf.m.offset);
    if (p == MAP_FAILED)
      return Err(ErrorCode::kIo, "mmap failed");
    slots[i].start = p;
    slots[i].length = buf.length;
  }

  pool_ = std::make_shared<V4l2BufferPool>(dev_, std::move(slots));

  // Queue all buffers, then start streaming.
  for (unsigned i = 0; i < req.count; ++i) pool_->Requeue(i);
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (dev_->Ioctl(VIDIOC_STREAMON, &type) != 0)
    return Err(ErrorCode::kDeviceError, "STREAMON failed");
  return Ok();
}

Status V4l2Source::Start() {
  if (running_.load()) return Ok();
  if (!dev_ || !dev_->IsOpen())
    return Err(ErrorCode::kInternal, "Start() before Open()");
  if (IsCompressed(neg_format_)) {
#ifdef XMCAM_WITH_GSTREAMER
    if (neg_format_ != PixelFormat::kMjpeg)
      return Err(ErrorCode::kUnsupported, "only MJPEG compressed capture");
    jpeg_decoder_ = std::make_unique<GstJpegDecoder>();
    if (auto ds = jpeg_decoder_->Open(); !ds.ok()) return ds;
    XLOG_INFO("V4l2Source MJPEG decode branch enabled");
#else
    return Err(ErrorCode::kUnsupported,
               "compressed capture needs the GStreamer decode branch");
#endif
  }

  Status st = SetupBuffers(kBufferCount);
  if (!st.ok()) return st;

  running_.store(true);
  seq_ = 0;
  capture_rate_.Reset();
  thread_ = std::thread(&V4l2Source::CaptureLoop, this);
  XLOG_INFO("V4l2Source started ({} {}x{})", ToString(neg_format_), neg_w_,
            neg_h_);
  return Ok();
}

void V4l2Source::CaptureLoop() {
  while (running_.load()) {
    pollfd pfd{dev_->fd(), POLLIN, 0};
    int pr = ::poll(&pfd, 1, 1000);
    if (pr < 0) {
      if (errno == EINTR) continue;
      XLOG_ERROR("V4l2 poll error: {}", std::strerror(errno));
      if (!RecoverDevice()) break;
      continue;
    }
    if (pr == 0) {
      XLOG_WARN("V4l2 capture timeout (no frame in 1s)");
      continue;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      XLOG_ERROR("V4l2 device error (revents=0x{:x}) — attempting recovery",
                 pfd.revents);
      if (!RecoverDevice()) break;
      continue;
    }

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (dev_->Ioctl(VIDIOC_DQBUF, &buf) != 0) {
      if (errno == EAGAIN) continue;
      const int e = errno;
      XLOG_ERROR("V4l2 DQBUF error: {}", std::strerror(e));
      if (e == ENODEV || e == EIO || e == ENXIO) {
        if (!RecoverDevice()) break;
        continue;
      }
      break;
    }

#ifdef XMCAM_WITH_GSTREAMER
    // MJPEG branch: decode into a fresh RGBA frame, then requeue the JPEG slot
    // immediately (the decoder copied the bytes, so the slot is free to reuse).
    if (jpeg_decoder_) {
      VideoFrame decoded;
      const uint8_t* jpeg =
          static_cast<const uint8_t*>(pool_->slot(buf.index).start);
      double decode_ms = 0.0;
      Status ds;
      {
        ScopeTimer t("mjpeg_decode", &decode_ms);
        ds = jpeg_decoder_->Decode(jpeg, buf.bytesused,
                                   TimevalToNs(buf.timestamp), seq_, &decoded);
      }
      pool_->Requeue(buf.index);
      if (ds.ok()) {
        decoded.seq = seq_++;
        decoded.hw_seq = buf.sequence;
        const double fps = capture_rate_.Tick();
        {
          std::lock_guard<std::mutex> lk(stats_mtx_);
          stats_.capture_fps = fps;
          stats_.frames = capture_rate_.count();
          stats_.decode_ms = decode_ms;
          stats_.decoder = "mjpeg->i420";
        }
        EmitFrame(decoded);
        frames_.Push(std::move(decoded));
      } else {
        XLOG_WARN("MJPEG decode failed: {}", ds.message());
      }
      continue;
    }
#endif

    // Wrap the mmap slot in a VideoFrame; the lease re-queues the slot when the
    // last frame copy is released (by the render thread or DataStream overwrite).
    auto pool = pool_;
    const unsigned index = buf.index;
    struct FrameLease {
      std::shared_ptr<V4l2BufferPool> pool;
      unsigned index;
      ~FrameLease() { pool->Requeue(index); }
    };
    auto lease = std::make_shared<FrameLease>(FrameLease{pool, index});

    VideoFrame f;
    f.width = neg_w_;
    f.height = neg_h_;
    f.format = neg_format_;
    f.stride = neg_stride_;
    f.data = static_cast<const uint8_t*>(pool->slot(index).start);
    f.pts_ns = TimevalToNs(buf.timestamp);
    f.seq = seq_++;
    f.hw_seq = buf.sequence;
    f.owner = lease;

    const double fps = capture_rate_.Tick();
    {
      std::lock_guard<std::mutex> lk(stats_mtx_);
      stats_.capture_fps = fps;
      stats_.frames = capture_rate_.count();
      stats_.decoder = "raw";
    }
    EmitFrame(f);
    frames_.Push(std::move(f));
  }
}

bool V4l2Source::RecoverDevice() {
  {
    std::lock_guard<std::mutex> lk(stats_mtx_);
    stats_.device_lost = true;
  }
  // Drop the dead device's pool; in-flight frame leases keep it (and its fd)
  // alive until they release, so this is safe with zero-copy frames out.
  pool_.reset();
  dev_.reset();

  // Candidate order: by-path (same physical port — unambiguous even with
  // duplicate USB serials), then by-id (survives a port change), then the
  // original node. The card name is verified so we never silently adopt a
  // different device that landed on the same handle.
  std::vector<std::string> candidates;
  if (!by_path_path_.empty()) candidates.push_back(by_path_path_);
  if (!by_id_path_.empty()) candidates.push_back(by_id_path_);
  candidates.push_back(desc_.device);

  int backoff_ms = 200;
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
    backoff_ms = std::min(backoff_ms * 2, 2000);

    for (const auto& path : candidates) {
      auto dev = std::make_shared<V4l2Device>();
      if (!dev->Open(path).ok()) continue;

      if (!card_.empty()) {
        v4l2_capability cap{};
        if (dev->Ioctl(VIDIOC_QUERYCAP, &cap) != 0 ||
            card_ != reinterpret_cast<const char*>(cap.card)) {
          XLOG_WARN("V4l2 recovery: {} is a different device — skipping",
                    path);
          continue;
        }
      }
      dev_ = std::move(dev);

      if (!NegotiateFormat().ok() || !SetupBuffers(kBufferCount).ok()) {
        dev_.reset();
        pool_.reset();
        continue;
      }

      ++generation_;
      {
        std::lock_guard<std::mutex> lk(stats_mtx_);
        stats_.device_lost = false;
        stats_.generation = generation_;
      }
      capture_rate_.Reset();
      XLOG_INFO("V4l2 device recovered via {} (generation {})", path,
                generation_);
      return true;
    }
  }
  return false;  // Stop() requested while recovering
}

void V4l2Source::Stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
  // Release our pool reference; STREAMOFF/munmap happen in the pool destructor
  // once all outstanding frame leases are released.
  pool_.reset();
#ifdef XMCAM_WITH_GSTREAMER
  jpeg_decoder_.reset();
#endif
  XLOG_INFO("V4l2Source stopped ({} frames)", seq_);
}

void V4l2Source::Close() {
  Stop();
  dev_.reset();  // fd closes when the last (pool) reference drops
  caps_ = SourceCaps{};
  neg_format_ = PixelFormat::kUnknown;
}

SourceStats V4l2Source::GetStats() const {
  std::lock_guard<std::mutex> lk(stats_mtx_);
  return stats_;
}

}  // namespace xmotion
