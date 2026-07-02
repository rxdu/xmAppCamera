/*
 * @file qualify_panel.hpp
 * @brief Production-camera qualification checklist. Runs the automated checks
 *        (control locks, timestamp stability, platform identity) on a worker
 *        thread against the live source, drives the operator-in-the-loop
 *        checks (power-cycle identity, disconnect recovery), records the
 *        vendor/procurement answers, and exports a YAML+Markdown report.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_QUALIFY_PANEL_HPP
#define XMCAM_UI_QUALIFY_PANEL_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"
#include "xmcam/qualify/frame_tap.hpp"
#include "xmcam/qualify/qual_types.hpp"

namespace xmotion {

class QualifyPanel : public quickviz::Panel {
 public:
  explicit QualifyPanel(AppController* app);
  ~QualifyPanel() override;

  void Draw() override;

 private:
  enum class Phase { kIdle, kAuto, kCycleWaiting, kCycleVerify, kDone };

  void StartAutomatedRun();
  void StartPowerCycleCheck(bool expect_stream_recovery);
  void JoinWorker();
  void PutResult(const QualCheckResult& r);  // worker -> shared results
  void ExportReport();

  AppController* app_;
  FrameTap tap_;
  bool tap_attached_ = false;
  bool auto_run_pending_ = false;  // XMCAM_AUTOQUALIFY headless-test hook
  bool auto_export_ = false;

  std::thread worker_;
  std::atomic<bool> worker_busy_{false};
  std::atomic<bool> abort_{false};

  mutable std::mutex mtx_;
  std::vector<QualCheckResult> results_;
  PlatformInfo platform_;
  std::string report_msg_;

  // Vendor / procurement answers (manual fields).
  char fw_policy_[256] = "";
  char bulk_rev_[256] = "";
  char lens_pn_[256] = "";
  char ir_filter_[256] = "";
  char isp_docs_[256] = "";
  char notes_[512] = "";
};

}  // namespace xmotion

#endif  // XMCAM_UI_QUALIFY_PANEL_HPP
