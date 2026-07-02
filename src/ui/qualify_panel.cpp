/*
 * @file qualify_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/qualify_panel.hpp"

#include <linux/videodev2.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "imgui.h"

#include "xmcam/qualify/qual_checks.hpp"
#include "xmcam/qualify/qual_report.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

ImVec4 StatusColor(QualStatus s) {
  switch (s) {
    case QualStatus::kPass: return ImVec4(0.2f, 1.0f, 0.2f, 1);
    case QualStatus::kFail: return ImVec4(1.0f, 0.3f, 0.3f, 1);
    case QualStatus::kRunning: return ImVec4(1.0f, 0.8f, 0.2f, 1);
    case QualStatus::kActionRequired: return ImVec4(0.3f, 0.7f, 1.0f, 1);
    case QualStatus::kSkipped: return ImVec4(0.6f, 0.6f, 0.6f, 1);
    default: return ImVec4(0.5f, 0.5f, 0.5f, 1);
  }
}

std::string NowIso() {
  char buf[32];
  std::time_t t = std::time(nullptr);
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
  return buf;
}

}  // namespace

QualifyPanel::QualifyPanel(AppController* app)
    : quickviz::Panel("Qualify"), app_(app) {
  this->SetAutoLayout(false);
  // Headless-test hook: run the automated checks (and export the report) as
  // soon as a source is streaming, without operator clicks.
  if (std::getenv("XMCAM_AUTOQUALIFY")) {
    auto_run_pending_ = true;
    auto_export_ = true;
  }
}

QualifyPanel::~QualifyPanel() {
  abort_.store(true);
  JoinWorker();
}

void QualifyPanel::JoinWorker() {
  if (worker_.joinable()) worker_.join();
  if (tap_attached_) {
    app_->DetachFrameSink(&tap_);
    tap_attached_ = false;
  }
}

void QualifyPanel::PutResult(const QualCheckResult& r) {
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto& e : results_) {
    if (e.id == r.id) {
      e = r;
      return;
    }
  }
  results_.push_back(r);
}

void QualifyPanel::StartAutomatedRun() {
  if (worker_busy_.load()) return;
  JoinWorker();
  abort_.store(false);

  // The tap must be attached from the render thread before the worker starts.
  tap_attached_ = app_->AttachFrameSink(&tap_);
  {
    std::lock_guard<std::mutex> lk(mtx_);
    results_.clear();
    report_msg_.clear();
  }

  worker_busy_.store(true);
  const std::string device = app_->active_device();
  worker_ = std::thread([this, device] {
    // 1. Platform + firmware identity.
    QualCheckResult pr{"platform", "Platform & firmware identity",
                       QualStatus::kRunning, "", {}};
    PutResult(pr);
    PlatformInfo pi;
    Status st = CollectPlatformInfo(device, &pi);
    {
      std::lock_guard<std::mutex> lk(mtx_);
      platform_ = pi;
    }
    pr.status = st.ok() && !pi.kernel.empty() ? QualStatus::kPass
                                              : QualStatus::kFail;
    pr.detail = "kernel " + pi.kernel +
                (pi.jetpack.empty() ? "" : " / " + pi.jetpack);
    pr.metrics = {{"driver", pi.driver},
                  {"usb_vid_pid", pi.usb_vid_pid},
                  {"fw_bcdDevice", pi.usb_bcd_device},
                  {"serial", pi.usb_serial}};
    PutResult(pr);

    // 2. Enumeration (formats + controls present).
    ControlSet* cs = app_->Controls();
    SourceCaps caps;
    for (const auto& d : app_->devices())
      if (d.device == device) caps = d.caps;
    if (cs) {
      PutResult(CheckEnumeration(caps, cs->controls()));
    } else {
      PutResult({"enumeration", "Formats & controls enumerable",
                 QualStatus::kSkipped, "no control set (not a V4L2 source)",
                 {}});
    }

    // 3-5. Lock checks (2s hold each). Gain has no dedicated auto control on
    // UVC; manual exposure mode is what stops AE from driving it.
    if (cs && !abort_.load()) {
      PutResult(CheckControlLock(*cs, "exposure_lock",
                                 "Exposure fully lockable",
                                 V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL,
                                 V4L2_CID_EXPOSURE_ABSOLUTE, 200, 2000));
      PutResult(CheckControlLock(*cs, "gain_lock", "Gain fully lockable",
                                 V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL,
                                 V4L2_CID_GAIN, 64, 2000));
      PutResult(CheckControlLock(*cs, "awb_disable", "AWB fully disableable",
                                 V4L2_CID_AUTO_WHITE_BALANCE, 0,
                                 V4L2_CID_WHITE_BALANCE_TEMPERATURE, 4600,
                                 2000));
    }

    // 6. Timestamp stability over a 3s window. The nominal rate is read
    // AFTER the window: at run start (right after autostart) the smoothed
    // capture_fps may still be 0.
    if (!abort_.load()) {
      PutResult({"timestamp_stability", "Timestamp stability",
                 QualStatus::kRunning, "sampling 3s...", {}});
      tap_.Reset();
      std::this_thread::sleep_for(std::chrono::seconds(3));
      const auto snap = tap_.Take();
      const double nominal_fps = app_->Stats().capture_fps;
      PutResult(CheckTimestampStability(snap.pts_ns, nominal_fps));
    }

    worker_busy_.store(false);
  });
}

void QualifyPanel::StartPowerCycleCheck(bool expect_stream_recovery) {
  if (worker_busy_.load()) return;
  JoinWorker();
  abort_.store(false);
  tap_attached_ = app_->AttachFrameSink(&tap_);

  const char* id = expect_stream_recovery ? "disconnect_recovery"
                                          : "power_cycle_identity";
  const char* title = expect_stream_recovery
                          ? "Recovery after USB disconnect"
                          : "Image identical after power cycle";
  worker_busy_.store(true);
  worker_ = std::thread([this, id, title, expect_stream_recovery] {
    using namespace std::chrono;
    // Phase A: reference fingerprint under the current (fixed!) scene.
    PutResult({id, title, QualStatus::kRunning, "capturing reference (2s)...",
               {}});
    tap_.Reset();
    std::this_thread::sleep_for(seconds(2));
    const auto before = tap_.Take();
    const uint32_t gen0 = app_->Stats().generation;

    PutResult({id, title, QualStatus::kActionRequired,
               "UNPLUG the camera (or power-cycle it), then plug it back in. "
               "Keep the scene and lighting fixed. Waiting up to 90s...",
               {}});

    // Phase B: wait for loss, then recovery (generation bump).
    const auto deadline = steady_clock::now() + seconds(90);
    bool saw_loss = false, recovered = false;
    steady_clock::time_point lost_at{}, back_at{};
    while (steady_clock::now() < deadline && !abort_.load()) {
      const SourceStats s = app_->Stats();
      if (s.device_lost && !saw_loss) {
        saw_loss = true;
        lost_at = steady_clock::now();
        PutResult({id, title, QualStatus::kRunning,
                   "device lost — waiting for it to come back...", {}});
      }
      if (saw_loss && !s.device_lost && s.generation > gen0) {
        recovered = true;
        back_at = steady_clock::now();
        break;
      }
      std::this_thread::sleep_for(milliseconds(100));
    }

    QualCheckResult r{id, title, QualStatus::kFail, "", {}};
    if (!saw_loss) {
      r.detail = "device was never disconnected (timeout)";
    } else if (!recovered) {
      r.detail = "device did not recover within 90s";
    } else {
      const double rec_s =
          duration_cast<milliseconds>(back_at - lost_at).count() / 1000.0;
      r.metrics.push_back({"recovery_s", std::to_string(rec_s)});
      // Settle, then verify the stream actually delivers frames again.
      std::this_thread::sleep_for(seconds(3));
      tap_.Reset();
      std::this_thread::sleep_for(seconds(2));
      const auto after = tap_.Take();
      if (after.frames < 10) {
        r.detail = "recovered but stream did not resume";
      } else if (expect_stream_recovery) {
        r.status = QualStatus::kPass;
        r.detail = "recovered in " + std::to_string(rec_s) +
                   "s, stream resumed (" + std::to_string(after.frames) +
                   " frames)";
      } else {
        // Power-cycle identity: compare image fingerprints (3% tolerance).
        QualCheckResult cmp = CompareImageFingerprint(before, after, 3.0);
        r.status = cmp.status;
        r.detail = cmp.detail + " (recovered in " + std::to_string(rec_s) +
                   "s)";
        r.metrics.insert(r.metrics.end(), cmp.metrics.begin(),
                         cmp.metrics.end());
      }
    }
    PutResult(r);
    worker_busy_.store(false);
  });
}

void QualifyPanel::ExportReport() {
  QualReport rep;
  rep.device = app_->active_device();
  for (const auto& d : app_->devices())
    if (d.device == rep.device) {
      rep.card = d.card;
      rep.by_id = d.by_id;
    }
  {
    std::lock_guard<std::mutex> lk(mtx_);
    rep.platform = platform_;
    rep.results = results_;
  }
  rep.manual_fields = {
      {"firmware_update_freeze_policy", fw_policy_},
      {"bulk_hardware_revision_guarantee", bulk_rev_},
      {"lens_part_number", lens_pn_},
      {"ir_filter_spec", ir_filter_},
      {"isp_control_documentation", isp_docs_},
      {"notes", notes_},
  };
  rep.finished_at = NowIso();
  rep.started_at = rep.finished_at;

  char base[64];
  std::time_t t = std::time(nullptr);
  std::strftime(base, sizeof base, "qual_report_%Y%m%d_%H%M%S",
                std::localtime(&t));
  const std::string yaml = std::string(base) + ".yaml";
  const std::string md = std::string(base) + ".md";
  Status s1 = WriteReportYaml(rep, yaml);
  Status s2 = WriteReportMarkdown(rep, md);
  std::lock_guard<std::mutex> lk(mtx_);
  report_msg_ = (s1.ok() && s2.ok()) ? "wrote " + yaml + " + " + md
                                     : "export failed: " + s1.message() +
                                           " " + s2.message();
}

void QualifyPanel::Draw() {
  Begin();
  {
    // Reap a finished worker so the tap detaches promptly.
    if (!worker_busy_.load() && worker_.joinable()) {
      JoinWorker();
      if (auto_export_ && !results_.empty()) {
        auto_export_ = false;
        ExportReport();
      }
    }
    if (auto_run_pending_ && app_->IsRunning() && app_->Controls() &&
        !worker_busy_.load()) {
      auto_run_pending_ = false;
      StartAutomatedRun();
    }

    if (!app_->IsRunning() || !app_->Controls()) {
      ImGui::TextWrapped(
          "Start a USB camera (Device panel) to run qualification.");
      End();
      return;
    }

    const bool busy = worker_busy_.load();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Run automated checks")) StartAutomatedRun();
    if (ImGui::Button("Power-cycle identity check"))
      StartPowerCycleCheck(false);
    ImGui::SameLine();
    if (ImGui::Button("Disconnect recovery check"))
      StartPowerCycleCheck(true);
    if (busy) ImGui::EndDisabled();
    if (busy) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "running...");
    }

    ImGui::Separator();
    {
      std::lock_guard<std::mutex> lk(mtx_);
      for (const auto& r : results_) {
        ImGui::TextColored(StatusColor(r.status), "[%s]", ToString(r.status));
        ImGui::SameLine();
        ImGui::TextUnformatted(r.title.c_str());
        if (!r.detail.empty()) {
          ImGui::Indent();
          ImGui::TextWrapped("%s", r.detail.c_str());
          ImGui::Unindent();
        }
      }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Vendor / procurement record")) {
      ImGui::InputText("FW update/freeze policy", fw_policy_, sizeof fw_policy_);
      ImGui::InputText("Bulk HW revision guarantee", bulk_rev_, sizeof bulk_rev_);
      ImGui::InputText("Lens part number", lens_pn_, sizeof lens_pn_);
      ImGui::InputText("IR-filter spec", ir_filter_, sizeof ir_filter_);
      ImGui::InputText("ISP control docs", isp_docs_, sizeof isp_docs_);
      ImGui::InputText("Notes", notes_, sizeof notes_);
    }

    if (ImGui::Button("Export report (YAML + Markdown)")) ExportReport();
    {
      std::lock_guard<std::mutex> lk(mtx_);
      if (!report_msg_.empty()) ImGui::TextWrapped("%s", report_msg_.c_str());
    }
  }
  End();
}

}  // namespace xmotion
