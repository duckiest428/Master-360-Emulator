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

// Small glyphs drawn at the right edge of guide rows.
enum class GuideIcon : uint8_t {
  kNone,
  kFriend,
  kParty,
  kMessage,
  kBeacon,
  kChat,
  kDisc,
  kTrophy,
  kMusic,
  kGear,
  kHome,
};

// The in-game Xbox Guide, drawn over the title's last frame (the D3D12/Vulkan
// presenter keeps showing it while guest threads are suspended). Two skins:
//  - Metro (2011+): "Xbox Guide" header, slate tab blades either side of a
//    light list with a green highlight.
//  - Blades (2008 NXE): light perspective tab blades either side of a dark
//    translucent list with a green pill highlight.
// Everything shown is captured when it opens, before guest threads stop.
class GuideOverlay final : public ui::ImGuiDialog {
 public:
  GuideOverlay(ui::ImGuiDrawer* imgui_drawer, DashboardController* controller,
               bool in_title);
  ~GuideOverlay() override;

  // Starts the closing animation; the controller destroys the overlay once
  // it reports completion through OnGuideAnimationFinished.
  void BeginClose();
  // Cancels a close in progress (Guide pressed again while closing).
  void Reopen();
  bool is_closing() const { return closing_; }
  bool in_title() const { return in_title_; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Row {
    std::string label;
    std::string count;  // Right-aligned value ("0", "On", "3 of 50").
    GuideIcon icon = GuideIcon::kNone;
    bool unavailable = false;
    std::function<void()> action;
  };
  struct Tab {
    std::string name;
    std::vector<Row> rows;
  };
  enum class Confirm : uint8_t { kNone, kQuit, kSignOut };

  bool metro() const { return metro_; }
  void BuildTabs();
  void HandleInput();

  void Draw(Painter& p);
  void DrawHeader(Painter& p, float alpha);
  void DrawTabs(Painter& p, float alpha, float spread);
  void DrawRows(Painter& p, float alpha);
  void DrawFooter(Painter& p, float alpha);
  void DrawConfirm(Painter& p, float alpha);
  void DrawIcon(Painter& p, GuideIcon icon, ImVec2 center, float size,
                ImU32 color);

  // Horizontal extent of tab `index` when tab `current` is open.
  std::pair<float, float> TabSpan(size_t index, size_t current) const;

  DashboardController* controller_;
  DashboardInput input_;
  const bool in_title_;
  const bool metro_;

  ProfileCard card_;
  std::string title_name_;
  uint32_t title_id_ = 0;
  uint32_t title_achievements_unlocked_ = 0;
  uint32_t title_achievements_total_ = 0;

  std::vector<Tab> tabs_;
  size_t tab_ = 0;
  size_t home_tab_ = 0;
  std::vector<size_t> selection_;
  float tab_anim_ = 0.f;
  float row_anim_ = 0.f;
  float open_anim_ = 0.f;
  bool closing_ = false;
  bool close_reported_ = false;
  Confirm confirm_ = Confirm::kNone;
  float time_ = 0.f;
  float delta_time_ = 0.f;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_GUIDE_OVERLAY_H_
