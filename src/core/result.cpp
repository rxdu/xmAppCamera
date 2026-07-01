/*
 * @file result.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/core/result.hpp"

namespace xmotion {

const char* ToString(ErrorCode c) {
  switch (c) {
    case ErrorCode::kOk: return "Ok";
    case ErrorCode::kNotFound: return "NotFound";
    case ErrorCode::kIo: return "Io";
    case ErrorCode::kInvalidArgument: return "InvalidArgument";
    case ErrorCode::kUnsupported: return "Unsupported";
    case ErrorCode::kDeviceError: return "DeviceError";
    case ErrorCode::kTimeout: return "Timeout";
    case ErrorCode::kInternal: return "Internal";
    default: return "Unknown";
  }
}

}  // namespace xmotion
