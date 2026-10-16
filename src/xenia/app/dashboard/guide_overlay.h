/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_GUIDE_OVERLAY_H_
#define XENIA_APP_DASHBOARD_GUIDE_OVERLAY_H_

#include <functional>
#include <string>
#include <vector>

#include "xenia/app/dashboard/dashboard_controller.h"
#include "xenia/app/dashboard/dashboard_input.h"
#include "xenia/app/dashboard/dashboard_painter.h"
#include "xenia/ui/imgui_dialog.h"

namespace xe {
namespace app {
namespace dashboard {

// The Xbox Guide: a semi-transparent side blade drawn over the title's last
// frame (the D3D12/Vulkan presenter keeps showing it while the title is
// suspended). Everything it shows is captured when it opens, before guest
// threads are suspended.
class GuideOverlay final : public ui::ImGuiDialog {
 public:
  GuideOverlay(ui::ImGuiDrawer* imgui_drawer, DashboardController* controller,
               bool in_title);
  ~GuideOverlay() override;

  // Starts the slide-out animation; the controller destroys the overlay once
  // IsFullyClosed() reports true.
  void BeginClose();
  // Cancels a close in progress (Guide pressed again while sliding out).
  void Reopen();
  bool is_closing() const { return closing_; }
  bool in_title() const { return in_title_; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Entry {
    std::string label;
    std::string detail;
    bool unavailable = false;
    std::function<void()> action;
  };
  struct Tab {
    std::string name;
    std::vector<Entry> entries;
  };

  void BuildTabs();
  void HandleInput();
  void Draw(Painter& p);

  DashboardController* controller_;
  DashboardInput input_;
  const bool in_title_;

  ProfileCard card_;
  std::string title_name_;
  uint32_t title_id_ = 0;
  ui::ImmediateTexture* title_icon_ = nullptr;
  uint32_t title_achievements_unlocked_ = 0;
  uint32_t title_achievements_total_ = 0;
  uint32_t title_gamerscore_earned_ = 0;
  uint32_t title_gamerscore_total_ = 0;

  std::vector<Tab> tabs_;
  size_t tab_ = 0;
  std::vector<size_t> selection_;
  float tab_anim_ = 0.f;
  float open_anim_ = 0.f;
  bool closing_ = false;
  bool close_reported_ = false;
  bool confirm_quit_ = false;
  float time_ = 0.f;
  float delta_time_ = 0.f;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_GUIDE_OVERLAY_H_
