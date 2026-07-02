/*
 * @file qual_types.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/qualify/qual_types.hpp"

namespace xmotion {

const char* ToString(QualStatus s) {
  switch (s) {
    case QualStatus::kPending:
      return "PENDING";
    case QualStatus::kRunning:
      return "RUNNING";
    case QualStatus::kPass:
      return "PASS";
    case QualStatus::kFail:
      return "FAIL";
    case QualStatus::kSkipped:
      return "SKIPPED";
    case QualStatus::kActionRequired:
      return "ACTION_REQUIRED";
  }
  return "UNKNOWN";
}

}  // namespace xmotion
