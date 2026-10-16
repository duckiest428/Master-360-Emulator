/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_DASHBOARD_UI_H_
#define XENIA_APP_DASHBOARD_DASHBOARD_UI_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "xenia/app/dashboard/dashboard_controller.h"
#include "xenia/app/dashboard/dashboard_input.h"
#include "xenia/app/dashboard/dashboard_painter.h"
#include "xenia/ui/imgui_dialog.h"

namespace xe {
namespace app {
namespace dashboard {

// One selectable item on a channel (NXE) or blade (Blades).
struct HomeItem {
  std::string title;
  std::string subtitle;
  ImU32 color = theme::kXboxGreen;
  ui::ImmediateTexture* image = nullptr;
  bool image_is_cover = false;
  // Requires services that are not available (Xbox LIVE, media sources).
  bool unavailable = false;
  std::function<void()> action;
};

struct HomeSection {
  std::string name;
  ImU32 accent = theme::kXboxGreen;
  std::vector<HomeItem> items;
};

// Full-screen home shown while no title is running. Drawn into ImGui's
// background draw list so dialogs it opens always appear above it.
class DashboardDialog final : public ui::ImGuiDialog {
 public:
  DashboardDialog(ui::ImGuiDrawer* imgui_drawer,
                  DashboardController* controller);
  ~DashboardDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Screen : uint8_t {
    kHome,
    kLibrary,
    kGameDetails,
    kProfiles,
    kSettings,
    kMarketplace,
  };

  struct SettingRow {
    std::string label;
    std::string value;
    std::function<void(int direction)> on_change;  // left/right
    std::function<void()> on_activate;             // A
  };

  void BuildSections();
  void PushScreen(Screen screen);
  void PopScreen();

  // Home (style specific).
  void UpdateNxeHome();
  void DrawNxeHome(Painter& p);
  void DrawNxeTile(Painter& p, const HomeItem& item, ImVec2 origin,
                   float width, float height, float perspective,
                   float alpha, bool focused);
  void UpdateBladesHome();
  void DrawBladesHome(Painter& p);
  void DrawBladeContent(Painter& p, const HomeSection& section,
                        size_t section_index, float x0, float x1);

  // Shared screens.
  void UpdateLibrary();
  void DrawLibrary(Painter& p);
  void UpdateGameDetails();
  void DrawGameDetails(Painter& p);
  void UpdateProfiles();
  void DrawProfiles(Painter& p);
  std::vector<SettingRow> BuildSettings();
  void UpdateSettings();
  void DrawSettings(Painter& p);
  void UpdateMarketplace();
  void DrawMarketplace(Painter& p);

  void DrawBackground(Painter& p);
  void DrawGamercardBadge(Painter& p, ImVec2 top_right, bool on_dark);
  void DrawScreenHeader(Painter& p, const std::string& title,
                        const std::string& subtitle);
  void DrawToast(Painter& p);
  ImU32 ScreenAccent() const;
  bool on_dark_background() const;

  void Activate(const HomeItem& item);
  void OpenGame(std::shared_ptr<const GameEntry> game);

  DashboardController* controller_;
  DashboardInput input_;
  std::vector<Screen> screen_stack_;
  float time_ = 0.f;
  float delta_time_ = 0.f;
  float screen_fade_ = 1.f;

  // Home content, rebuilt as the library/profile changes.
  std::vector<HomeSection> sections_;
  uint32_t built_library_generation_ = UINT32_MAX;
  DashboardStyle built_style_ = DashboardStyle::kNxe;
  float rebuild_timer_ = 0.f;

  // NXE state: channel index and per-channel tile index, plus the animated
  // positions that chase them.
  size_t channel_ = 1;
  std::vector<size_t> tile_index_;
  float channel_anim_ = 1.f;
  std::vector<float> tile_anim_;

  // Blades state.
  size_t blade_ = 1;
  float blade_anim_ = 1.f;
  std::vector<size_t> blade_item_;

  // Library / details.
  GameList games_;
  size_t library_index_ = 0;
  float library_scroll_ = 0.f;
  int library_sort_ = 0;  // 0 = A-Z, 1 = title ID, 2 = source
  std::shared_ptr<const GameEntry> details_game_;

  // Profiles / settings / marketplace.
  size_t profile_index_ = 0;
  size_t setting_index_ = 0;
  size_t market_index_ = 0;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_UI_H_
