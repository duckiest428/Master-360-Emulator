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
#include <cmath>
#include <ctime>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/platform.h"
#include "xenia/emulator.h"

namespace xe {
namespace app {
namespace dashboard {

using ui::DashboardFont;

namespace {

// Geometry is measured from console captures and expressed on the 1280x720
// canvas. Metro: the 2011+ guide. Blades: the 2008 NXE guide.
struct GuideMetrics {
  float content_left;
  float content_right;
  float top;
  float bottom;
  float row_height;
  float active_tab_width;
  float tab_width;
  float text_size;
  float tab_text_size;
  float footer_y;
};

constexpr GuideMetrics kMetro = {428.f, 913.f, 202.f, 518.f, 42.f,
                                 63.f,  45.f,  24.f,  24.f,  527.f};
constexpr GuideMetrics kBlades = {292.f, 984.f, 202.f, 598.f, 44.f,
                                  58.f,  52.f,  21.f,  20.f,  612.f};

// Metro palette.
constexpr ImU32 kMetroDim = theme::Rgb(0x000000, 205);
constexpr ImU32 kMetroListTop = theme::Rgb(0xDCDEE0);
constexpr ImU32 kMetroListBottom = theme::Rgb(0xC9CCCF);
constexpr ImU32 kMetroSeparator = theme::Rgb(0xB2B5B8);
constexpr ImU32 kMetroText = theme::Rgb(0x3A3A3A);
constexpr ImU32 kMetroHighlightTop = theme::Rgb(0x25A125);
constexpr ImU32 kMetroHighlightBottom = theme::Rgb(0x147A14);
constexpr ImU32 kMetroTabA = theme::Rgb(0x56636F);
constexpr ImU32 kMetroTabB = theme::Rgb(0x66737E);
constexpr ImU32 kMetroActiveTabTop = theme::Rgb(0xD4D6D8);
constexpr ImU32 kMetroActiveTabBottom = theme::Rgb(0xB4B8BC);
constexpr ImU32 kMetroTabText = theme::Rgb(0xDDE3E8);
constexpr ImU32 kMetroActiveTabText = theme::Rgb(0x4E5052);
constexpr ImU32 kMetroHeaderText = theme::Rgb(0xE8E8E8);

// Blades palette.
constexpr ImU32 kBladesDim = theme::Rgb(0x000000, 115);
constexpr ImU32 kBladesListTop = theme::Rgb(0x3A3A3A, 210);
constexpr ImU32 kBladesListBottom = theme::Rgb(0x1E1E1E, 215);
constexpr ImU32 kBladesText = theme::Rgb(0xF2F2F2);
constexpr ImU32 kBladesHighlightTop = theme::Rgb(0xA9DA4C);
constexpr ImU32 kBladesHighlightBottom = theme::Rgb(0x5BA61C);
constexpr ImU32 kBladesTabTop = theme::Rgb(0xE2E2E2);
constexpr ImU32 kBladesTabBottom = theme::Rgb(0xA4A4A4);
constexpr ImU32 kBladesActiveTabTop = theme::Rgb(0xFAFAFA);
constexpr ImU32 kBladesActiveTabBottom = theme::Rgb(0xCDCDCD);
constexpr ImU32 kBladesTabText = theme::Rgb(0x505050);

std::string LocalTimeString() {
  const std::time_t now = std::time(nullptr);
  std::tm local = {};
#if XE_PLATFORM_WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
  return fmt::format("{}:{:02}  {}", hour12, local.tm_min,
                     local.tm_hour < 12 ? "AM" : "PM");
}

}  // namespace

GuideOverlay::GuideOverlay(ui::ImGuiDrawer* imgui_drawer,
                           DashboardController* controller, bool in_title)
    : ui::ImGuiDialog(imgui_drawer),
      controller_(controller),
      in_title_(in_title),
      metro_(controller->guide_metro()) {
  // Snapshot everything now: guest threads are suspended right after this.
  card_ = controller_->GetProfileCard(0);
  Emulator* emulator = controller_->emulator();
  if (in_title_) {
    title_name_ = emulator->title_name();
    title_id_ = emulator->title_id();
    if (auto* db = emulator->game_info_database();
        db && db->IsValid() && title_name_.empty()) {
      title_name_ = db->GetTitleName();
    }
    if (card_.signed_in) {
      for (const auto& info : controller_->GetPlayedTitles(card_.xuid)) {
        if (info.id == title_id_) {
          title_achievements_unlocked_ = info.unlocked_achievements_count;
          title_achievements_total_ = info.achievements_count;
          break;
        }
      }
    }
  }
  BuildTabs();
  tab_ = home_tab_;
  tab_anim_ = float(tab_);
  input_.SwallowHeldButtons();
}

GuideOverlay::~GuideOverlay() = default;

void GuideOverlay::BeginClose() {
  closing_ = true;
  confirm_ = Confirm::kNone;
}

void GuideOverlay::Reopen() {
  closing_ = false;
  close_reported_ = false;
  input_.SwallowHeldButtons();
}

void GuideOverlay::BuildTabs() {
  DashboardController* c = controller_;
  tabs_.clear();
  const std::string profile_name =
      card_.signed_in ? card_.gamertag : std::string("Sign In");
  auto unavailable = [](std::string label, std::string count,
                        GuideIcon icon) {
    return Row{std::move(label), std::move(count), icon, true, nullptr};
  };

  // Profile / home tab: the one the guide opens on.
  Tab home{profile_name, {}};
  home.rows.push_back({metro_ ? "Xbox Home" : "Xbox Dashboard", "",
                       GuideIcon::kNone, false,
                       [this]() { confirm_ = Confirm::kQuit; }});
  if (!metro_) {
    home.rows.push_back(unavailable("Quick Launch", "", GuideIcon::kNone));
  }
  home.rows.push_back(unavailable("Friends", "0", GuideIcon::kFriend));
  home.rows.push_back(unavailable("Party", "0", GuideIcon::kParty));
  home.rows.push_back(unavailable("Messages", "0", GuideIcon::kMessage));
  if (metro_) {
    home.rows.push_back(
        unavailable("Beacons & Activity", "", GuideIcon::kBeacon));
    home.rows.push_back(unavailable("Chat", "", GuideIcon::kChat));
    home.rows.push_back(unavailable("Open Tray", "", GuideIcon::kDisc));
  } else {
    home.rows.push_back(unavailable("Chat & IM", "", GuideIcon::kChat));
    if (in_title_) {
      home.rows.push_back({fmt::format("Play {}", title_name_.empty()
                                                      ? std::string("Game")
                                                      : title_name_),
                           "", GuideIcon::kDisc, false,
                           [c]() { c->CloseGuide(); }});
    }
  }

  // Games (& Apps).
  Tab games{metro_ ? "Games & Apps" : "Games", {}};
  if (in_title_) {
    games.rows.push_back(
        {"Achievements",
         title_achievements_total_
             ? fmt::format("{} of {}", title_achievements_unlocked_,
                           title_achievements_total_)
             : "",
         GuideIcon::kTrophy, false,
         [c, id = title_id_]() { c->OpenTitleAchievements(0, id); }});
  }
  games.rows.push_back({in_title_ ? "All Achievements" : "Achievements",
                        card_.signed_in
                            ? fmt::format("{}", card_.achievements_unlocked)
                            : "",
                        GuideIcon::kTrophy, false,
                        [c]() { c->OpenAchievementsList(0); }});
  games.rows.push_back(unavailable("Awards", "", GuideIcon::kNone));
  games.rows.push_back(unavailable("Recently Played", "", GuideIcon::kNone));
  games.rows.push_back({"Game Marketplace", "", GuideIcon::kNone, false,
                        [c]() { c->OpenInstalledContent(); }});
  if (metro_) {
    games.rows.push_back(unavailable("My Apps", "", GuideIcon::kNone));
  }

  // Marketplace (blades only).
  Tab market{"Marketplace", {}};
  market.rows.push_back({"Game Marketplace", "", GuideIcon::kNone, false,
                         [c]() { c->OpenInstalledContent(); }});
  market.rows.push_back(unavailable("Redeem Code", "", GuideIcon::kNone));
  market.rows.push_back(
      unavailable("Active Downloads", "", GuideIcon::kNone));

  // Media.
  Tab media{"Media", {}};
  media.rows.push_back({"Music Player", "", GuideIcon::kMusic, false,
                        [c]() { c->OpenMusicPlayer(); }});
  media.rows.push_back(unavailable("Select Music", "", GuideIcon::kMusic));
  media.rows.push_back(unavailable("Pictures", "", GuideIcon::kNone));
  media.rows.push_back(unavailable("Videos", "", GuideIcon::kNone));

  // Settings.
  Tab settings{"Settings", {}};
  settings.rows.push_back(
      {"Profile", "", GuideIcon::kNone, false, [c, xuid = card_.xuid]() {
         if (xuid) {
           c->OpenGamercardEditor(xuid);
         } else {
           c->OpenProfilesMenu();
         }
       }});
  settings.rows.push_back(
      {"Controller Vibration", c->IsVibrationEnabled() ? "On" : "Off",
       GuideIcon::kNone, false, [this, c]() {
         c->ToggleControllerVibration();
         const size_t keep = tab_;
         BuildTabs();
         tab_ = keep;
       }});
  settings.rows.push_back({"System Settings", "", GuideIcon::kGear, false,
                           [c]() { c->OpenConsoleSettings(); }});
  settings.rows.push_back({"Display", "", GuideIcon::kNone, false,
                           [c]() { c->OpenDisplaySettings(); }});
  settings.rows.push_back({"Full Screen", "", GuideIcon::kNone, false,
                           [c]() { c->ToggleFullscreen(); }});
  if (in_title_) {
    settings.rows.push_back({"Take Screenshot", "", GuideIcon::kNone, false,
                             [c]() { c->TakeScreenshot(); }});
  }
  settings.rows.push_back(
      {"Guide Style", metro_ ? "Metro" : "Blades", GuideIcon::kNone, false,
       [c]() {
         c->ToggleGuideStyle();
         c->ShowToast("The new guide style applies the next time it opens.");
       }});

  if (metro_) {
    tabs_.push_back(std::move(games));
    home_tab_ = tabs_.size();
    tabs_.push_back(std::move(home));
  } else {
    tabs_.push_back(std::move(market));
    tabs_.push_back(std::move(games));
    home_tab_ = tabs_.size();
    tabs_.push_back(std::move(home));
  }
  tabs_.push_back(std::move(media));
  tabs_.push_back(std::move(settings));

  selection_.resize(tabs_.size(), 0);
  for (size_t i = 0; i < tabs_.size(); i++) {
    selection_[i] = std::min(selection_[i], tabs_[i].rows.size() - 1);
  }
}

void GuideOverlay::HandleInput() {
  if (closing_) {
    return;
  }
  if (confirm_ != Confirm::kNone) {
    if (input_.Pressed(Button::kA)) {
      controller_->audio().Play(SoundEvent::kSelect);
      const Confirm confirm = confirm_;
      confirm_ = Confirm::kNone;
      if (confirm == Confirm::kQuit) {
        if (in_title_) {
          controller_->RestartToDashboard();
        } else {
          controller_->CloseGuide();
        }
      } else {
        controller_->SignOut(0);
        card_ = controller_->GetProfileCard(0);
        BuildTabs();
      }
    } else if (input_.Pressed(Button::kB)) {
      controller_->audio().Play(SoundEvent::kBack);
      confirm_ = Confirm::kNone;
    }
    return;
  }

  if (input_.Pressed(Button::kB)) {
    controller_->CloseGuide();
    return;
  }
  if (input_.Pressed(Button::kY)) {
    controller_->audio().Play(SoundEvent::kSelect);
    if (in_title_) {
      confirm_ = Confirm::kQuit;
    } else {
      controller_->CloseGuide();
    }
    return;
  }
  if (input_.Pressed(Button::kX) && card_.signed_in) {
    controller_->audio().Play(SoundEvent::kSelect);
    confirm_ = Confirm::kSignOut;
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
      row_anim_ = float(selection_[tab_]);
      controller_->audio().Play(SoundEvent::kBladeSlide);
    }
  }

