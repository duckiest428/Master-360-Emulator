/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_DASHBOARD_CONTROLLER_H_
#define XENIA_APP_DASHBOARD_DASHBOARD_CONTROLLER_H_

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "xenia/app/dashboard/dashboard_audio.h"
#include "xenia/app/dashboard/game_library.h"
#include "xenia/kernel/xam/user_tracker.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
class Emulator;
namespace ui {
class ImGuiDrawer;
}  // namespace ui
namespace app {
class EmulatorWindow;
}  // namespace app
}  // namespace xe

namespace xe {
namespace app {
namespace dashboard {

class DashboardDialog;
class GuideOverlay;

enum class DashboardStyle : uint8_t {
  kNxe,     // New Xbox Experience (2008): channels of tiles.
  kBlades,  // Launch dashboard (2005): sliding blades.
};

// Lazily uploads PNG/JPEG data to immediate-drawer textures, keyed by a
// stable string. Must only be used on the UI thread.
class TextureCache {
 public:
  explicit TextureCache(ui::ImGuiDrawer* imgui_drawer)
      : imgui_drawer_(imgui_drawer) {}

  ui::ImmediateTexture* Get(const std::string& key,
                            std::span<const uint8_t> image_data);
  void Remove(const std::string& key);
  void Clear();

 private:
  ui::ImGuiDrawer* imgui_drawer_;
  std::map<std::string, std::unique_ptr<ui::ImmediateTexture>> textures_;
  std::set<std::string> failed_;
};

// What the dashboard shows for the signed-in player (gamercard).
struct ProfileCard {
  bool signed_in = false;
  uint8_t user_index = 0;
  uint64_t xuid = 0;
  std::string gamertag;
  std::string motto;
  std::string zone;
  uint32_t gamerscore = 0;
  uint32_t titles_played = 0;
  uint32_t achievements_unlocked = 0;
  ui::ImmediateTexture* gamerpic = nullptr;
  ui::ImmediateTexture* avatar_image = nullptr;
};

struct RecentTitle {
  std::string title_name;
  std::filesystem::path path;
  std::shared_ptr<const GameEntry> game;  // Library entry, if known.
};

// Owns the dashboard shell: the home screen shown when no title is running,
// the in-game Guide, the game library and the glue to Xenia's profile,
// content and settings subsystems.
class DashboardController {
 public:
  explicit DashboardController(EmulatorWindow* emulator_window);
  ~DashboardController();

  // UI thread, once the emulator is set up.
  void Initialize();

  EmulatorWindow* emulator_window() const { return emulator_window_; }
  Emulator* emulator() const;
  ui::ImGuiDrawer* imgui_drawer() const;
  GameLibrary& library() { return *library_; }
  DashboardAudio& audio() { return *audio_; }
  TextureCache& textures() { return *textures_; }

  DashboardStyle style() const;
  void SetStyle(DashboardStyle style);
  void SetSoundsEnabled(bool enabled);
  // Xbox Guide skin: Metro (2011+) or Blades (2008 NXE).
  bool guide_metro() const;
  void ToggleGuideStyle();

  // Gamercard of the player signed in to `user_index`.
  ProfileCard GetProfileCard(uint8_t user_index = 0);
  std::vector<kernel::xam::TitleInfo> GetPlayedTitles(uint64_t xuid);
  std::vector<RecentTitle> GetRecentTitles(size_t max_count);
  ui::ImmediateTexture* GetGameIcon(const GameEntry& game);
  ui::ImmediateTexture* GetGameCover(const GameEntry& game);

  // Home screen.
  void ShowDashboard();
  void HideDashboard();
  bool IsDashboardVisible() const { return dashboard_ != nullptr; }

  // Guide.
  void ToggleGuide();
  void OpenGuide();
  void CloseGuide();
  bool IsGuideOpen() const { return guide_ != nullptr; }
  // Called by the guide when its slide-out animation completes.
  void OnGuideAnimationFinished();

  // Actions. All are safe to call from inside a dialog's OnDraw: anything
  // that tears down dialogs is deferred to the next UI loop iteration.
  void LaunchGame(const std::filesystem::path& path);
  void ChooseLibraryFolder();
  void RescanLibrary();
  // Unloads the running title by restarting Xenia into the dashboard, the
  // emulator equivalent of the console rebooting into the dashboard.
  void RestartToDashboard();

  void SignIn(uint64_t xuid, uint8_t user_index);
  void SignOut(uint8_t user_index);
  void OpenGamercardEditor(uint64_t xuid);
  void OpenCreateProfile();
  void OpenAchievementsList(uint8_t user_index);
  void OpenTitleAchievements(uint8_t user_index, uint32_t title_id);
  void OpenConsoleSettings();
  void OpenDisplaySettings();
  void OpenInstalledContent();
  void InstallContentPackage();
  void OpenProfilesMenu();
  void OpenMusicPlayer();
  void ToggleControllerVibration();
  bool IsVibrationEnabled() const;
  void ToggleFullscreen();
  void TakeScreenshot();

  // Brief on-screen message drawn by the active dashboard/guide view.
  void ShowToast(std::string message);
  const std::string& toast() const { return toast_; }
  float toast_age() const;

 private:
  void GuideMonitorThread();
  void OnTitleLaunched();
  void OnTitleExitRequested();
  void OnGuestNotificationShown(bool is_achievement);
  // Shell mode: signs in the shell's profile, creating it on first use.
  void EnsureShellProfile();
  // Writes the signed-in profile's summary for the shell.
  void ExportProfileJson();

  EmulatorWindow* emulator_window_;
  std::unique_ptr<GameLibrary> library_;
  std::unique_ptr<DashboardAudio> audio_;
  std::unique_ptr<TextureCache> textures_;
  std::unique_ptr<DashboardDialog> dashboard_;
  std::unique_ptr<GuideOverlay> guide_;

  // Guest input stays blocked after the guide closes until every button is
  // released, so the press that closed it never reaches the title.
  std::atomic<bool> release_input_on_neutral_ = false;
  bool guide_suspended_title_ = false;

  std::thread guide_monitor_thread_;
  std::atomic<bool> quit_ = false;

  std::string toast_;
  uint64_t toast_time_ms_ = 0;

  struct CachedCard {
    ProfileCard card;
    uint64_t time_ms = 0;
  };
  std::map<uint8_t, CachedCard> card_cache_;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_CONTROLLER_H_
