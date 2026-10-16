/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/dashboard_controller.h"

#include <cstdlib>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/dashboard/dashboard_ui.h"
#include "xenia/app/dashboard/guide_overlay.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"
#include "xenia/base/threading.h"
#include "xenia/base/utf8.h"
#include "xenia/config.h"
#include "xenia/emulator.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/kernel/xam/ui/game_achievements_ui.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"
#include "xenia/kernel/xam/user_settings.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_drawer.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#else
#include <unistd.h>
#endif

DEFINE_bool(dashboard, true,
            "Show the Xbox 360 dashboard when no title is running. Titles that "
            "exit to the dashboard restart Xenia into it.",
            "Dashboard");
DEFINE_string(dashboard_style, "nxe",
              "Dashboard interface style.\n"
              " nxe: New Xbox Experience (2008), channels of tiles.\n"
              " blades: launch dashboard (2005), sliding blades.",
              "Dashboard");
DEFINE_path(dashboard_library_path, "",
            "Folder scanned recursively for games (.iso, extracted discs, "
            "Games on Demand / Xbox LIVE Arcade packages, .xex, .zar).",
            "Dashboard");
DEFINE_bool(dashboard_sounds, true,
            "Play interface sounds. Custom .wav files can be placed in "
            "<storage root>/dashboard/sounds (navigate.wav, select.wav, "
            "back.wav, blade_slide.wav, channel_change.wav, guide_open.wav, "
            "guide_close.wav, achievement.wav, notification.wav, error.wav).",
            "Dashboard");
DEFINE_bool(guide_suspends_title, true,
            "Suspend the title's guest threads while the Xbox Guide is open.",
            "Dashboard");

DECLARE_string(notification_sound_path);