  Tab& tab = tabs_[tab_];
  size_t& selected = selection_[tab_];
  const int v = input_.Vertical();
  if (v && !tab.rows.empty()) {
    const int next = std::clamp(int(selected) + v, 0, int(tab.rows.size()) - 1);
    if (next != int(selected)) {
      selected = size_t(next);
      controller_->audio().Play(SoundEvent::kNavigate);
    }
  }
  if (input_.Pressed(Button::kA) && selected < tab.rows.size()) {
    const Row& row = tab.rows[selected];
    if (row.unavailable) {
      controller_->audio().Play(SoundEvent::kError);
      controller_->ShowToast(
          fmt::format("{} needs Xbox LIVE, which isn't available.", row.label));
    } else if (row.action) {
      controller_->audio().Play(SoundEvent::kSelect);
      // Copy: the action may rebuild the tabs.
      auto action = row.action;
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
  row_anim_ = theme::Approach(row_anim_, float(selection_[tab_]), delta_time_,
                              0.12f);
  if (closing_ && open_anim_ <= 0.01f && !close_reported_) {
    close_reported_ = true;
    controller_->OnGuideAnimationFinished();
  }

  const Canvas canvas = Canvas::FromDisplay(io.DisplaySize);
  Painter p(ImGui::GetBackgroundDrawList(), imgui_drawer(), canvas);
  Draw(p);
}

std::pair<float, float> GuideOverlay::TabSpan(size_t index,
                                              size_t current) const {
  const GuideMetrics& m = metro_ ? kMetro : kBlades;
  if (index == current) {
    return {m.content_left - m.active_tab_width, m.content_left};
  }
  if (index < current) {
    const float right = m.content_left - m.active_tab_width -
                        float(current - index - 1) * m.tab_width;
    return {right - m.tab_width, right};
  }
  const float left = m.content_right + float(index - current - 1) * m.tab_width;
  return {left, left + m.tab_width};
}

void GuideOverlay::Draw(Painter& p) {
  const float open = theme::EaseOutCubic(open_anim_);
  if (open <= 0.f) {
    return;
  }
  const ImU32 dim = metro_ ? kMetroDim : kBladesDim;
  p.FillBackground(theme::WithAlpha(dim, open), theme::WithAlpha(dim, open));

  // Opening unfolds the tab blades outwards from the list.
  const float spread = 0.55f + 0.45f * open;
  DrawHeader(p, open);
  DrawTabs(p, open, spread);
  DrawRows(p, open);
  DrawFooter(p, open);
  if (confirm_ != Confirm::kNone) {
    DrawConfirm(p, open);
  }

  const std::string& message = controller_->toast();
  if (!message.empty() && controller_->toast_age() < 3.f) {
    const GuideMetrics& m = metro_ ? kMetro : kBlades;
    const ImVec2 size = p.MeasureText(message, 18.f);
    const float x = (m.content_left + m.content_right) * 0.5f;
    const ImVec2 min(x - size.x * 0.5f - 16.f, m.footer_y + 44.f);
    const ImVec2 max(x + size.x * 0.5f + 16.f, m.footer_y + 80.f);
    p.Rect(min, max, theme::WithAlpha(theme::Rgb(0x1C1C1C, 230), open), 4.f);
    p.Text(ImVec2(x, min.y + 8.f), message, 18.f,
           theme::WithAlpha(theme::kWhite, open), DashboardFont::kRegular,
           TextAlign::kCenter);
  }
}

void GuideOverlay::DrawHeader(Painter& p, float alpha) {
  const GuideMetrics& m = metro_ ? kMetro : kBlades;
  const ImU32 text = theme::WithAlpha(kMetroHeaderText, alpha);
  const float pic = metro_ ? 47.f : 42.f;
  const float pic_y = m.top - pic - (metro_ ? 7.f : 12.f);
  const float pic_x = (m.content_left + m.content_right) * 0.5f - pic * 0.5f -
                      (metro_ ? 33.f : 0.f);

  if (metro_) {
    p.Text(ImVec2(m.content_left - m.active_tab_width - m.tab_width + 45.f,
                  m.top - 48.f),
           "Xbox Guide", 25.f, text);
  } else {
    // Controller quadrant ring at the left of the header.
    const ImVec2 ring(m.content_left + 16.f, m.top - 26.f);
    p.draw_list()->AddCircle(p.canvas().P(ring), p.canvas().S(9.f),
                             theme::WithAlpha(theme::kTextLightGray, alpha),
                             24, p.canvas().S(2.5f));
    p.draw_list()->PathArcTo(p.canvas().P(ring), p.canvas().S(9.f),
                             -3.14159f, -1.5708f, 12);
    p.draw_list()->PathStroke(theme::WithAlpha(theme::kXboxGreenLight, alpha),
                              0, p.canvas().S(2.5f));
  }

  // Gamer picture.
  const ImVec2 pic_min(pic_x, pic_y);
  const ImVec2 pic_max(pic_x + pic, pic_y + pic);
  p.Rect(ImVec2(pic_min.x - 2.f, pic_min.y - 2.f),
         ImVec2(pic_max.x + 2.f, pic_max.y + 2.f),
         theme::WithAlpha(theme::Rgb(0xBFC3C7), alpha), 2.f);
  p.Rect(pic_min, pic_max, theme::WithAlpha(theme::Rgb(0x5A6570), alpha));
  if (card_.gamerpic) {
    p.Image(card_.gamerpic, pic_min, pic_max, true,
            theme::WithAlpha(theme::kWhite, alpha));
  } else {
    p.Circle(ImVec2(pic_min.x + pic * 0.5f, pic_min.y + pic * 0.38f),
             pic * 0.2f, theme::WithAlpha(theme::Rgb(0xC9D0D6), alpha));
    p.Rect(ImVec2(pic_min.x + pic * 0.24f, pic_min.y + pic * 0.62f),
           ImVec2(pic_max.x - pic * 0.24f, pic_max.y),
           theme::WithAlpha(theme::Rgb(0xC9D0D6), alpha), pic * 0.15f);
  }

  // Controller + battery and the ring of light, then the clock.
  const float right = m.content_right;
  if (metro_) {
    const float iy = m.top - 54.f;
    const ImU32 icon = theme::WithAlpha(theme::Rgb(0xD8DDE2), alpha);
    p.Rect(ImVec2(right - 92.f, iy + 4.f), ImVec2(right - 72.f, iy + 16.f),
           icon, 5.f);
    p.RectOutline(ImVec2(right - 68.f, iy + 4.f),
                  ImVec2(right - 42.f, iy + 16.f), icon, 1.5f, 1.f);
    for (int i = 0; i < 4; i++) {
      p.Rect(ImVec2(right - 66.f + i * 6.f, iy + 6.f),
             ImVec2(right - 62.f + i * 6.f, iy + 14.f), icon);
    }
    const ImVec2 ring(right - 18.f, iy + 10.f);
    p.draw_list()->PathArcTo(p.canvas().P(ring), p.canvas().S(10.f), 0.6f,
                             5.3f, 24);
    p.draw_list()->PathStroke(icon, 0, p.canvas().S(2.f));
    p.draw_list()->PathArcTo(p.canvas().P(ring), p.canvas().S(10.f), -0.9f,
                             0.4f, 12);
    p.draw_list()->PathStroke(theme::WithAlpha(theme::kXboxGreenLight, alpha),
                              0, p.canvas().S(2.5f));
    p.Text(ImVec2(right, m.top - 34.f), LocalTimeString(), 23.f, text,
           DashboardFont::kRegular, TextAlign::kRight);
  } else {
    p.Text(ImVec2(right - 8.f, m.top - 38.f), LocalTimeString(), 19.f, text,
           DashboardFont::kRegular, TextAlign::kRight);
  }
}

void GuideOverlay::DrawTabs(Painter& p, float alpha, float spread) {
  const GuideMetrics& m = metro_ ? kMetro : kBlades;
  const size_t lo = std::min(size_t(std::floor(tab_anim_)), tabs_.size() - 1);
  const size_t hi = std::min(lo + 1, tabs_.size() - 1);
  const float f = std::clamp(tab_anim_ - float(lo), 0.f, 1.f);

  for (size_t i = 0; i < tabs_.size(); i++) {
    // Interpolate the tab's slot between the two neighbouring layouts.
    const auto a = TabSpan(i, lo);
    const auto b = TabSpan(i, hi);
    float x0 = a.first + (b.first - a.first) * f;
    float x1 = a.second + (b.second - a.second) * f;
    // Unfold from the list edge while opening.
    const float edge =
        x1 <= m.content_left + 0.5f ? m.content_left : m.content_right;
    x0 = edge + (x0 - edge) * spread;
    x1 = edge + (x1 - edge) * spread;

    const float active = std::clamp(1.f - std::abs(float(i) - tab_anim_), 0.f,
                                    1.f);
    const bool left_side = x1 <= m.content_left + 0.5f;
    const float distance =
        left_side ? (m.content_left - x1) / m.tab_width
                  : (x0 - m.content_right) / m.tab_width;

    if (metro_) {
      const ImU32 base = (i % 2) ? kMetroTabB : kMetroTabA;
      const ImU32 top = theme::Lerp(base, kMetroActiveTabTop, active);
      const ImU32 bottom = theme::Lerp(base, kMetroActiveTabBottom, active);
      p.VerticalGradient(ImVec2(x0, m.top), ImVec2(x1, m.bottom),
                         theme::WithAlpha(top, alpha),
                         theme::WithAlpha(bottom, alpha));
      p.Rect(ImVec2(x1 - 1.f, m.top), ImVec2(x1, m.bottom),
             theme::WithAlpha(theme::Rgb(0x000000, 50), alpha));
      const ImU32 label =
          theme::Lerp(kMetroTabText, kMetroActiveTabText, active);
      const float size = m.tab_text_size;
      p.TextVertical(ImVec2((x0 + x1) * 0.5f + size * 0.62f, m.top + 16.f),
                     tabs_[i].name, size, theme::WithAlpha(label, alpha),
                     DashboardFont::kRegular, true);
    } else {
      // Blades recede in perspective away from the list.
      const float inset_near = std::max(0.f, distance) * 9.f;
      const float inset_far = inset_near + 9.f;
      ImVec2 quad[4];
      if (left_side) {
        quad[0] = ImVec2(x0, m.top + inset_far);
        quad[1] = ImVec2(x1, m.top + inset_near);
        quad[2] = ImVec2(x1, m.bottom - inset_near);
        quad[3] = ImVec2(x0, m.bottom - inset_far);
      } else {
        quad[0] = ImVec2(x0, m.top + inset_near);
        quad[1] = ImVec2(x1, m.top + inset_far);
        quad[2] = ImVec2(x1, m.bottom - inset_far);
        quad[3] = ImVec2(x0, m.bottom - inset_near);
      }
      const ImU32 top = theme::Lerp(kBladesTabTop, kBladesActiveTabTop, active);
      const ImU32 bottom =
          theme::Lerp(kBladesTabBottom, kBladesActiveTabBottom, active);
      p.QuadGradient(quad, theme::WithAlpha(top, alpha),
                     theme::WithAlpha(bottom, alpha));
      // Edge shading between stacked blades.
      const float shade_x = left_side ? x1 : x0;
      p.Rect(ImVec2(shade_x - 1.f, quad[left_side ? 1 : 0].y),
             ImVec2(shade_x + 1.f, quad[left_side ? 2 : 3].y),
             theme::WithAlpha(theme::Rgb(0x000000, 70), alpha));
      const float size = m.tab_text_size;
      const ImU32 label = theme::WithAlpha(kBladesTabText, alpha);
      if (left_side) {
        p.TextVertical(ImVec2((x0 + x1) * 0.5f - size * 0.62f,
                              m.bottom - inset_near - 14.f),
                       tabs_[i].name, size, label, DashboardFont::kRegular,
                       false);
      } else {
        p.TextVertical(ImVec2((x0 + x1) * 0.5f + size * 0.62f,
                              m.top + inset_near + 14.f),
                       tabs_[i].name, size, label, DashboardFont::kRegular,
                       true);
      }
    }
  }
}

void GuideOverlay::DrawRows(Painter& p, float alpha) {
  const GuideMetrics& m = metro_ ? kMetro : kBlades;
  const ImVec2 list_min(m.content_left, m.top);
  const ImVec2 list_max(m.content_right, m.bottom);
  if (metro_) {
    p.VerticalGradient(list_min, list_max,
                       theme::WithAlpha(kMetroListTop, alpha),
                       theme::WithAlpha(kMetroListBottom, alpha));
  } else {
    p.Shadow(list_min, list_max, 10.f, 0.5f * alpha, 4.f);
    p.VerticalGradient(list_min, list_max,
                       theme::WithAlpha(kBladesListTop, alpha),
                       theme::WithAlpha(kBladesListBottom, alpha));
  }

  // Content slides horizontally while switching tabs.
  const float slide = (float(tab_) - tab_anim_) * -90.f;
  const float content_alpha =
      alpha * std::clamp(1.f - std::abs(float(tab_) - tab_anim_) * 1.6f,
                         0.f, 1.f);
  ImDrawList* dl = p.draw_list();
  dl->PushClipRect(p.canvas().P(list_min), p.canvas().P(list_max), true);

  const Tab& tab = tabs_[tab_];
  const float pad = metro_ ? 15.f : 18.f;
  const float highlight_y = m.top + row_anim_ * m.row_height;
  if (confirm_ == Confirm::kNone) {
    if (metro_) {
      p.VerticalGradient(
          ImVec2(m.content_left, highlight_y),
          ImVec2(m.content_right, highlight_y + m.row_height),
          theme::WithAlpha(kMetroHighlightTop, content_alpha),
          theme::WithAlpha(kMetroHighlightBottom, content_alpha));
    } else {
      const ImVec2 min(m.content_left + 8.f, highlight_y + 4.f);
      const ImVec2 max(m.content_right - 8.f,
                       highlight_y + m.row_height - 4.f);
      p.Rect(min, max, theme::WithAlpha(theme::Rgb(0x2E5E0E), content_alpha),
             (max.y - min.y) * 0.5f);
      dl->AddRectFilledMultiColor(
          p.canvas().P(ImVec2(min.x + 6.f, min.y + 1.f)),
          p.canvas().P(ImVec2(max.x - 6.f, max.y - 1.f)),
          theme::WithAlpha(kBladesHighlightTop, content_alpha),
          theme::WithAlpha(kBladesHighlightTop, content_alpha),
          theme::WithAlpha(kBladesHighlightBottom, content_alpha),
          theme::WithAlpha(kBladesHighlightBottom, content_alpha));
    }
  }

  for (size_t i = 0; i < tab.rows.size(); i++) {
    const Row& row = tab.rows[i];
    const float y = m.top + float(i) * m.row_height;
    if (y >= m.bottom) {
      break;
    }
    const bool focused = i == selection_[tab_] && confirm_ == Confirm::kNone;
    if (metro_) {
      p.Rect(ImVec2(m.content_left, y + m.row_height - 1.f),
             ImVec2(m.content_right, y + m.row_height),
             theme::WithAlpha(kMetroSeparator, content_alpha));
    }
    ImU32 color;
    if (metro_) {
      color = focused ? theme::kWhite : kMetroText;
    } else {
      color = kBladesText;
    }
    const float row_alpha = content_alpha * (row.unavailable && !focused
                                                 ? 0.55f
                                                 : 1.f);
    const float text_y = y + (m.row_height - m.text_size * 1.33f) * 0.5f;
    p.TextClipped(ImVec2(m.content_left + pad + slide, text_y), row.label,
                  m.text_size, theme::WithAlpha(color, row_alpha),
                  m.content_right - m.content_left - pad * 2.f - 110.f);

    float right = m.content_right - pad + slide;
    if (row.icon != GuideIcon::kNone) {
      const float icon_size = m.row_height * 0.58f;
      DrawIcon(p, row.icon,
               ImVec2(right - icon_size * 0.5f, y + m.row_height * 0.5f),
               icon_size,
               theme::WithAlpha(metro_ && !focused ? theme::Rgb(0x2C2C2C)
                                                   : theme::kWhite,
                                row_alpha));
      right -= icon_size + 14.f;
    }
    if (!row.count.empty()) {
      p.Text(ImVec2(right, text_y), row.count, m.text_size,
             theme::WithAlpha(color, row_alpha), DashboardFont::kRegular,
             TextAlign::kRight);
    }
  }
  dl->PopClipRect();
}

void GuideOverlay::DrawFooter(Painter& p, float alpha) {
  const GuideMetrics& m = metro_ ? kMetro : kBlades;
  std::vector<std::pair<char, std::string>> prompts = {{'A', "Select"},
                                                       {'B', "Back"}};
  if (card_.signed_in) {
    prompts.push_back({'X', "Sign Out"});
  }
  prompts.push_back({'Y', metro_ ? "Xbox Home" : "Xbox Dashboard"});
  const float x = metro_ ? m.content_left - m.active_tab_width - m.tab_width +
                               45.f
                         : m.content_left + 6.f;
  p.ButtonPrompts(ImVec2(x, m.footer_y), prompts,
                  theme::WithAlpha(theme::Rgb(0xEDEDED), alpha),
                  metro_ ? 23.f : 18.f);
}

void GuideOverlay::DrawConfirm(Painter& p, float alpha) {
  const GuideMetrics& m = metro_ ? kMetro : kBlades;
  const bool quit = confirm_ == Confirm::kQuit;
  const std::string title =
      quit ? (in_title_ ? fmt::format("Quit {}?", title_name_.empty()
                                                     ? std::string("game")
                                                     : title_name_)
                        : std::string("Close the Xbox Guide?"))
           : std::string("Sign out?");
  const std::string body =
      quit ? (metro_ ? "Unsaved progress will be lost. You'll return to Xbox "
                       "Home."
                     : "Unsaved progress will be lost. You'll return to the "
                       "Xbox Dashboard.")
           : "The game may stop or lose unsaved progress when you sign out.";
  const ImVec2 min(m.content_left + 20.f, m.top + 40.f);
  const ImVec2 max(m.content_right - 20.f, m.top + 210.f);
  const ImU32 bg = metro_ ? theme::Rgb(0xEEEFF0) : theme::Rgb(0x141414, 245);
  const ImU32 fg = metro_ ? kMetroText : kBladesText;
  p.Shadow(min, max, 10.f, 0.6f * alpha, 4.f);
  p.Rect(min, max, theme::WithAlpha(bg, alpha), metro_ ? 0.f : 6.f);
  p.Rect(min, ImVec2(max.x, min.y + 5.f),
         theme::WithAlpha(metro_ ? kMetroHighlightTop : kBladesHighlightTop,
                          alpha));
  p.Text(ImVec2(min.x + 20.f, min.y + 22.f), title, 25.f,
         theme::WithAlpha(fg, alpha));
  p.Text(ImVec2(min.x + 20.f, min.y + 62.f), body, 18.f,
         theme::WithAlpha(fg, alpha * 0.85f), DashboardFont::kRegular,
         TextAlign::kLeft, max.x - min.x - 40.f);
  p.ButtonPrompts(ImVec2(min.x + 20.f, max.y - 42.f),
                  {{'A', quit ? (in_title_ ? "Quit" : "Close") : "Sign Out"},
                   {'B', "Cancel"}},
                  theme::WithAlpha(fg, alpha), 19.f);
}

void GuideOverlay::DrawIcon(Painter& p, GuideIcon icon, ImVec2 center,
                            float size, ImU32 color) {
  ImDrawList* dl = p.draw_list();
  const Canvas& c = p.canvas();
  const float s = size * 0.5f;
  const float stroke = std::max(1.f, c.S(size * 0.09f));
  auto pt = [&](float x, float y) {
    return c.P(ImVec2(center.x + x * s, center.y + y * s));
  };
  switch (icon) {
    case GuideIcon::kFriend:
      dl->AddCircleFilled(pt(0.f, -0.45f), c.S(s * 0.38f), color, 16);
      dl->AddRectFilled(pt(-0.55f, 0.f), pt(0.55f, 0.95f), color,
                        c.S(s * 0.4f), ImDrawFlags_RoundCornersTop);
      break;
    case GuideIcon::kParty:
      dl->AddCircleFilled(pt(-0.45f, -0.35f), c.S(s * 0.28f), color, 12);
      dl->AddCircleFilled(pt(0.45f, -0.35f), c.S(s * 0.28f), color, 12);
      dl->AddCircleFilled(pt(0.f, -0.1f), c.S(s * 0.32f), color, 12);
      dl->AddRectFilled(pt(-0.95f, 0.05f), pt(0.95f, 0.95f), color,
                        c.S(s * 0.35f), ImDrawFlags_RoundCornersTop);
      break;
    case GuideIcon::kMessage:
      dl->AddRectFilled(pt(-0.95f, -0.6f), pt(0.95f, 0.65f), color,
                        c.S(1.f));
      dl->AddTriangleFilled(pt(-0.8f, -0.6f), pt(0.8f, -0.6f), pt(0.f, 0.05f),
                            theme::Rgb(0xD0D2D4));
      break;
    case GuideIcon::kBeacon:
      for (int i = 0; i < 3; i++) {
        const float a = -0.4f - 1.17f * float(i);
        dl->AddLine(pt(0.f, 0.3f), pt(std::cos(a) * 0.95f, std::sin(a) * 0.9f),
                    color, stroke);
      }
      dl->AddLine(pt(-0.95f, 0.75f), pt(0.95f, 0.75f), color, stroke);
      break;
    case GuideIcon::kChat:
      dl->AddRectFilled(pt(-0.9f, -0.8f), pt(0.9f, 0.45f), color,
                        c.S(s * 0.25f));
      dl->AddTriangleFilled(pt(-0.4f, 0.4f), pt(0.1f, 0.4f), pt(-0.5f, 0.95f),
                            color);
      break;
    case GuideIcon::kDisc:
      // Ring + hub so it reads on both the light and the dark list.
      dl->AddCircle(pt(0.f, 0.f), c.S(s * 0.7f), color, 24, c.S(s * 0.5f));
      dl->AddCircleFilled(pt(0.f, 0.f), c.S(s * 0.14f), color, 12);
      break;
    case GuideIcon::kTrophy:
      dl->AddRectFilled(pt(-0.6f, -0.85f), pt(0.6f, -0.1f), color,
                        c.S(s * 0.5f), ImDrawFlags_RoundCornersBottom);
      dl->AddRectFilled(pt(-0.12f, -0.1f), pt(0.12f, 0.5f), color);
      dl->AddRectFilled(pt(-0.5f, 0.5f), pt(0.5f, 0.8f), color);
      break;
    case GuideIcon::kMusic:
      dl->AddCircleFilled(pt(-0.35f, 0.55f), c.S(s * 0.32f), color, 12);
      dl->AddLine(pt(-0.08f, 0.55f), pt(-0.08f, -0.85f), color, stroke);
      dl->AddLine(pt(-0.08f, -0.85f), pt(0.6f, -0.55f), color, stroke);
      break;
    case GuideIcon::kGear:
      dl->AddCircle(pt(0.f, 0.f), c.S(s * 0.55f), color, 16, stroke * 1.5f);
      for (int i = 0; i < 8; i++) {
        const float a = float(i) * 0.785f;
        dl->AddLine(pt(std::cos(a) * 0.6f, std::sin(a) * 0.6f),
                    pt(std::cos(a) * 0.95f, std::sin(a) * 0.95f), color,
                    stroke * 1.6f);
      }
      break;
    case GuideIcon::kHome:
      dl->AddTriangleFilled(pt(-0.95f, -0.05f), pt(0.f, -0.9f),
                            pt(0.95f, -0.05f), color);
      dl->AddRectFilled(pt(-0.65f, -0.05f), pt(0.65f, 0.9f), color);
      break;
    case GuideIcon::kNone:
      break;
  }
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
