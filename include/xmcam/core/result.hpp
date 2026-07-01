/*
 * @file result.hpp
 * @brief Lightweight status/result types. No exceptions across thread bounds.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_RESULT_HPP
#define XMCAM_CORE_RESULT_HPP

#include <string>
#include <utility>

namespace xmotion {

enum class ErrorCode {
  kOk,
  kNotFound,
  kIo,
  kInvalidArgument,
  kUnsupported,
  kDeviceError,
  kTimeout,
  kInternal,
};

const char* ToString(ErrorCode c);

// A movable, [[nodiscard]] status. Default-constructed == ok.
class [[nodiscard]] Status {
 public:
  Status() = default;
  Status(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status Ok() { return Status{}; }

  bool ok() const { return code_ == ErrorCode::kOk; }
  ErrorCode code() const { return code_; }
  const std::string& message() const { return message_; }

  explicit operator bool() const { return ok(); }

 private:
  ErrorCode code_ = ErrorCode::kOk;
  std::string message_;
};

// Convenience constructors.
inline Status Ok() { return Status::Ok(); }
inline Status Err(ErrorCode c, std::string msg) {
  return Status{c, std::move(msg)};
}

}  // namespace xmotion

#endif  // XMCAM_CORE_RESULT_HPP