namespace xe {
namespace app {
namespace dashboard {

using kernel::xam::UserSettingId;

// ---------------------------------------------------------------------------
// TextureCache
// ---------------------------------------------------------------------------

ui::ImmediateTexture* TextureCache::Get(const std::string& key,
                                        std::span<const uint8_t> image_data) {
  if (auto it = textures_.find(key); it != textures_.end()) {
    return it->second.get();
  }
  if (image_data.empty() || failed_.contains(key)) {
    return nullptr;
  }
  auto texture = imgui_drawer_->LoadImGuiIcon(image_data);
  if (!texture) {
    failed_.insert(key);
    return nullptr;
  }
  auto* result = texture.get();
  textures_.emplace(key, std::move(texture));
  return result;
}

void TextureCache::Remove(const std::string& key) {
  textures_.erase(key);
  failed_.erase(key);
}

void TextureCache::Clear() {
  textures_.clear();
  failed_.clear();
}

// ---------------------------------------------------------------------------
// DashboardController
// ---------------------------------------------------------------------------

DashboardController::DashboardController(EmulatorWindow* emulator_window)
    : emulator_window_(emulator_window) {}

DashboardController::~DashboardController() {
  quit_ = true;
  if (guide_monitor_thread_.joinable()) {
    guide_monitor_thread_.join();
  }
  if (emulator()) {
    emulator()->title_exit_handler = nullptr;
  }
  if (imgui_drawer()) {
    imgui_drawer()->SetGuestNotificationShownCallback(nullptr);
  }
  guide_.reset();
  dashboard_.reset();
  textures_.reset();
  library_.reset();
}

Emulator* DashboardController::emulator() const {
  return emulator_window_->emulator();
}

ui::ImGuiDrawer* DashboardController::imgui_drawer() const {
  return emulator_window_->imgui_drawer();
}

void DashboardController::Initialize() {
  Emulator* emu = emulator();
  library_ = std::make_unique<GameLibrary>(emu->cache_root() / "dashboard");
  audio_ = std::make_unique<DashboardAudio>(emu->storage_root() / "dashboard" /
                                            "sounds");
  audio_->SetEnabled(cvars::dashboard_sounds);
  textures_ = std::make_unique<TextureCache>(imgui_drawer());

  imgui_drawer()->SetGuestNotificationShownCallback(
      [this](bool is_achievement) { OnGuestNotificationShown(is_achievement); });

  emu->on_launch.AddListener([this](uint32_t, const std::string_view) {
    emulator_window_->app_context().CallInUIThreadDeferred(
        [this]() { OnTitleLaunched(); });
  });

  if (cvars::dashboard) {
    emu->title_exit_handler = [this]() {
      OnTitleExitRequested();
      return true;
    };
  }

  if (!cvars::dashboard_library_path.empty()) {
    library_->StartScan(cvars::dashboard_library_path);
  }

  if (cvars::dashboard_guide) {
    if (!cvars::guide_button) {
      XELOGW(
          "Dashboard: guide_button is disabled, so the Guide button can't "
          "open the Xbox Guide. Use Ctrl+G instead.");
    } else if (IsUseNexusForGameBarEnabled()) {
      XELOGW(
          "Dashboard: Windows Game Bar is bound to the Guide button and will "
          "intercept it. Disable 'Open Xbox Game Bar using this button on a "
          "controller' in Windows settings, or use Ctrl+G.");
    }
    guide_monitor_thread_ = std::thread([this]() { GuideMonitorThread(); });
  }

  ShowDashboard();
}

DashboardStyle DashboardController::style() const {
  return xe::utf8::lower_ascii(cvars::dashboard_style) == "blades"
             ? DashboardStyle::kBlades
             : DashboardStyle::kNxe;
}

void DashboardController::SetStyle(DashboardStyle style) {
  OVERRIDE_string(dashboard_style,
                  style == DashboardStyle::kBlades ? "blades" : "nxe");
  config::SaveConfig();
}

void DashboardController::SetSoundsEnabled(bool enabled) {
  OVERRIDE_bool(dashboard_sounds, enabled);
  audio_->SetEnabled(enabled);
  config::SaveConfig();
  if (enabled) {
    audio_->Play(SoundEvent::kSelect);
  }
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

ProfileCard DashboardController::GetProfileCard(uint8_t user_index) {
  const uint64_t now = Clock::QueryHostUptimeMillis();
  if (auto it = card_cache_.find(user_index);
      it != card_cache_.end() && now - it->second.time_ms < 1000) {
    return it->second.card;
  }

  ProfileCard card;
  card.user_index = user_index;
  auto* xam = emulator()->kernel_state()->xam_state();
  auto* profile = xam->profile_manager()->GetProfile(user_index);
  if (profile) {
    card.signed_in = true;
    card.xuid = profile->xuid();
    card.gamertag = profile->name();

    const auto gamerpic =
        profile->GetProfileIcon(kernel::xam::XTileType::kGamerTile);
    card.gamerpic = textures_->Get(
        fmt::format("gamerpic:{:016X}:{}", card.xuid, gamerpic.size()),
        gamerpic);
    const auto avatar =
        profile->GetProfileIcon(kernel::xam::XTileType::kAvatarGamerTile);
    card.avatar_image = textures_->Get(
        fmt::format("avatar:{:016X}:{}", card.xuid, avatar.size()), avatar);

    auto* tracker = xam->user_tracker();
    auto setting = [&](UserSettingId id) {
      return tracker->GetSetting(profile, kernel::kDashboardID,
                                 static_cast<uint32_t>(id));
    };
    if (auto motto = setting(UserSettingId::XPROFILE_GAMERCARD_MOTTO)) {
      const auto value = motto->get_host_data();
      if (const auto* text = std::get_if<std::u16string>(&value)) {
        card.motto = xe::to_utf8(*text);
      }
    }
    if (auto zone = setting(UserSettingId::XPROFILE_GAMERCARD_ZONE)) {
      const auto value = zone->get_host_data();
      if (const auto* z = std::get_if<int32_t>(&value)) {
        static constexpr const char* kZones[] = {"", "Recreation", "Pro",
                                                 "Family", "Underground"};
        if (*z >= 0 && *z < int32_t(std::size(kZones))) {
          card.zone = kZones[*z];
        }
      }
    }
    uint32_t summed_gamerscore = 0;
    for (const auto& title : tracker->GetPlayedTitles(card.xuid)) {
      if (title.id == kernel::kDashboardID) {
        continue;
      }
      card.titles_played++;
      card.achievements_unlocked += title.unlocked_achievements_count;
      summed_gamerscore += title.title_earned_gamerscore;
    }
    card.gamerscore = summed_gamerscore;
    if (auto cred = setting(UserSettingId::XPROFILE_GAMERCARD_CRED)) {
      const auto value = cred->get_host_data();
      if (const auto* score = std::get_if<int32_t>(&value);
          score && *score > 0) {
        card.gamerscore = uint32_t(*score);
      }
    }
  }
  card_cache_[user_index] = {card, now};
  return card;
}

std::vector<kernel::xam::TitleInfo> DashboardController::GetPlayedTitles(
    uint64_t xuid) {
  return emulator()->kernel_state()->xam_state()->user_tracker()
      ->GetPlayedTitles(xuid);
}

std::vector<RecentTitle> DashboardController::GetRecentTitles(
    size_t max_count) {
  // Compare normalized paths as strings: no filesystem access per frame.
  auto normalize = [](const std::filesystem::path& path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    return xe::utf8::lower_ascii(
        xe::path_to_utf8((ec ? path : absolute).lexically_normal()));
  };
  std::vector<RecentTitle> result;
  const GameList games = library_->GetGames();
  for (const auto& entry : emulator_window_->recently_launched_titles()) {
    if (result.size() >= max_count) {
      break;
    }
    RecentTitle recent;
    recent.title_name = entry.title_name;
    recent.path = entry.path_to_file;
    const std::string recent_key = normalize(entry.path_to_file);
    for (const auto& game : games) {
      if (normalize(game->launch_path) == recent_key) {
        recent.game = game;
        if (recent.title_name.empty()) {
          recent.title_name = game->DisplayName();
        }
        break;
      }
    }
    result.push_back(std::move(recent));
  }
  return result;
}

ui::ImmediateTexture* DashboardController::GetGameIcon(const GameEntry& game) {
  return textures_->Get("icon:" + xe::path_to_utf8(game.launch_path),
                        game.icon_image);
}

ui::ImmediateTexture* DashboardController::GetGameCover(const GameEntry& game) {
  return textures_->Get("cover:" + xe::path_to_utf8(game.launch_path),
                        game.cover_image);
}

// ---------------------------------------------------------------------------
// Home screen
// ---------------------------------------------------------------------------

void DashboardController::ShowDashboard() {
  if (!cvars::dashboard || emulator()->is_title_open()) {
    return;
  }
  // Recreate: RunTitle's ClearDialogs may have detached the old instance.
  dashboard_.reset();
  dashboard_ = std::make_unique<DashboardDialog>(imgui_drawer(), this);
}

void DashboardController::HideDashboard() { dashboard_.reset(); }

void DashboardController::OnTitleLaunched() {
  HideDashboard();
  if (guide_ && !guide_->in_title()) {
    guide_.reset();
  }
}

// ---------------------------------------------------------------------------
// Guide
// ---------------------------------------------------------------------------

void DashboardController::ToggleGuide() {
  if (guide_ && !guide_->is_closing()) {
    CloseGuide();
  } else {
    OpenGuide();
  }
}

void DashboardController::OpenGuide() {
  Emulator* emu = emulator();
  auto* xam = emu->kernel_state()->xam_state();
  const bool in_title = emu->is_title_open();

  if (guide_) {
    if (!guide_->is_closing()) {
      return;
    }
    // Pressed again while sliding out: bring it back.
    guide_->Reopen();
  } else {
    if (in_title && xam->IsUIActive() && !release_input_on_neutral_) {
      // A title-requested system dialog (keyboard, sign-in...) is showing.
      return;
    }
    guide_ = std::make_unique<GuideOverlay>(imgui_drawer(), this, in_title);
  }
  audio_->Play(SoundEvent::kGuideOpen);

  if (in_title) {
    release_input_on_neutral_ = false;
    emu->kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
    xam->is_xam_dialog_present_.store(true);
    if (cvars::guide_suspends_title && !guide_suspended_title_) {
      guide_suspended_title_ = emu->SuspendGuestThreads();
    }
  }
}

void DashboardController::CloseGuide() {
  if (!guide_ || guide_->is_closing()) {
    return;
  }
  guide_->BeginClose();
  audio_->Play(SoundEvent::kGuideClose);

  Emulator* emu = emulator();
  if (guide_->in_title() && emu->is_title_open()) {
    if (guide_suspended_title_) {
      emu->ResumeGuestThreads();
      guide_suspended_title_ = false;
    }
    emu->kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    if (guide_monitor_thread_.joinable()) {
      // Keep the title's input blocked until the closing press is released.
      release_input_on_neutral_ = true;
    } else {
      emu->kernel_state()->xam_state()->is_xam_dialog_present_.store(false);
    }
  }
}

void DashboardController::OnGuideAnimationFinished() {
  // Called from the guide's own OnDraw: destroy it on the next loop
  // iteration instead.
  emulator_window_->app_context().CallInUIThreadDeferred([this]() {
    if (guide_ && guide_->is_closing()) {
      guide_.reset();
    }
  });
}

void DashboardController::GuideMonitorThread() {
  xe::threading::set_name("Dashboard Guide Monitor");
  bool guide_was_down = false;
  bool combo_used = false;
  while (!quit_) {
    xe::threading::Sleep(std::chrono::milliseconds(16));
    hid::InputSystem* input = emulator()->input_system();
    if (!input) {
      continue;
    }
    bool guide_down = false;
    bool other_down = false;
    {
      auto lock = input->try_lock();
      if (!lock.owns_lock()) {
        continue;
      }
      for (uint32_t user = 0; user < XUserMaxUserCount; user++) {
        hid::X_INPUT_STATE state = {};
        if (input->GetState(user, hid::X_INPUT_FLAG_GAMEPAD, &state) !=
            X_ERROR_SUCCESS) {
          continue;
        }
        const uint16_t buttons = state.gamepad.buttons;
        guide_down |= (buttons & hid::X_INPUT_GAMEPAD_GUIDE) != 0;
        other_down |= (buttons & ~uint16_t(hid::X_INPUT_GAMEPAD_GUIDE)) != 0;
      }
    }

    if (guide_down) {
      if (!guide_was_down) {
        combo_used = false;
      }
      // Guide + button combinations are Xenia's controller hotkeys.
      combo_used |= other_down;
    } else if (guide_was_down && !combo_used) {
      emulator_window_->app_context().CallInUIThreadDeferred(
          [this]() { ToggleGuide(); });
    }
    guide_was_down = guide_down;

    if (release_input_on_neutral_ && !guide_down && !other_down) {
      release_input_on_neutral_ = false;
      if (auto* kernel_state = emulator()->kernel_state()) {
        kernel_state->xam_state()->is_xam_dialog_present_.store(false);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void DashboardController::LaunchGame(const std::filesystem::path& path) {
  if (emulator()->is_title_open()) {
    ShowToast("Return to the Xbox Dashboard before starting another game.");
    return;
  }
  emulator_window_->app_context().CallInUIThreadDeferred([this, path]() {
    guide_.reset();
    HideDashboard();
    const X_STATUS result = emulator_window_->RunTitle(path);
    if (XFAILED(result)) {
      audio_->Play(SoundEvent::kError);
      ShowDashboard();
    }
  });
}

void DashboardController::ChooseLibraryFolder() {
  // The native folder picker is modal; never run it inside a dialog's draw.
  emulator_window_->app_context().CallInUIThreadDeferred([this]() {
    auto file_picker = xe::ui::FilePicker::Create();
    file_picker->set_mode(ui::FilePicker::Mode::kOpen);
    file_picker->set_type(ui::FilePicker::Type::kDirectory);
    file_picker->set_multi_selection(false);
    file_picker->set_title("Select Game Library Folder");
    if (!file_picker->Show(emulator_window_->window())) {
      return;
    }
    const auto selected = file_picker->selected_files();
    if (selected.empty()) {
      return;
    }
    OVERRIDE_path(dashboard_library_path, selected.front());
    config::SaveConfig();
    library_->StartScan(selected.front());
    ShowToast("Searching for games...");
  });
}

void DashboardController::RescanLibrary() {
  if (cvars::dashboard_library_path.empty()) {
    ChooseLibraryFolder();
    return;
  }
  library_->StartScan(cvars::dashboard_library_path);
  ShowToast("Searching for games...");
}

void DashboardController::OnTitleExitRequested() {
  // Guest thread: the title called XamLoaderTerminateTitle or relaunched
  // itself. The loader data (if any) is already saved, so a restart either
  // boots the requested title or lands on the dashboard.
  emulator_window_->app_context().CallInUIThreadDeferred(
      [this]() { RestartToDashboard(); });
}

void DashboardController::RestartToDashboard() {
  XELOGI("Dashboard: restarting Xenia to return to the dashboard");
  config::SaveConfig();

  const std::filesystem::path exe = xe::filesystem::GetExecutablePath();
  bool started = false;
#if XE_PLATFORM_WIN32
  std::wstring command_line = L"\"" + exe.wstring() + L"\"";
  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  if (CreateProcessW(exe.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                     0, nullptr, nullptr, &startup_info, &process_info)) {
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    started = true;
  }
#else
  const std::string exe_utf8 = xe::path_to_utf8(exe);
  const pid_t pid = fork();
  if (pid == 0) {
    execl(exe_utf8.c_str(), exe_utf8.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  started = pid > 0;
#endif
  if (!started) {
    XELOGE("Dashboard: unable to start {}", xe::path_to_utf8(exe));
    audio_->Play(SoundEvent::kError);
    ShowToast("Couldn't restart Xenia. Close it and start it again.");
    return;
  }
  xe::FlushLog();
  std::quick_exit(0);
}

void DashboardController::SignIn(uint64_t xuid, uint8_t user_index) {
  auto* profile_manager =
      emulator()->kernel_state()->xam_state()->profile_manager();
  profile_manager->Login(xuid, user_index);
  card_cache_.clear();
}

void DashboardController::SignOut(uint8_t user_index) {
  auto* profile_manager =
      emulator()->kernel_state()->xam_state()->profile_manager();
  profile_manager->Logout(user_index);
  card_cache_.clear();
}

void DashboardController::OpenGamercardEditor(uint64_t xuid) {
  card_cache_.clear();
  new kernel::xam::ui::GamercardUI(emulator_window_->window(), imgui_drawer(),
                                   emulator()->kernel_state(), xuid);
}

void DashboardController::OpenCreateProfile() {
  card_cache_.clear();
  new kernel::xam::ui::CreateProfileUI(imgui_drawer(), emulator());
}

void DashboardController::OpenAchievementsList(uint8_t user_index) {
  auto* profile = emulator()->kernel_state()->xam_state()->GetUserProfile(
      uint32_t(user_index));
  if (!profile) {
    ShowToast("Sign in to a profile to view achievements.");
    return;
  }
  new kernel::xam::ui::TitleListUI(imgui_drawer(), ImVec2(60.f, 60.f),
                                   profile);
}

void DashboardController::OpenTitleAchievements(uint8_t user_index,
                                                uint32_t title_id) {
  auto* profile = emulator()->kernel_state()->xam_state()->GetUserProfile(
      uint32_t(user_index));
  if (!profile) {
    ShowToast("Sign in to a profile to view achievements.");
    return;
  }
  for (const auto& info : GetPlayedTitles(profile->xuid())) {
    if (info.id == title_id) {
      new kernel::xam::ui::GameAchievementsUI(imgui_drawer(),
                                              ImVec2(60.f, 60.f), &info,
                                              profile);
      return;
    }
  }
  ShowToast("No achievements recorded yet. Play the game to start.");
}

void DashboardController::OpenConsoleSettings() {
  emulator_window_->ToggleConsoleSettingsDialog();
}

void DashboardController::OpenDisplaySettings() {
  emulator_window_->ToggleDisplayConfigDialog();
}

void DashboardController::OpenInstalledContent() {
  emulator_window_->ToggleContentListDialog();
}

void DashboardController::InstallContentPackage() {
  emulator_window_->app_context().CallInUIThreadDeferred(
      [this]() { emulator_window_->InstallContent(); });
}

void DashboardController::OpenProfilesMenu() {
  emulator_window_->ToggleProfilesConfigDialog();
}

void DashboardController::OpenMusicPlayer() {
  emulator_window_->ToggleXMPConfigDialog();
}

void DashboardController::ToggleControllerVibration() {
  emulator_window_->ToggleControllerVibration();
}

bool DashboardController::IsVibrationEnabled() const {
  auto* input = emulator_window_->emulator()->input_system();
  return input && input->GetVibrationCvar();
}

void DashboardController::ToggleFullscreen() {
  emulator_window_->app_context().CallInUIThreadDeferred(
      [this]() { emulator_window_->ToggleFullscreen(); });
}

void DashboardController::TakeScreenshot() {
  emulator_window_->TakeScreenshot();
}

void DashboardController::ShowToast(std::string message) {
  toast_ = std::move(message);
  toast_time_ms_ = Clock::QueryHostUptimeMillis();
}

float DashboardController::toast_age() const {
  return float(Clock::QueryHostUptimeMillis() - toast_time_ms_) / 1000.f;
}

void DashboardController::OnGuestNotificationShown(bool is_achievement) {
  card_cache_.clear();
  if (!cvars::notification_sound_path.empty()) {
    // The user's configured notification sound already plays.
    return;
  }
  audio_->Play(is_achievement ? SoundEvent::kAchievement
                              : SoundEvent::kNotification);
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
