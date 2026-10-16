/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/guide_overlay.h"

#include <algorithm>
#include <chrono>
#include <ctime>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/platform.h"
#include "xenia/emulator.h"

namespace xe {
namespace app {
namespace dashboard {

using ui::DashboardFont;

namespace {

constexpr float kPanelX = 92.f;
constexpr float kPanelW = 520.f;
constexpr float kPanelTop = 64.f;
constexpr float kPanelBottom = 656.f;
constexpr float kRowH = 46.f;

std::string LocalTimeString() {
  const std::time_t now = std::time(nullptr);
  std::tm local = {};
#if XE_PLATFORM_WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
  return fmt::format("{}:{:02} {}", hour12, local.tm_min,
                     local.tm_hour < 12 ? "AM" : "PM");
}

}  // namespace

GuideOverlay::GuideOverlay(ui::ImGuiDrawer* imgui_drawer,
                           DashboardController* controller, bool in_title)
    : ui::ImGuiDialog(imgui_drawer),
      controller_(controller),
      in_title_(in_title) {
  // Snapshot everything now: guest threads are suspended right after this.
  card_ = controller_->GetProfileCard(0);
  Emulator* emulator = controller_->emulator();
  if (in_title_) {
    title_name_ = emulator->title_name();
    title_id_ = emulator->title_id();
    if (auto* db = emulator->game_info_database(); db && db->IsValid()) {
      const std::vector<uint8_t> icon = db->GetIcon();
      title_icon_ = controller_->textures().Get(
          fmt::format("title:{:08X}", title_id_), icon);
      if (title_name_.empty()) {
        title_name_ = db->GetTitleName();
      }
    }
    if (card_.signed_in) {
      for (const auto& info : controller_->GetPlayedTitles(card_.xuid)) {
        if (info.id == title_id_) {
          title_achievements_unlocked_ = info.unlocked_achievements_count;
          title_achievements_total_ = info.achievements_count;
          title_gamerscore_earned_ = info.title_earned_gamerscore;
          title_gamerscore_total_ = info.gamerscore_amount;
          break;
        }
      }
    }
  }
  BuildTabs();
  input_.SwallowHeldButtons();
}

GuideOverlay::~GuideOverlay() = default;

void GuideOverlay::BeginClose() {
  closing_ = true;
  confirm_quit_ = false;
}

void GuideOverlay::Reopen() {
  closing_ = false;
  close_reported_ = false;
  input_.SwallowHeldButtons();
}

void GuideOverlay::BuildTabs() {
  DashboardController* c = controller_;
  tabs_.clear();

  Tab games{"games", {}};
  if (in_title_) {
    games.entries.push_back(
        {"Achievements",
         title_achievements_total_
             ? fmt::format("{} of {}", title_achievements_unlocked_,
                           title_achievements_total_)
             : "",
         false, [c, id = title_id_]() { c->OpenTitleAchievements(0, id); }});
  }
  games.entries.push_back({"All Achievements",
                           card_.signed_in
                               ? fmt::format("{} games", card_.titles_played)
                               : "",
                           false, [c]() { c->OpenAchievementsList(0); }});
  games.entries.push_back({"Game Marketplace", "", false,
                           [c]() { c->OpenInstalledContent(); }});
  if (in_title_) {
    games.entries.push_back({"Xbox Dashboard", "Quit game", false,
                             [this]() { confirm_quit_ = true; }});
  }
  tabs_.push_back(std::move(games));

  Tab live{"xbox live", {}};
  live.entries.push_back({"Friends", "", true, nullptr});
  live.entries.push_back({"Messages", "", true, nullptr});
  live.entries.push_back({"Party", "", true, nullptr});
  live.entries.push_back({"Chat", "", true, nullptr});
  tabs_.push_back(std::move(live));

  Tab media{"media", {}};
  media.entries.push_back({"Music Player", "", false,
                           [c]() { c->OpenMusicPlayer(); }});
  media.entries.push_back({"Pictures", "", true, nullptr});
  media.entries.push_back({"Video", "", true, nullptr});
  tabs_.push_back(std::move(media));

  Tab settings{"settings", {}};
  settings.entries.push_back(
      {"Profile", card_.signed_in ? card_.gamertag : "Not signed in", false,
       [c, xuid = card_.xuid]() {
         if (xuid) {
           c->OpenGamercardEditor(xuid);
         } else {
           c->OpenProfilesMenu();
         }
       }});
  settings.entries.push_back(
      {"Controller Vibration", c->IsVibrationEnabled() ? "On" : "Off", false,
       [this, c]() {
         c->ToggleControllerVibration();
         BuildTabs();
       }});
  settings.entries.push_back({"System Settings", "", false,
                              [c]() { c->OpenConsoleSettings(); }});
  settings.entries.push_back({"Display", "", false,
                              [c]() { c->OpenDisplaySettings(); }});
  settings.entries.push_back({"Full Screen", "", false,
                              [c]() { c->ToggleFullscreen(); }});
  if (in_title_) {
    settings.entries.push_back({"Take Screenshot", "", false,
                                [c]() { c->TakeScreenshot(); }});
  }
  tabs_.push_back(std::move(settings));

  selection_.resize(tabs_.size(), 0);
  for (size_t i = 0; i < tabs_.size(); i++) {
    selection_[i] = std::min(selection_[i], tabs_[i].entries.size() - 1);
  }
}

void GuideOverlay::HandleInput() {
  if (closing_) {
    return;
  }
  if (confirm_quit_) {
    if (input_.Pressed(Button::kA)) {
      controller_->audio().Play(SoundEvent::kSelect);
      controller_->RestartToDashboard();
    } else if (input_.Pressed(Button::kB)) {
      controller_->audio().Play(SoundEvent::kBack);
      confirm_quit_ = false;
    }
    return;
  }

  if (input_.Pressed(Button::kB)) {
    controller_->CloseGuide();
    return;
  }
  if (in_title_ && input_.Pressed(Button::kY)) {
    controller_->audio().Play(SoundEvent::kSelect);
    confirm_quit_ = true;
    return;
  }

  int tab_move = 0;
  if (input_.Pressed(Button::kRightBumper) || input_.Pressed(Button::kRight)) {
    tab_move = 1;
  } else if (input_.Pressed(Button::kLeftBumper) ||
             input_.Pressed(Button::kLeft)) {
    tab_move = -1;
  }
  if (tab_move) {
    const int next = int(tab_) + tab_move;
    if (next >= 0 && next < int(tabs_.size())) {
      tab_ = size_t(next);
      controller_->audio().Play(SoundEvent::kBladeSlide);
    }
  }

  Tab& tab = tabs_[tab_];
  size_t& selected = selection_[tab_];
  const int v = input_.Vertical();
  if (v && !tab.entries.empty()) {
    const int next =
        std::clamp(int(selected) + v, 0, int(tab.entries.size()) - 1);
    if (next != int(selected)) {
      selected = size_t(next);
      controller_->audio().Play(SoundEvent::kNavigate);
    }
  }
  if (input_.Pressed(Button::kA) && selected < tab.entries.size()) {
    const Entry& entry = tab.entries[selected];
    if (entry.unavailable) {
      controller_->audio().Play(SoundEvent::kError);
      controller_->ShowToast(
          fmt::format("{} needs Xbox LIVE, which isn't available.",
                      entry.label));
    } else if (entry.action) {
      controller_->audio().Play(SoundEvent::kSelect);
      // Copy: the action may rebuild the tabs.
      auto action = entry.action;
      action();
    }
  }
}

void GuideOverlay::OnDraw(ImGuiIO& io) {
  delta_time_ = std::clamp(io.DeltaTime, 0.f, 0.1f);
  time_ += delta_time_;

  const bool has_focus = !closing_ && imgui_drawer()->GetTopDialog() == this;
  input_.Update(controller_->emulator()->input_system(), delta_time_,
                has_focus);
  HandleInput();

  open_anim_ = theme::Approach(open_anim_, closing_ ? 0.f : 1.f, delta_time_,
                               theme::kGuideSlideTime);
  tab_anim_ = theme::Approach(tab_anim_, float(tab_), delta_time_,
                              theme::kBladeSlideTime);
  if (closing_ && open_anim_ <= 0.01f && !close_reported_) {
    close_reported_ = true;
    controller_->OnGuideAnimationFinished();
  }

  const Canvas canvas = Canvas::FromDisplay(io.DisplaySize);
  Painter p(ImGui::GetBackgroundDrawList(), imgui_drawer(), canvas);
  Draw(p);
}

void GuideOverlay::Draw(Painter& p) {
  const float open = theme::EaseOutCubic(open_anim_);
  if (open <= 0.f) {
    return;
  }
  // Dim the frozen game frame behind the guide.
  p.FillBackground(theme::WithAlpha(theme::kGuideDim, open),
                   theme::WithAlpha(theme::kGuideDim, open));

  const float slide = (1.f - open) * -(kPanelX + kPanelW + 40.f);
  const float x0 = kPanelX + slide;
  const float x1 = x0 + kPanelW;
  const ImVec2 panel_min(x0, kPanelTop);
  const ImVec2 panel_max(x1, kPanelBottom);

  p.Shadow(panel_min, panel_max, 18.f, 0.6f * open, 10.f);
  p.Rect(panel_min, panel_max, theme::WithAlpha(theme::kGuidePanel, open),
         10.f);
  p.VerticalGradient(panel_min, ImVec2(x1, kPanelTop + 140.f),
                     theme::WithAlpha(theme::Rgb(0x2F4A22), 0.85f * open),
                     theme::WithAlpha(theme::Rgb(0x151A15), 0.f));
  p.RectOutline(panel_min, panel_max,
                theme::WithAlpha(theme::kGuidePanelEdge, 0.55f * open), 1.5f,
                10.f);

  // Gamercard header.
  const ImVec2 pic_min(x0 + 24.f, kPanelTop + 22.f);
  const ImVec2 pic_max(pic_min.x + 72.f, pic_min.y + 72.f);
  p.Rect(pic_min, pic_max, theme::WithAlpha(theme::Rgb(0x4B5A44), open), 6.f);
  if (card_.gamerpic) {
    p.Image(card_.gamerpic, pic_min, pic_max, true,
            theme::WithAlpha(theme::kWhite, open), 6.f);
  }
  const ImU32 white = theme::WithAlpha(theme::kWhite, open);
  const ImU32 gray = theme::WithAlpha(theme::kTextLightGray, open);
  if (card_.signed_in) {
    p.TextClipped(ImVec2(pic_max.x + 16.f, pic_min.y + 4.f), card_.gamertag,
                  28.f, white, x1 - pic_max.x - 130.f,
                  DashboardFont::kSemibold);
    p.Gamerscore(ImVec2(pic_max.x + 16.f, pic_min.y + 42.f), card_.gamerscore,
                 19.f, gray);
  } else {
    p.Text(ImVec2(pic_max.x + 16.f, pic_min.y + 18.f), "Not signed in", 24.f,
           gray);
  }
  p.Text(ImVec2(x1 - 24.f, pic_min.y + 6.f), LocalTimeString(), 18.f, gray,
         DashboardFont::kRegular, TextAlign::kRight);

  // Tabs.
  const float tab_y = kPanelTop + 120.f;
  float tab_x = x0 + 24.f;
  std::vector<std::pair<float, float>> tab_spans;
  for (size_t i = 0; i < tabs_.size(); i++) {
    const bool active = i == tab_;
    const float w =
        p.Text(ImVec2(tab_x, tab_y), tabs_[i].name, 21.f,
               active ? white : theme::WithAlpha(theme::kTextGray, open),
               active ? DashboardFont::kSemibold : DashboardFont::kRegular);
    tab_spans.push_back({tab_x, w});
    tab_x += w + 26.f;
  }
  // Sliding underline between tabs.
  if (!tab_spans.empty()) {
    const size_t lo = std::min(size_t(std::floor(tab_anim_)),
                               tab_spans.size() - 1);
    const size_t hi = std::min(lo + 1, tab_spans.size() - 1);
    const float f = tab_anim_ - float(lo);
    const float ux = tab_spans[lo].first +
                     (tab_spans[hi].first - tab_spans[lo].first) * f;
    const float uw = tab_spans[lo].second +
                     (tab_spans[hi].second - tab_spans[lo].second) * f;
    p.Rect(ImVec2(ux, tab_y + 32.f), ImVec2(ux + uw, tab_y + 35.f),
           theme::WithAlpha(theme::kGuideHighlight, open));
  }
  p.Rect(ImVec2(x0 + 24.f, tab_y + 35.f), ImVec2(x1 - 24.f, tab_y + 36.f),
         theme::WithAlpha(theme::Rgb(0xFFFFFF, 40), open));

  // Entries.
  const Tab& tab = tabs_[tab_];
  float y = tab_y + 56.f;
  for (size_t i = 0; i < tab.entries.size(); i++) {
    const Entry& entry = tab.entries[i];
    const bool focused = i == selection_[tab_] && !confirm_quit_;
    const ImVec2 row_min(x0 + 16.f, y);
    const ImVec2 row_max(x1 - 16.f, y + kRowH - 6.f);
    if (focused) {
      p.VerticalGradient(row_min, row_max,
                         theme::WithAlpha(theme::kWhite, open),
                         theme::WithAlpha(theme::Rgb(0xDDE3D8), open));
      p.Rect(row_min, ImVec2(row_min.x + 5.f, row_max.y),
             theme::WithAlpha(theme::kGuideHighlight, open));
    }
    const float entry_alpha = entry.unavailable ? 0.45f : 1.f;
    const ImU32 label_color =
        focused ? theme::WithAlpha(theme::kTextDark, open)
                : theme::WithAlpha(theme::kWhite, open * entry_alpha);
    p.Text(ImVec2(row_min.x + 18.f, y + 7.f), entry.label, 21.f, label_color,
           focused ? DashboardFont::kSemibold : DashboardFont::kRegular);
    if (!entry.detail.empty()) {
      p.Text(ImVec2(row_max.x - 14.f, y + 10.f), entry.detail, 17.f,
             focused ? theme::WithAlpha(theme::kTextGray, open) : gray,
             DashboardFont::kRegular, TextAlign::kRight);
    }
    y += kRowH;
  }

  // Now playing strip.
  if (in_title_) {
    const float strip_y = kPanelBottom - 118.f;
    p.Rect(ImVec2(x0 + 16.f, strip_y), ImVec2(x1 - 16.f, strip_y + 64.f),
           theme::WithAlpha(theme::Rgb(0x000000, 90), open), 6.f);
    float text_x = x0 + 30.f;
    if (title_icon_) {
      p.Image(title_icon_, ImVec2(x0 + 26.f, strip_y + 8.f),
              ImVec2(x0 + 74.f, strip_y + 56.f), false, white);
      text_x = x0 + 88.f;
    }
    p.TextClipped(ImVec2(text_x, strip_y + 9.f),
                  title_name_.empty() ? fmt::format("{:08X}", title_id_)
                                      : title_name_,
                  20.f, white, x1 - text_x - 30.f, DashboardFont::kSemibold);
    if (title_gamerscore_total_) {
      p.Text(ImVec2(text_x, strip_y + 36.f),
             fmt::format("{} of {} gamerscore", title_gamerscore_earned_,
                         title_gamerscore_total_),
             15.f, gray);
    } else {
      p.Text(ImVec2(text_x, strip_y + 36.f), "Now playing", 15.f, gray);
    }
  }

  // Quit confirmation.
  if (confirm_quit_) {
    const ImVec2 min(x0 + 16.f, tab_y + 56.f);
    const ImVec2 max(x1 - 16.f, tab_y + 210.f);
    p.Rect(min, max, theme::WithAlpha(theme::Rgb(0x0E120E, 245), open), 8.f);
    p.RectOutline(min, max, theme::WithAlpha(theme::kGuideHighlight, open),
                  2.f, 8.f);
    p.Text(ImVec2(min.x + 20.f, min.y + 18.f), "Return to the Xbox Dashboard?",
           24.f, white, DashboardFont::kSemibold);
    p.Text(ImVec2(min.x + 20.f, min.y + 58.f),
           "Any unsaved progress in the game will be lost.", 18.f, gray,
           DashboardFont::kRegular, TextAlign::kLeft, max.x - min.x - 40.f);
    p.ButtonPrompts(ImVec2(min.x + 20.f, max.y - 44.f),
                    {{'A', "Yes"}, {'B', "No"}}, white, 19.f);
  }

  // Button legend.
  std::vector<std::pair<char, std::string>> prompts = {{'A', "Select"},
                                                       {'B', "Back"}};
  if (in_title_) {
    prompts.push_back({'Y', "Xbox Dashboard"});
  }
  p.ButtonPrompts(ImVec2(x0 + 24.f, kPanelBottom - 42.f), prompts, gray, 17.f);

  // The controller's own toast, drawn above the guide.
  const std::string& message = controller_->toast();
  if (!message.empty() && controller_->toast_age() < 3.f) {
    const ImVec2 size = p.MeasureText(message, 18.f);
    const ImVec2 min(x0 + 16.f, kPanelBottom + 10.f);
    p.Rect(min, ImVec2(min.x + size.x + 32.f, min.y + 38.f),
           theme::WithAlpha(theme::kGuidePanel, open), 6.f);
    p.Text(ImVec2(min.x + 16.f, min.y + 9.f), message, 18.f, white);
  }
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
