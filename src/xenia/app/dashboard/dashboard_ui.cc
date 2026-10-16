/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/dashboard_ui.h"

#include <algorithm>
#include <cmath>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/utf8.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/xam_state.h"

namespace xe {
namespace app {
namespace dashboard {

using ui::DashboardFont;

namespace {

constexpr float kLibraryColumns = 5;
constexpr float kLibraryTileW = 196.f;
constexpr float kLibraryTileH = 196.f;
constexpr float kLibraryRowH = 262.f;
constexpr float kLibraryTop = 150.f;

std::string FormatVersion(uint32_t value) {
  // xex2_version: major:4 minor:4 build:16 qfe:8
  return fmt::format("{}.{}.{}.{}", (value >> 28) & 0xF, (value >> 24) & 0xF,
                     (value >> 8) & 0xFFFF, value & 0xFF);
}

std::string FormatSize(uint64_t bytes) {
  if (bytes >= (1ull << 30)) {
    return fmt::format("{:.2f} GB", double(bytes) / double(1ull << 30));
  }
  return fmt::format("{:.1f} MB", double(bytes) / double(1ull << 20));
}

ImU32 SourceColor(GameSource source) {
  switch (source) {
    case GameSource::kArcade:
      return theme::Rgb(0xE3871E);
    case GameSource::kGamesOnDemand:
      return theme::Rgb(0x2C83C6);
    default:
      return theme::kXboxGreen;
  }
}

// Stable pastel color per title for tiles without art.
ImU32 TitleColor(uint32_t title_id) {
  static constexpr ImU32 palette[] = {
      theme::Rgb(0x3E9A1A), theme::Rgb(0x1C7BC9), theme::Rgb(0x7A3FA0),
      theme::Rgb(0xE07A12), theme::Rgb(0x2B8C7A), theme::Rgb(0xB8322E),
  };
  return palette[(title_id * 2654435761u >> 24) % std::size(palette)];
}

}  // namespace

DashboardDialog::DashboardDialog(ui::ImGuiDrawer* imgui_drawer,
                                 DashboardController* controller)
    : ui::ImGuiDialog(imgui_drawer), controller_(controller) {
  screen_stack_.push_back(Screen::kHome);
  // Prevent the press that opened the dashboard (e.g. the B that closed a
  // game's error box) from acting immediately.
  input_.SwallowHeldButtons();
}

DashboardDialog::~DashboardDialog() = default;

ImU32 DashboardDialog::ScreenAccent() const {
  if (sections_.empty()) {
    return theme::kXboxGreen;
  }
  if (controller_->style() == DashboardStyle::kNxe) {
    return sections_[std::min(channel_, sections_.size() - 1)].accent;
  }
  return sections_[std::min(blade_, sections_.size() - 1)].accent;
}

bool DashboardDialog::on_dark_background() const {
  return controller_->style() == DashboardStyle::kBlades;
}

void DashboardDialog::PushScreen(Screen screen) {
  screen_stack_.push_back(screen);
  screen_fade_ = 0.f;
  if (screen == Screen::kLibrary) {
    games_ = controller_->library().GetGames();
  }
}

void DashboardDialog::PopScreen() {
  if (screen_stack_.size() > 1) {
    screen_stack_.pop_back();
    screen_fade_ = 0.f;
    controller_->audio().Play(SoundEvent::kBack);
  }
}

void DashboardDialog::Activate(const HomeItem& item) {
  if (item.unavailable) {
    controller_->audio().Play(SoundEvent::kError);
    controller_->ShowToast(fmt::format(
        "{} needs Xbox LIVE or a media service, which isn't available.",
        item.title));
    return;
  }
  controller_->audio().Play(SoundEvent::kSelect);
  if (item.action) {
    item.action();
  }
}

void DashboardDialog::OpenGame(std::shared_ptr<const GameEntry> game) {
  if (!game) {
    return;
  }
  controller_->audio().Play(SoundEvent::kSelect);
  controller_->LaunchGame(game->launch_path);
}

// ---------------------------------------------------------------------------
// Content model
// ---------------------------------------------------------------------------

void DashboardDialog::BuildSections() {
  DashboardController* c = controller_;
  const ProfileCard card = c->GetProfileCard(0);
  const GameList games = c->library().GetGames();
  const auto recents = c->GetRecentTitles(4);
  const bool has_library_path = !c->library().root().empty();

  auto library_item = [&]() {
    HomeItem item;
    item.title = "Game Library";
    if (!has_library_path) {
      item.subtitle = "Choose the folder that holds your games";
      item.action = [c]() { c->ChooseLibraryFolder(); };
    } else {
      item.subtitle = c->library().IsScanning()
                          ? fmt::format("Searching... {} games found",
                                        games.size())
                          : fmt::format("{} games", games.size());
      item.action = [this]() { PushScreen(Screen::kLibrary); };
    }
    item.color = theme::kNxeAccentGames;
    if (!games.empty()) {
      item.image = c->GetGameCover(*games.front());
      item.image_is_cover = item.image != nullptr;
    }
    return item;
  };
  auto recent_items = [&](size_t count) {
    std::vector<HomeItem> items;
    for (const auto& recent : recents) {
      if (items.size() >= count) {
        break;
      }
      HomeItem item;
      item.title = recent.title_name.empty()
                       ? xe::path_to_utf8(recent.path.filename())
                       : recent.title_name;
      item.subtitle = "Recently played";
      item.color = recent.game ? TitleColor(recent.game->title_id)
                               : theme::kNxeAccentGames;
      if (recent.game) {
        item.image = c->GetGameCover(*recent.game);
        item.image_is_cover = item.image != nullptr;
        if (!item.image) {
          item.image = c->GetGameIcon(*recent.game);
        }
      }
      const auto path = recent.path;
      item.action = [c, path]() { c->LaunchGame(path); };
      items.push_back(std::move(item));
    }
    return items;
  };
  auto achievements_item = [&]() {
    HomeItem item;
    item.title = "Achievements";
    item.subtitle = card.signed_in
                        ? fmt::format("{} unlocked across {} games",
                                      card.achievements_unlocked,
                                      card.titles_played)
                        : "Sign in to view your achievements";
    item.color = theme::Rgb(0x2B8C7A);
    item.action = [c, signed_in = card.signed_in]() {
      if (signed_in) {
        c->OpenAchievementsList(0);
      } else {
        c->ShowToast("Sign in to a profile to view achievements.");
      }
    };
    return item;
  };
  auto marketplace_item = [&]() {
    HomeItem item;
    item.title = "Game Marketplace";
    item.subtitle = "Add-ons and content installed on this console";
    item.color = theme::kBladeMarketplace;
    item.action = [this]() { PushScreen(Screen::kMarketplace); };
    return item;
  };
  auto gamercard_item = [&]() {
    HomeItem item;
    item.title = card.signed_in ? card.gamertag : "Sign In";
    item.subtitle = card.signed_in
                        ? (card.motto.empty()
                               ? fmt::format("Gamerscore {}", card.gamerscore)
                               : card.motto)
                        : "Choose a profile or create a new one";
    item.color = theme::kNxeAccentMyXbox;
    item.image = card.gamerpic;
    item.action = [this]() { PushScreen(Screen::kProfiles); };
    return item;
  };
  auto avatar_item = [&]() {
    HomeItem item;
    item.title = "Avatar";
    item.subtitle = "Gamer picture (3D avatars aren't rendered yet)";
    item.color = theme::Rgb(0x5F7F2A);
    item.image = card.avatar_image ? card.avatar_image : card.gamerpic;
    const uint64_t xuid = card.xuid;
    item.action = [c, xuid]() {
      if (xuid) {
        c->OpenGamercardEditor(xuid);
      } else {
        c->ShowToast("Sign in to change your gamer picture.");
      }
    };
    return item;
  };
  auto settings_item = [&]() {
    HomeItem item;
    item.title = "System Settings";
    item.subtitle = "Dashboard, console, display and profiles";
    item.color = theme::Rgb(0x5E6670);
    item.action = [this]() { PushScreen(Screen::kSettings); };
    return item;
  };
  auto memory_item = [&]() {
    HomeItem item;
    item.title = "Memory";
    item.subtitle = "Installed content, saves and title updates";
    item.color = theme::Rgb(0x46505A);
    item.action = [c]() { c->OpenInstalledContent(); };
    return item;
  };
  auto unavailable = [](std::string title, std::string subtitle,
                        ImU32 color) {
    HomeItem item;
    item.title = std::move(title);
    item.subtitle = std::move(subtitle);
    item.color = color;
    item.unavailable = true;
    return item;
  };

  sections_.clear();
  if (c->style() == DashboardStyle::kNxe) {
    HomeSection spotlight{"spotlight", theme::kNxeAccentSpotlight, {}};
    for (auto& item : recent_items(1)) {
      item.subtitle = "Jump back in";
      spotlight.items.push_back(std::move(item));
    }
    spotlight.items.push_back(library_item());
    spotlight.items.push_back(gamercard_item());
    spotlight.items.push_back(unavailable(
        "Featured Content", "Xbox LIVE spotlight content",
        theme::kNxeAccentSpotlight));
    sections_.push_back(std::move(spotlight));

    HomeSection games_channel{"games", theme::kNxeAccentGames, {}};
    games_channel.items.push_back(library_item());
    for (auto& item : recent_items(3)) {
      games_channel.items.push_back(std::move(item));
    }
    games_channel.items.push_back(achievements_item());
    games_channel.items.push_back(marketplace_item());
    sections_.push_back(std::move(games_channel));

    HomeSection video{"video", theme::kNxeAccentVideo, {}};
    video.items.push_back(unavailable("Video Library", "Movies and TV shows",
                                      theme::kNxeAccentVideo));
    video.items.push_back(unavailable("Video Marketplace",
                                      "Rent and buy movies",
                                      theme::kNxeAccentVideo));
    sections_.push_back(std::move(video));

    HomeSection music{"music", theme::kNxeAccentMusic, {}};
    music.items.push_back(unavailable("Music Library", "Songs and playlists",
                                      theme::kNxeAccentMusic));
    music.items.push_back(unavailable("Picture Library", "Photos and slide "
                                      "shows", theme::kNxeAccentMusic));
    sections_.push_back(std::move(music));

    HomeSection friends{"friends", theme::kNxeAccentFriends, {}};
    friends.items.push_back(unavailable("Friends", "See who's online",
                                        theme::kNxeAccentFriends));
    friends.items.push_back(unavailable("Messages", "Voice and text messages",
                                        theme::kNxeAccentFriends));
    friends.items.push_back(unavailable("Party", "Chat with up to 8 friends",
                                        theme::kNxeAccentFriends));
    sections_.push_back(std::move(friends));

    HomeSection my_xbox{"my xbox", theme::kNxeAccentMyXbox, {}};
    my_xbox.items.push_back(gamercard_item());
    my_xbox.items.push_back(avatar_item());
    my_xbox.items.push_back(library_item());
    my_xbox.items.push_back(settings_item());
    my_xbox.items.push_back(memory_item());
    sections_.push_back(std::move(my_xbox));
  } else {
    HomeSection market{"Marketplace", theme::kBladeMarketplace, {}};
    market.items.push_back(marketplace_item());
    HomeItem install;
    install.title = "Install Content Package";
    install.subtitle = "Install DLC, title updates or arcade games from a file";
    install.action = [c]() { c->InstallContentPackage(); };
    market.items.push_back(install);
    market.items.push_back(unavailable("Featured Downloads",
                                       "New content on Xbox LIVE",
                                       theme::kBladeMarketplace));
    market.items.push_back(unavailable("Active Downloads",
                                       "Downloads in progress",
                                       theme::kBladeMarketplace));
    sections_.push_back(std::move(market));

    HomeSection live{"Xbox Live", theme::kBladeLive, {}};
    live.items.push_back(gamercard_item());
    HomeItem edit;
    edit.title = "Edit Gamercard";
    edit.subtitle = "Gamer picture, motto, bio and gamer zone";
    edit.action = [c, xuid = card.xuid]() {
      if (xuid) {
        c->OpenGamercardEditor(xuid);
      } else {
        c->ShowToast("Sign in to edit your gamercard.");
      }
    };
    live.items.push_back(edit);
    live.items.push_back(unavailable("Friends", "See who's online",
                                     theme::kBladeLive));
    live.items.push_back(unavailable("Messages", "Voice and text messages",
                                     theme::kBladeLive));
    sections_.push_back(std::move(live));

    HomeSection games_blade{"Games", theme::kBladeGames, {}};
    games_blade.items.push_back(library_item());
    for (auto& item : recent_items(3)) {
      games_blade.items.push_back(std::move(item));
    }
    games_blade.items.push_back(achievements_item());
    sections_.push_back(std::move(games_blade));

    HomeSection media{"Media", theme::kBladeMedia, {}};
    media.items.push_back(unavailable("Music", "Play music from a device",
                                      theme::kBladeMedia));
    media.items.push_back(unavailable("Pictures", "View photos",
                                      theme::kBladeMedia));
    media.items.push_back(unavailable("Video", "Play videos",
                                      theme::kBladeMedia));
    sections_.push_back(std::move(media));

    HomeSection system{"System", theme::kBladeSystem, {}};
    system.items.push_back(settings_item());
    HomeItem console;
    console.title = "Console Settings";
    console.subtitle = "Language, region, clock and audio/video";
    console.action = [c]() { c->OpenConsoleSettings(); };
    system.items.push_back(console);
    HomeItem display;
    display.title = "Display";
    display.subtitle = "Post-processing and presentation options";
    display.action = [c]() { c->OpenDisplaySettings(); };
    system.items.push_back(display);
    system.items.push_back(memory_item());
    HomeItem profiles;
    profiles.title = "Profiles";
    profiles.subtitle = "Sign in, create or edit profiles";
    profiles.action = [this]() { PushScreen(Screen::kProfiles); };
    system.items.push_back(profiles);
    sections_.push_back(std::move(system));
  }

  tile_index_.resize(sections_.size(), 0);
  tile_anim_.resize(sections_.size(), 0.f);
  blade_item_.resize(sections_.size(), 0);
  for (size_t i = 0; i < sections_.size(); i++) {
    const size_t count = sections_[i].items.size();
    tile_index_[i] = std::min(tile_index_[i], count ? count - 1 : 0);
    blade_item_[i] = std::min(blade_item_[i], count ? count - 1 : 0);
  }
  channel_ = std::min(channel_, sections_.size() - 1);
  blade_ = std::min(blade_, sections_.size() - 1);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void DashboardDialog::OnDraw(ImGuiIO& io) {
  delta_time_ = std::clamp(io.DeltaTime, 0.f, 0.1f);
  time_ += delta_time_;

  // Only take input while nothing else (guide, message box, profile editor)
  // is open: those are drawn above the dashboard and own the controller.
  const bool has_focus = imgui_drawer()->GetDialogCount() == 1 &&
                         imgui_drawer()->GetTopDialog() == this;
  input_.Update(controller_->emulator()->input_system(), delta_time_,
                has_focus);

  // Rebuild the home model when the library changes and periodically for
  // profile/gamercard changes made in other dialogs.
  rebuild_timer_ -= delta_time_;
  const uint32_t generation = controller_->library().generation();
  if (generation != built_library_generation_ || rebuild_timer_ <= 0.f ||
      sections_.empty() || built_style_ != controller_->style()) {
    built_library_generation_ = generation;
    built_style_ = controller_->style();
    rebuild_timer_ = 1.f;
    BuildSections();
    if (!screen_stack_.empty() && screen_stack_.back() == Screen::kLibrary) {
      games_ = controller_->library().GetGames();
    }
  }

  const Canvas canvas = Canvas::FromDisplay(io.DisplaySize);
  Painter p(ImGui::GetBackgroundDrawList(), imgui_drawer(), canvas);
  DrawBackground(p);

  switch (screen_stack_.back()) {
    case Screen::kHome:
      if (controller_->style() == DashboardStyle::kNxe) {
        UpdateNxeHome();
        DrawNxeHome(p);
      } else {
        UpdateBladesHome();
        DrawBladesHome(p);
      }
      break;
    case Screen::kLibrary:
      UpdateLibrary();
      DrawLibrary(p);
      break;
    case Screen::kGameDetails:
      UpdateGameDetails();
      DrawGameDetails(p);
      break;
    case Screen::kProfiles:
      UpdateProfiles();
      DrawProfiles(p);
      break;
    case Screen::kSettings:
      UpdateSettings();
      DrawSettings(p);
      break;
    case Screen::kMarketplace:
      UpdateMarketplace();
      DrawMarketplace(p);
      break;
  }

  // Cross-fade between screens.
  screen_fade_ = theme::Approach(screen_fade_, 1.f, delta_time_,
                                 theme::kFadeTime);
  if (screen_fade_ < 1.f) {
    const ImU32 bg = on_dark_background() ? theme::kBladeBackdrop
                                          : theme::kNxeBackgroundTop;
    p.FillBackground(theme::WithAlpha(bg, 1.f - screen_fade_),
                     theme::WithAlpha(bg, 1.f - screen_fade_));
  }

  DrawToast(p);
}

void DashboardDialog::DrawBackground(Painter& p) {
  const ImU32 accent = ScreenAccent();
  if (!on_dark_background()) {
    p.FillBackground(theme::kNxeBackgroundTop, theme::kNxeBackgroundBottom);
    // Slowly drifting light blooms tinted with the channel color.
    const float t = time_ * 0.08f;
    p.SoftGlow(ImVec2(980.f + std::sin(t) * 140.f, 180.f + std::cos(t) * 40.f),
               420.f, theme::WithAlpha(accent, 0.20f));
    p.SoftGlow(ImVec2(260.f + std::cos(t * 0.7f) * 120.f, 640.f), 380.f,
               theme::WithAlpha(theme::kWhite, 0.55f));
    p.HorizontalGradient(ImVec2(-400.f, 0.f), ImVec2(1680.f, 720.f),
                         theme::Rgb(0xFFFFFF, 0), theme::Rgb(0x000000, 18));
  } else {
    p.FillBackground(theme::Rgb(0x1A1F1A), theme::kBladeBackdrop);
    // Concentric "ring of light" arcs behind the blades.
    const ImVec2 center = p.canvas().P(1040.f, 380.f);
    for (int i = 0; i < 6; i++) {
      const float r = p.canvas().S(140.f + i * 70.f);
      const float a0 = time_ * (0.10f + i * 0.02f) + i;
      p.draw_list()->PathArcTo(center, r, a0, a0 + 2.2f, 48);
      p.draw_list()->PathStroke(theme::WithAlpha(accent, 0.10f - i * 0.012f),
                                0, p.canvas().S(18.f));
    }
  }
}

void DashboardDialog::DrawGamercardBadge(Painter& p, ImVec2 top_right,
                                         bool on_dark) {
  const ProfileCard card = controller_->GetProfileCard(0);
  const ImU32 text = on_dark ? theme::kWhite : theme::kTextDark;
  const ImU32 sub = on_dark ? theme::kTextLightGray : theme::kTextGray;
  const float pic = 48.f;
  const ImVec2 pic_min(top_right.x - pic, top_right.y);
  p.Shadow(pic_min, ImVec2(top_right.x, top_right.y + pic), 4.f, 0.4f, 4.f);
  p.Rect(pic_min, ImVec2(top_right.x, top_right.y + pic),
         theme::Rgb(0x4B5A44), 4.f);
  if (card.gamerpic) {
    p.Image(card.gamerpic, pic_min, ImVec2(top_right.x, top_right.y + pic),
            true, IM_COL32_WHITE, 4.f);
  } else {
    p.Circle(ImVec2(pic_min.x + pic * 0.5f, pic_min.y + pic * 0.38f), 10.f,
             theme::Rgb(0xC8D0C0));
    p.Rect(ImVec2(pic_min.x + 12.f, pic_min.y + 30.f),
           ImVec2(top_right.x - 12.f, top_right.y + pic), theme::Rgb(0xC8D0C0),
           8.f);
  }
  const float tx = pic_min.x - 12.f;
  if (card.signed_in) {
    p.Text(ImVec2(tx, top_right.y + 2.f), card.gamertag, 22.f, text,
           DashboardFont::kSemibold, TextAlign::kRight);
    const ImVec2 gs_size = p.MeasureText(fmt::format("{}", card.gamerscore),
                                         17.f);
    p.Gamerscore(ImVec2(tx - gs_size.x - 24.f, top_right.y + 27.f),
                 card.gamerscore, 17.f, sub);
  } else {
    p.Text(ImVec2(tx, top_right.y + 12.f), "Not signed in", 20.f, sub,
           DashboardFont::kRegular, TextAlign::kRight);
  }
}

void DashboardDialog::DrawScreenHeader(Painter& p, const std::string& title,
                                       const std::string& subtitle) {
  const bool dark = on_dark_background();
  p.Text(ImVec2(theme::kSafeX, 52.f), title, 46.f,
         dark ? theme::kWhite : theme::kTextDark, DashboardFont::kLight);
  if (!subtitle.empty()) {
    p.Text(ImVec2(theme::kSafeX + 2.f, 108.f), subtitle, 18.f,
           dark ? theme::kTextLightGray : theme::kTextGray);
  }
  DrawGamercardBadge(p, ImVec2(1280.f - theme::kSafeX, 52.f), dark);
}

void DashboardDialog::DrawToast(Painter& p) {
  const std::string& message = controller_->toast();
  const float age = controller_->toast_age();
  if (message.empty() || age > 3.f) {
    return;
  }
  const float alpha =
      std::min({1.f, age / 0.15f, std::max(0.f, (3.f - age) / 0.4f)});
  const ImVec2 size = p.MeasureText(message, 19.f);
  const float w = size.x + 48.f;
  const ImVec2 min(640.f - w * 0.5f, 600.f);
  const ImVec2 max(640.f + w * 0.5f, 644.f);
  p.Shadow(min, max, 8.f, 0.5f * alpha, 8.f);
  p.Rect(min, max, theme::WithAlpha(theme::Rgb(0x1C201C), 0.94f * alpha),
         8.f);
  p.Rect(ImVec2(min.x, min.y), ImVec2(min.x + 5.f, max.y),
         theme::WithAlpha(theme::kXboxGreen, alpha), 2.f);
  p.Text(ImVec2(640.f, min.y + 11.f), message, 19.f,
         theme::WithAlpha(theme::kWhite, alpha), DashboardFont::kRegular,
         TextAlign::kCenter);
}

// ---------------------------------------------------------------------------
// NXE home
// ---------------------------------------------------------------------------

void DashboardDialog::UpdateNxeHome() {
  if (sections_.empty()) {
    return;
  }
  const int v = input_.Vertical();
  if (v != 0) {
    const int next = int(channel_) + v;
    if (next >= 0 && next < int(sections_.size())) {
      channel_ = size_t(next);
      controller_->audio().Play(SoundEvent::kChannelChange);
    }
  }
  HomeSection& section = sections_[channel_];
  size_t& tile = tile_index_[channel_];
  int h = input_.Horizontal();
  if (input_.Pressed(Button::kRightBumper)) {
    h = 3;
  } else if (input_.Pressed(Button::kLeftBumper)) {
    h = -3;
  }
  if (h != 0 && !section.items.empty()) {
    const int next = std::clamp(int(tile) + h, 0,
                                int(section.items.size()) - 1);
    if (next != int(tile)) {
      tile = size_t(next);
      controller_->audio().Play(SoundEvent::kNavigate);
    }
  }
  if (input_.Pressed(Button::kA) && tile < section.items.size()) {
    Activate(section.items[tile]);
  }
  if (input_.Pressed(Button::kB) && tile > 0) {
    // B on the home screen returns to the first slate of the channel.
    tile = 0;
    controller_->audio().Play(SoundEvent::kBack);
  }
  if (input_.Pressed(Button::kY)) {
    controller_->OpenGuide();
  }
}

void DashboardDialog::DrawNxeTile(Painter& p, const HomeItem& item,
                                  ImVec2 origin, float width, float height,
                                  float perspective, float alpha,
                                  bool focused) {
  if (alpha <= 0.01f) {
    return;
  }
  const float inset = height * perspective * 0.5f;
  const ImVec2 quad[4] = {
      origin,
      ImVec2(origin.x + width, origin.y + inset),
      ImVec2(origin.x + width, origin.y + height - inset),
      ImVec2(origin.x, origin.y + height),
  };
  auto lerp_quad = [&](float u, float v) {
    // Bilinear point inside the tile quad.
    const ImVec2 top(quad[0].x + (quad[1].x - quad[0].x) * u,
                     quad[0].y + (quad[1].y - quad[0].y) * u);
    const ImVec2 bottom(quad[3].x + (quad[2].x - quad[3].x) * u,
                        quad[3].y + (quad[2].y - quad[3].y) * u);
    return ImVec2(top.x + (bottom.x - top.x) * v,
                  top.y + (bottom.y - top.y) * v);
  };

  // Reflection-style soft shadow underneath.
  p.Shadow(ImVec2(origin.x + 4.f, origin.y + 8.f),
           ImVec2(origin.x + width - 4.f, origin.y + height),
           focused ? 14.f : 8.f, 0.35f * alpha, 6.f);

  const ImU32 base = item.color;
  const ImU32 top = theme::WithAlpha(theme::Lerp(base, theme::kWhite, 0.18f),
                                     alpha);
  const ImU32 bottom = theme::WithAlpha(theme::Lerp(base, theme::kBlack, 0.25f),
                                        alpha);
  p.QuadGradient(quad, top, bottom);

  if (item.image) {
    const ImU32 tint = theme::WithAlpha(theme::kWhite, alpha);
    if (item.image_is_cover) {
      p.ImageQuad(item.image, quad, tint);
    } else {
      // Center the icon as a square occupying ~55% of the tile height.
      const float icon_v = 0.55f;
      const float icon_u = icon_v * height / width;
      const float u0 = 0.5f - icon_u * 0.5f, u1 = 0.5f + icon_u * 0.5f;
      const float v0 = 0.14f, v1 = v0 + icon_v;
      const ImVec2 icon_quad[4] = {lerp_quad(u0, v0), lerp_quad(u1, v0),
                                   lerp_quad(u1, v1), lerp_quad(u0, v1)};
      p.ImageQuad(item.image, icon_quad, tint);
    }
  } else {
    // Large faint initial as a placeholder graphic.
    const std::string initial = item.title.substr(0, 1);
    const ImVec2 center = lerp_quad(0.5f, 0.42f);
    p.Text(ImVec2(center.x, center.y - height * 0.26f), initial,
           height * 0.48f, theme::WithAlpha(theme::kWhite, 0.25f * alpha),
           DashboardFont::kLight, TextAlign::kCenter);
  }

  if (item.unavailable) {
    p.Quad(quad, theme::WithAlpha(theme::Rgb(0x808080), 0.45f * alpha));
  }

  // Caption strip.
  const ImVec2 strip[4] = {lerp_quad(0.f, 0.70f), lerp_quad(1.f, 0.70f),
                           quad[2], quad[3]};
  p.QuadGradient(strip, theme::WithAlpha(theme::kBlack, 0.f),
                 theme::WithAlpha(theme::kBlack, 0.62f * alpha));
  const float title_size = focused ? 26.f : std::max(13.f, height * 0.075f);
  const ImVec2 text_pos = lerp_quad(0.f, 0.80f);
  if (width > 110.f) {
    p.TextClipped(ImVec2(text_pos.x + 16.f, text_pos.y - (focused ? 8.f : 2.f)),
                  item.title, title_size,
                  theme::WithAlpha(theme::kWhite, alpha), width - 32.f,
                  DashboardFont::kSemibold);
    if (focused && !item.subtitle.empty()) {
      p.TextClipped(ImVec2(text_pos.x + 16.f, text_pos.y + 24.f),
                    item.subtitle, 16.f,
                    theme::WithAlpha(theme::kTextLightGray, alpha),
                    width - 32.f);
    }
  }

  if (focused) {
    const float pulse = 0.75f + 0.25f * std::sin(time_ * 4.f);
    for (int i = 0; i < 4; i++) {
      p.draw_list()->AddLine(p.canvas().P(quad[i]),
                             p.canvas().P(quad[(i + 1) % 4]),
                             theme::WithAlpha(theme::kWhite, pulse * alpha),
                             p.canvas().S(3.f));
    }
  }
}

void DashboardDialog::DrawNxeHome(Painter& p) {
  if (sections_.empty()) {
    return;
  }
  channel_anim_ = theme::Approach(channel_anim_, float(channel_), delta_time_,
                                  theme::kChannelSlideTime);

  constexpr float kTileX = 150.f;
  constexpr float kTileW = 520.f;
  constexpr float kTileH = 292.f;
  constexpr float kRowY = 222.f;
  constexpr float kRowSpacing = 430.f;

  for (size_t c = 0; c < sections_.size(); c++) {
    const float dc = float(c) - channel_anim_;
    if (std::abs(dc) > 1.6f) {
      continue;
    }
    HomeSection& section = sections_[c];
    tile_anim_[c] = theme::Approach(tile_anim_[c], float(tile_index_[c]),
                                    delta_time_, theme::kTileSlideTime);
    const float row_y = kRowY + dc * kRowSpacing;
    const float row_alpha = std::clamp(1.f - std::abs(dc) * 0.8f, 0.f, 1.f);
    const bool current_channel = c == channel_;

    // Channel name.
    const ImU32 name_color =
        theme::Lerp(theme::kNxeChannelInactive, section.accent,
                    std::clamp(1.f - std::abs(dc), 0.f, 1.f));
    const float name_size = 50.f - std::min(1.f, std::abs(dc)) * 24.f;
    p.Text(ImVec2(kTileX - 4.f, row_y - 18.f - name_size),
           section.name, name_size, theme::WithAlpha(name_color,
                                                     0.35f + row_alpha * 0.65f),
           DashboardFont::kLight);

    // Tiles, far to near so the focused slate is on top.
    struct Placement {
      size_t index;
      float d;
    };
    std::vector<Placement> order;
    for (size_t i = 0; i < section.items.size(); i++) {
      const float d = float(i) - tile_anim_[c];
      if (d < -1.3f || d > 6.f) {
        continue;
      }
      order.push_back({i, d});
    }
    std::sort(order.begin(), order.end(),
              [](const Placement& a, const Placement& b) { return a.d > b.d; });

    for (const Placement& place : order) {
      const float d = place.d;
      float x, w, h, persp, alpha;
      if (d <= 0.f) {
        x = kTileX + d * (kTileW + 60.f);
        w = kTileW;
        h = kTileH;
        persp = 0.f;
        alpha = std::clamp(1.f + d * 1.2f, 0.f, 1.f);
      } else {
        auto receding = [&](float k, float& rx, float& rw, float& rh,
                            float& rp) {
          const float shrink = std::pow(0.88f, k - 1.f);
          rx = kTileX + kTileW + 40.f + (k - 1.f) * 136.f;
          rw = 210.f * shrink;
          rh = kTileH * 0.66f * shrink;
          rp = 0.10f;
        };
        if (d >= 1.f) {
          receding(d, x, w, h, persp);
        } else {
          float x1, w1, h1, p1;
          receding(1.f, x1, w1, h1, p1);
          const float e = theme::EaseOutCubic(d);
          x = kTileX + (x1 - kTileX) * e;
          w = kTileW + (w1 - kTileW) * e;
          h = kTileH + (h1 - kTileH) * e;
          persp = p1 * e;
        }
        alpha = std::clamp(5.5f - d, 0.f, 1.f);
      }
      const float y = row_y + (kTileH - h) * 0.5f;
      const bool focused = current_channel && place.index == tile_index_[c] &&
                           std::abs(d) < 0.5f;
      DrawNxeTile(p, section.items[place.index], ImVec2(x, y), w, h, persp,
                  alpha * row_alpha * (current_channel ? 1.f : 0.55f),
                  focused);
    }

    // Slate counter under the focused channel.
    if (current_channel && section.items.size() > 1) {
      for (size_t i = 0; i < section.items.size(); i++) {
        const bool active = i == tile_index_[c];
        p.Circle(ImVec2(kTileX + 8.f + i * 18.f, row_y + kTileH + 22.f),
                 active ? 5.f : 3.5f,
                 active ? section.accent
                        : theme::WithAlpha(theme::kTextGray, 0.5f));
      }
    }
  }

  DrawGamercardBadge(p, ImVec2(1280.f - theme::kSafeX, 40.f), false);
  p.ButtonPrompts(ImVec2(kTileX, 662.f),
                  {{'A', "Select"}, {'Y', "Xbox Guide"}}, theme::kTextDark);
}

// ---------------------------------------------------------------------------
// Blades home
// ---------------------------------------------------------------------------

void DashboardDialog::UpdateBladesHome() {
  if (sections_.empty()) {
    return;
  }
  HomeSection& section = sections_[blade_];
  size_t& item = blade_item_[blade_];

  int blade_move = 0;
  if (input_.Pressed(Button::kRightBumper) || input_.Pressed(Button::kRight)) {
    blade_move = 1;
  } else if (input_.Pressed(Button::kLeftBumper) ||
             input_.Pressed(Button::kLeft)) {
    blade_move = -1;
  }
  if (blade_move) {
    const int next = int(blade_) + blade_move;
    if (next >= 0 && next < int(sections_.size())) {
      blade_ = size_t(next);
      controller_->audio().Play(SoundEvent::kBladeSlide);
    }
  }
  const int v = input_.Vertical();
  if (v != 0 && !section.items.empty()) {
    const int next =
        std::clamp(int(item) + v, 0, int(section.items.size()) - 1);
    if (next != int(item)) {
      item = size_t(next);
      controller_->audio().Play(SoundEvent::kNavigate);
    }
  }
  if (input_.Pressed(Button::kA) && item < section.items.size()) {
    Activate(section.items[item]);
  }
  if (input_.Pressed(Button::kY)) {
    controller_->OpenGuide();
  }
}

void DashboardDialog::DrawBladeContent(Painter& p, const HomeSection& section,
                                       size_t section_index, float x0,
                                       float x1) {
  const float left = x0 + 28.f;
  p.Text(ImVec2(left, 86.f), section.name, 40.f, theme::kWhite,
         DashboardFont::kLight);

  const size_t selected = blade_item_[section_index];
  const float list_w = std::min(360.f, (x1 - x0) * 0.42f);
  float y = 160.f;
  for (size_t i = 0; i < section.items.size(); i++) {
    const HomeItem& item = section.items[i];
    const bool focused = i == selected;
    if (focused) {
      p.Shadow(ImVec2(left - 8.f, y - 4.f), ImVec2(left + list_w, y + 36.f),
               4.f, 0.3f, 4.f);
      p.VerticalGradient(ImVec2(left - 8.f, y - 4.f),
                         ImVec2(left + list_w, y + 36.f), theme::kWhite,
                         theme::Rgb(0xE4E8E0));
    }
    const ImU32 color =
        focused ? theme::kTextDark
                : theme::WithAlpha(theme::kWhite, item.unavailable ? 0.5f : 1.f);
    p.TextClipped(ImVec2(left + 6.f, y), item.title, 22.f, color,
                  list_w - 20.f,
                  focused ? DashboardFont::kSemibold : DashboardFont::kRegular);
    y += 44.f;
  }

  // Preview pane for the selected item.
  if (selected >= section.items.size()) {
    return;
  }
  const HomeItem& item = section.items[selected];
  const float px = left + list_w + 36.f;
  const float pw = x1 - px - 28.f;
  if (pw < 120.f) {
    return;
  }
  const float art = std::min(pw, 240.f);
  const ImVec2 art_min(px, 160.f);
  const ImVec2 art_max(px + art, 160.f + art * (item.image_is_cover ? 1.4f
                                                                     : 1.f));
  p.Shadow(art_min, art_max, 8.f, 0.45f, 4.f);
  p.VerticalGradient(art_min, art_max,
                     theme::Lerp(item.color, theme::kWhite, 0.15f),
                     theme::Lerp(item.color, theme::kBlack, 0.3f));
  if (item.image) {
    if (item.image_is_cover) {
      p.Image(item.image, art_min, art_max, true);
    } else {
      p.Image(item.image, ImVec2(art_min.x + art * 0.18f, art_min.y + art * 0.18f),
              ImVec2(art_max.x - art * 0.18f, art_max.y - art * 0.18f));
    }
  } else {
    p.Text(ImVec2((art_min.x + art_max.x) * 0.5f, art_min.y + art * 0.18f),
           item.title.substr(0, 1), art * 0.55f,
           theme::WithAlpha(theme::kWhite, 0.3f), DashboardFont::kLight,
           TextAlign::kCenter);
  }
  const float text_y = art_max.y + 18.f;
  p.TextClipped(ImVec2(px, text_y), item.title, 26.f, theme::kWhite, pw,
                DashboardFont::kSemibold);
  p.Text(ImVec2(px, text_y + 36.f),
         item.unavailable ? "Requires Xbox LIVE, which isn't available."
                          : item.subtitle,
         17.f, theme::kTextLightGray, DashboardFont::kRegular,
         TextAlign::kLeft, pw);
}

void DashboardDialog::DrawBladesHome(Painter& p) {
  if (sections_.empty()) {
    return;
  }
  blade_anim_ = theme::Approach(blade_anim_, float(blade_), delta_time_,
                                theme::kBladeSlideTime);

  const size_t n = sections_.size();
  constexpr float kTabW = 36.f;
  constexpr float kLeft = 60.f;
  constexpr float kRight = 1220.f;
  constexpr float kTop = 62.f;
  constexpr float kBottom = 640.f;

  auto blade_x = [&](size_t i) {
    const float d = float(i) - blade_anim_;
    const float left_x = kLeft + float(i) * kTabW;
    const float right_x = kRight - float(n - i) * kTabW;
    if (d <= 0.f) {
      return left_x;
    }
    if (d >= 1.f) {
      return right_x;
    }
    return left_x + (right_x - left_x) * theme::EaseOutCubic(d);
  };

  for (size_t i = 0; i < n; i++) {
    const HomeSection& section = sections_[i];
    const float x = blade_x(i);
    const ImVec2 min(x, kTop);
    const ImVec2 max(kRight, kBottom);

    // Depth shadow cast onto the blade underneath.
    p.HorizontalGradient(ImVec2(x - 22.f, kTop), ImVec2(x, kBottom),
                         theme::Rgb(0x000000, 0), theme::Rgb(0x000000, 120));
    p.draw_list()->AddRectFilledMultiColor(
        p.canvas().P(min), p.canvas().P(max),
        theme::Lerp(section.accent, theme::kWhite, 0.22f),
        theme::Lerp(section.accent, theme::kWhite, 0.10f),
        theme::Lerp(section.accent, theme::kBlack, 0.30f),
        theme::Lerp(section.accent, theme::kBlack, 0.18f));
    // Tab highlight and label.
    p.HorizontalGradient(min, ImVec2(x + kTabW, kBottom),
                         theme::Rgb(0xFFFFFF, 60), theme::Rgb(0xFFFFFF, 0));
    p.Rect(min, ImVec2(x + 2.f, kBottom), theme::Rgb(0xFFFFFF, 140));
    p.TextVertical(ImVec2(x + 8.f, kBottom - 18.f), section.name, 21.f,
                   theme::WithAlpha(theme::kWhite, 0.92f),
                   DashboardFont::kSemibold);

    // Content of the blade(s) currently open.
    const float next_x = i + 1 < n ? blade_x(i + 1) : kRight;
    if (next_x - x > 260.f) {
      ImDrawList* dl = p.draw_list();
      dl->PushClipRect(p.canvas().P(x + kTabW, kTop),
                       p.canvas().P(next_x, kBottom), true);
      DrawBladeContent(p, section, i, x + kTabW, next_x);
      dl->PopClipRect();
    }
  }

  // Keep the gamercard on the open blade, clear of the stacked tabs.
  const float open_right = blade_ + 1 < n ? blade_x(blade_ + 1) : kRight;
  DrawGamercardBadge(p, ImVec2(open_right - 24.f, 82.f), true);
  p.ButtonPrompts(ImVec2(kLeft, 662.f),
                  {{'A', "Select"}, {'Y', "Xbox Guide"}},
                  theme::kTextLightGray);
  p.Text(ImVec2(kRight, 664.f), "LB / RB  change blade", 17.f,
         theme::kTextLightGray, DashboardFont::kRegular, TextAlign::kRight);
}

// ---------------------------------------------------------------------------
// Game library
// ---------------------------------------------------------------------------

void DashboardDialog::UpdateLibrary() {
  GameLibrary& library = controller_->library();
  if (input_.Pressed(Button::kB)) {
    PopScreen();
    return;
  }
  if (games_.empty()) {
    if (input_.Pressed(Button::kX)) {
      controller_->audio().Play(SoundEvent::kSelect);
      controller_->ChooseLibraryFolder();
    } else if (input_.Pressed(Button::kY)) {
      controller_->audio().Play(SoundEvent::kSelect);
      controller_->RescanLibrary();
    }
    return;
  }

  const int columns = int(kLibraryColumns);
  const int count = int(games_.size());
  int index = int(std::min(library_index_, games_.size() - 1));
  const int old = index;
  const int h = input_.Horizontal();
  const int v = input_.Vertical();
  if (h) {
    index = std::clamp(index + h, 0, count - 1);
  }
  if (v) {
    const int next = index + v * columns;
    if (next >= 0 && next < count) {
      index = next;
    } else if (v > 0) {
      index = count - 1;
    }
  }
  if (input_.Pressed(Button::kRightBumper)) {
    index = std::min(count - 1, index + columns * 2);
  }
  if (input_.Pressed(Button::kLeftBumper)) {
    index = std::max(0, index - columns * 2);
  }
  if (index != old) {
    controller_->audio().Play(SoundEvent::kNavigate);
  }
  library_index_ = size_t(index);

  if (input_.Pressed(Button::kA)) {
    OpenGame(games_[library_index_]);
  } else if (input_.Pressed(Button::kX)) {
    controller_->audio().Play(SoundEvent::kSelect);
    details_game_ = games_[library_index_];
    PushScreen(Screen::kGameDetails);
  } else if (input_.Pressed(Button::kY)) {
    controller_->audio().Play(SoundEvent::kSelect);
    library_sort_ = (library_sort_ + 1) % 3;
    const auto selected = games_[library_index_];
    std::ranges::stable_sort(games_, [this](const auto& a, const auto& b) {
      switch (library_sort_) {
        case 1:
          return a->title_id < b->title_id;
        case 2:
          return a->source < b->source;
        default:
          return xe::utf8::lower_ascii(a->DisplayName()) <
                 xe::utf8::lower_ascii(b->DisplayName());
      }
    });
    library_index_ = size_t(std::distance(
        games_.begin(), std::ranges::find(games_, selected)));
  } else if (input_.Pressed(Button::kStart)) {
    controller_->RescanLibrary();
  }
  (void)library;
}

void DashboardDialog::DrawLibrary(Painter& p) {
  GameLibrary& library = controller_->library();
  const bool dark = on_dark_background();
  const ImU32 text = dark ? theme::kWhite : theme::kTextDark;
  const ImU32 sub = dark ? theme::kTextLightGray : theme::kTextGray;

  static constexpr const char* kSortNames[] = {"A-Z", "Title ID", "Type"};
  std::string subtitle;
  if (library.root().empty()) {
    subtitle = "No game folder selected";
  } else if (library.IsScanning()) {
    subtitle = fmt::format("Searching {} ... {} games, {} files checked",
                           xe::path_to_utf8(library.root()), games_.size(),
                           library.files_examined());
  } else {
    subtitle = fmt::format("{} games  |  sorted by {}  |  {}", games_.size(),
                           kSortNames[library_sort_],
                           xe::path_to_utf8(library.root()));
  }
  DrawScreenHeader(p, "game library", subtitle);

  if (games_.empty()) {
    const std::string message =
        library.root().empty()
            ? "Choose the folder where your games are stored. Disc images "
              "(.iso), extracted discs (default.xex), Games on Demand and "
              "Xbox LIVE Arcade packages are found automatically, including "
              "in sub-folders."
            : (library.IsScanning() ? "Looking for games..."
                                    : "No games were found in this folder.");
    p.Text(ImVec2(theme::kSafeX, 220.f), message, 22.f, text,
           DashboardFont::kRegular, TextAlign::kLeft, 820.f);
    p.ButtonPrompts(ImVec2(theme::kSafeX, 662.f),
                    {{'X', "Choose Folder"}, {'Y', "Search Again"},
                     {'B', "Back"}},
                    text);
    return;
  }

  const int columns = int(kLibraryColumns);
  const float gap = (1280.f - 2.f * theme::kSafeX -
                     kLibraryColumns * kLibraryTileW) /
                    (kLibraryColumns - 1.f);
  const int selected_row = int(library_index_) / columns;
  // Keep the selected row within the two visible rows.
  float target_scroll = library_scroll_;
  if (selected_row < int(target_scroll)) {
    target_scroll = float(selected_row);
  } else if (selected_row > int(target_scroll) + 1) {
    target_scroll = float(selected_row - 1);
  }
  library_scroll_ = theme::Approach(library_scroll_, std::round(target_scroll),
                                    delta_time_, theme::kTileSlideTime);

  ImDrawList* dl = p.draw_list();
  dl->PushClipRect(p.canvas().P(0.f, kLibraryTop - 20.f),
                   p.canvas().P(1280.f, 640.f), true);
  for (size_t i = 0; i < games_.size(); i++) {
    const int row = int(i) / columns;
    const int col = int(i) % columns;
    const float y = kLibraryTop + (float(row) - library_scroll_) * kLibraryRowH;
    if (y < kLibraryTop - kLibraryRowH || y > 660.f) {
      continue;
    }
    const GameEntry& game = *games_[i];
    const bool focused = i == library_index_;
    const float grow = focused ? 8.f : 0.f;
    const float x = theme::kSafeX + col * (kLibraryTileW + gap);
    const ImVec2 min(x - grow, y - grow);
    const ImVec2 max(x + kLibraryTileW + grow, y + kLibraryTileH + grow);

    p.Shadow(min, max, focused ? 12.f : 6.f, focused ? 0.55f : 0.3f, 4.f);
    const ImU32 base = TitleColor(game.title_id);
    p.VerticalGradient(min, max, theme::Lerp(base, theme::kWhite, 0.2f),
                       theme::Lerp(base, theme::kBlack, 0.3f));
    if (auto* cover = controller_->GetGameCover(game)) {
      p.Image(cover, min, max, true);
    } else if (auto* icon = controller_->GetGameIcon(game)) {
      const float pad = (max.x - min.x) * 0.2f;
      p.Image(icon, ImVec2(min.x + pad, min.y + pad * 0.8f),
              ImVec2(max.x - pad, max.y - pad * 1.2f));
    } else {
      p.Text(ImVec2((min.x + max.x) * 0.5f, min.y + 30.f),
             game.DisplayName().substr(0, 1), 110.f,
             theme::WithAlpha(theme::kWhite, 0.3f), DashboardFont::kLight,
             TextAlign::kCenter);
    }
    // Source ribbon.
    p.Rect(ImVec2(min.x, max.y - 5.f), max, SourceColor(game.source));
    if (focused) {
      p.RectOutline(min, max, theme::kWhite, 3.f, 2.f);
    }
    p.TextClipped(ImVec2(x, y + kLibraryTileH + 14.f), game.DisplayName(),
                  19.f, text, kLibraryTileW,
                  focused ? DashboardFont::kSemibold : DashboardFont::kRegular);
    p.TextClipped(ImVec2(x, y + kLibraryTileH + 38.f),
                  fmt::format("{}  {}", GameSourceName(game.source),
                              game.TitleIdString()),
                  14.f, sub, kLibraryTileW);
  }
  dl->PopClipRect();

  p.ButtonPrompts(ImVec2(theme::kSafeX, 662.f),
                  {{'A', "Play"},
                   {'X', "Game Details"},
                   {'Y', fmt::format("Sort: {}", kSortNames[library_sort_])},
                   {'B', "Back"}},
                  text);
}

// ---------------------------------------------------------------------------
// Game details
// ---------------------------------------------------------------------------

void DashboardDialog::UpdateGameDetails() {
  if (input_.Pressed(Button::kB) || !details_game_) {
    PopScreen();
    return;
  }
  if (input_.Pressed(Button::kA)) {
    OpenGame(details_game_);
  } else if (input_.Pressed(Button::kY)) {
    controller_->audio().Play(SoundEvent::kSelect);
    controller_->OpenTitleAchievements(0, details_game_->title_id);
  }
}

void DashboardDialog::DrawGameDetails(Painter& p) {
  if (!details_game_) {
    return;
  }
  const GameEntry& game = *details_game_;
  const bool dark = on_dark_background();
  const ImU32 text = dark ? theme::kWhite : theme::kTextDark;
  const ImU32 sub = dark ? theme::kTextLightGray : theme::kTextGray;
  DrawScreenHeader(p, "game details", "");

  const ImVec2 art_min(theme::kSafeX + 20.f, 150.f);
  const ImVec2 art_max(art_min.x + 300.f, art_min.y + 420.f);
  p.Shadow(art_min, art_max, 14.f, 0.5f, 6.f);
  const ImU32 base = TitleColor(game.title_id);
  p.VerticalGradient(art_min, art_max, theme::Lerp(base, theme::kWhite, 0.2f),
                     theme::Lerp(base, theme::kBlack, 0.35f));
  if (auto* cover = controller_->GetGameCover(game)) {
    p.Image(cover, art_min, art_max, true);
  } else if (auto* icon = controller_->GetGameIcon(game)) {
    p.Image(icon, ImVec2(art_min.x + 50.f, art_min.y + 110.f),
            ImVec2(art_max.x - 50.f, art_min.y + 310.f));
  }

  const float x = art_max.x + 48.f;
  const float w = 1280.f - theme::kSafeX - x;
  p.Text(ImVec2(x, 150.f), game.DisplayName(), 38.f, text,
         DashboardFont::kLight, TextAlign::kLeft, w);
  float y = 150.f + p.MeasureText(game.DisplayName(), 38.f,
                                  DashboardFont::kLight, w)
                        .y +
            20.f;
  auto row = [&](const std::string& label, const std::string& value) {
    p.Text(ImVec2(x, y), label, 18.f, sub);
    p.Text(ImVec2(x + 190.f, y), value, 18.f, text, DashboardFont::kRegular,
           TextAlign::kLeft, w - 190.f);
    y += 30.f;
  };
  row("Title ID", game.TitleIdString());
  row("Media ID", fmt::format("{:08X}", game.media_id));
  row("Version", FormatVersion(game.version));
  if (game.disc_count > 1) {
    row("Disc", fmt::format("{} of {}", game.disc_number, game.disc_count));
  }
  row("Type", GameSourceName(game.source));
  if (game.achievement_count) {
    row("Achievements",
        fmt::format("{} worth {} gamerscore", game.achievement_count,
                    game.total_gamerscore));
  }
  row("Size", FormatSize(game.file_size));
  y += 10.f;
  p.Text(ImVec2(x, y), xe::path_to_utf8(game.launch_path), 15.f, sub,
         DashboardFont::kRegular, TextAlign::kLeft, w);

  p.ButtonPrompts(ImVec2(theme::kSafeX, 662.f),
                  {{'A', "Play"}, {'Y', "Achievements"}, {'B', "Back"}},
                  text);
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

void DashboardDialog::UpdateProfiles() {
  if (input_.Pressed(Button::kB)) {
    PopScreen();
    return;
  }
  auto* profile_manager =
      controller_->emulator()->kernel_state()->xam_state()->profile_manager();
  const auto* accounts = profile_manager->GetAccounts();
  if (input_.Pressed(Button::kY)) {
    controller_->audio().Play(SoundEvent::kSelect);
    controller_->OpenCreateProfile();
    return;
  }
  if (accounts->empty()) {
    return;
  }
  const int v = input_.Vertical();
  const int count = int(accounts->size());
  const int next = std::clamp(int(profile_index_) + v, 0, count - 1);
  if (next != int(profile_index_)) {
    profile_index_ = size_t(next);
    controller_->audio().Play(SoundEvent::kNavigate);
  }
  profile_index_ = std::min(profile_index_, size_t(count - 1));
  auto it = accounts->begin();
  std::advance(it, profile_index_);
  const uint64_t xuid = it->first;
  const uint8_t slot = profile_manager->GetUserIndexAssignedToProfile(xuid);

  if (input_.Pressed(Button::kA)) {
    controller_->audio().Play(SoundEvent::kSelect);
    if (slot == XUserIndexAny) {
      controller_->SignIn(xuid, 0);
    } else {
      controller_->SignOut(slot);
    }
  } else if (input_.Pressed(Button::kX)) {
    controller_->audio().Play(SoundEvent::kSelect);
    controller_->OpenGamercardEditor(xuid);
  }
}

void DashboardDialog::DrawProfiles(Painter& p) {
  const bool dark = on_dark_background();
  const ImU32 text = dark ? theme::kWhite : theme::kTextDark;
  const ImU32 sub = dark ? theme::kTextLightGray : theme::kTextGray;
  DrawScreenHeader(p, "profiles", "Profiles stored on this console");

  auto* profile_manager =
      controller_->emulator()->kernel_state()->xam_state()->profile_manager();
  const auto* accounts = profile_manager->GetAccounts();
  if (accounts->empty()) {
    p.Text(ImVec2(theme::kSafeX, 200.f),
           "There are no profiles on this console yet. Create one to save "
           "games and earn achievements.",
           22.f, text, DashboardFont::kRegular, TextAlign::kLeft, 800.f);
  }

  float y = 160.f;
  size_t i = 0;
  for (const auto& [xuid, account] : *accounts) {
    const bool focused = i == profile_index_;
    const uint8_t slot = profile_manager->GetUserIndexAssignedToProfile(xuid);
    const ImVec2 min(theme::kSafeX, y);
    const ImVec2 max(theme::kSafeX + 640.f, y + 76.f);
    if (focused) {
      p.Shadow(min, max, 6.f, 0.35f, 4.f);
      p.VerticalGradient(min, max, theme::kWhite, theme::Rgb(0xE6EAE2));
    }
    const ImU32 row_text = focused ? theme::kTextDark : text;
    const ImU32 row_sub = focused ? theme::kTextGray : sub;
    p.Rect(ImVec2(min.x + 10.f, y + 8.f), ImVec2(min.x + 70.f, y + 68.f),
           theme::Rgb(0x4B5A44), 4.f);
    if (slot != XUserIndexAny) {
      const ProfileCard card = controller_->GetProfileCard(slot);
      if (card.gamerpic) {
        p.Image(card.gamerpic, ImVec2(min.x + 10.f, y + 8.f),
                ImVec2(min.x + 70.f, y + 68.f), true, IM_COL32_WHITE, 4.f);
      }
    }
    p.Text(ImVec2(min.x + 86.f, y + 10.f), account.GetGamertagString(), 24.f,
           row_text, DashboardFont::kSemibold);
    p.Text(ImVec2(min.x + 86.f, y + 42.f),
           slot == XUserIndexAny
               ? "Not signed in"
               : fmt::format("Signed in  |  Controller {}", slot + 1),
           16.f, row_sub);
    y += 86.f;
    i++;
  }

  const bool selected_signed_in = [&]() {
    if (accounts->empty()) {
      return false;
    }
    auto it = accounts->begin();
    std::advance(it, std::min(profile_index_, accounts->size() - 1));
    return profile_manager->GetUserIndexAssignedToProfile(it->first) !=
           XUserIndexAny;
  }();
  std::vector<std::pair<char, std::string>> prompts;
  if (!accounts->empty()) {
    prompts.push_back({'A', selected_signed_in ? "Sign Out" : "Sign In"});
    prompts.push_back({'X', "Edit Gamercard"});
  }
  prompts.push_back({'Y', "Create Profile"});
  prompts.push_back({'B', "Back"});
  p.ButtonPrompts(ImVec2(theme::kSafeX, 662.f), prompts, text);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

std::vector<DashboardDialog::SettingRow> DashboardDialog::BuildSettings() {
  DashboardController* c = controller_;
  std::vector<SettingRow> rows;
  rows.push_back(
      {"Dashboard style",
       c->style() == DashboardStyle::kNxe ? "New Xbox Experience (2008)"
                                          : "Blades (2005)",
       [c](int) {
         c->SetStyle(c->style() == DashboardStyle::kNxe
                         ? DashboardStyle::kBlades
                         : DashboardStyle::kNxe);
       },
       nullptr});
  rows.back().on_activate = [f = rows.back().on_change]() { f(1); };
  rows.push_back({"Interface sounds", c->audio().enabled() ? "On" : "Off",
                  [c](int) { c->SetSoundsEnabled(!c->audio().enabled()); },
                  nullptr});
  rows.back().on_activate = [f = rows.back().on_change]() { f(1); };
  rows.push_back({"Game library folder",
                  c->library().root().empty()
                      ? "Not set"
                      : xe::path_to_utf8(c->library().root()),
                  nullptr, [c]() { c->ChooseLibraryFolder(); }});
  rows.push_back({"Search for new games", "", nullptr,
                  [c]() { c->RescanLibrary(); }});
  rows.push_back({"Controller vibration",
                  c->IsVibrationEnabled() ? "On" : "Off",
                  [c](int) { c->ToggleControllerVibration(); },
                  [c]() { c->ToggleControllerVibration(); }});
  rows.push_back({"Console settings", "", nullptr,
                  [c]() { c->OpenConsoleSettings(); }});
  rows.push_back({"Display", "", nullptr, [c]() { c->OpenDisplaySettings(); }});
  rows.push_back({"Memory", "", nullptr, [c]() { c->OpenInstalledContent(); }});
  rows.push_back({"Profiles", "", nullptr,
                  [this]() { PushScreen(Screen::kProfiles); }});
  rows.push_back({"Full screen", "", nullptr, [c]() { c->ToggleFullscreen(); }});
  return rows;
}

void DashboardDialog::UpdateSettings() {
  if (input_.Pressed(Button::kB)) {
    PopScreen();
    return;
  }
  auto rows = BuildSettings();
  const int v = input_.Vertical();
  const int next =
      std::clamp(int(setting_index_) + v, 0, int(rows.size()) - 1);
  if (next != int(setting_index_)) {
    setting_index_ = size_t(next);
    controller_->audio().Play(SoundEvent::kNavigate);
  }
  SettingRow& row = rows[setting_index_];
  const int h = input_.Horizontal();
  if (h && row.on_change) {
    controller_->audio().Play(SoundEvent::kNavigate);
    row.on_change(h);
  }
  if (input_.Pressed(Button::kA) && row.on_activate) {
    controller_->audio().Play(SoundEvent::kSelect);
    row.on_activate();
  }
}

void DashboardDialog::DrawSettings(Painter& p) {
  const bool dark = on_dark_background();
  const ImU32 text = dark ? theme::kWhite : theme::kTextDark;
  const ImU32 sub = dark ? theme::kTextLightGray : theme::kTextGray;
  DrawScreenHeader(p, "system settings", "");

  const auto rows = BuildSettings();
  float y = 150.f;
  for (size_t i = 0; i < rows.size(); i++) {
    const bool focused = i == setting_index_;
    const ImVec2 min(theme::kSafeX, y);
    const ImVec2 max(1280.f - theme::kSafeX, y + 42.f);
    if (focused) {
      p.Shadow(min, max, 4.f, 0.3f, 4.f);
      p.VerticalGradient(min, max, theme::kWhite, theme::Rgb(0xE6EAE2));
    }
    p.Text(ImVec2(min.x + 16.f, y + 8.f), rows[i].label, 21.f,
           focused ? theme::kTextDark : text,
           focused ? DashboardFont::kSemibold : DashboardFont::kRegular);
    if (!rows[i].value.empty()) {
      const std::string value =
          rows[i].on_change ? fmt::format("<  {}  >", rows[i].value)
                            : rows[i].value;
      p.TextClipped(ImVec2(max.x - 16.f, y + 10.f), value, 18.f,
                    focused ? theme::kTextGray : sub, 560.f,
                    DashboardFont::kRegular, TextAlign::kRight);
    }
    y += 48.f;
  }
  p.ButtonPrompts(ImVec2(theme::kSafeX, 662.f),
                  {{'A', "Select"}, {'B', "Back"}}, text);
}

// ---------------------------------------------------------------------------
// Marketplace (local content)
// ---------------------------------------------------------------------------

void DashboardDialog::UpdateMarketplace() {
  if (input_.Pressed(Button::kB)) {
    PopScreen();
    return;
  }
  constexpr int kRows = 2;
  const int v = input_.Vertical();
  const int next = std::clamp(int(market_index_) + v, 0, kRows - 1);
  if (next != int(market_index_)) {
    market_index_ = size_t(next);
    controller_->audio().Play(SoundEvent::kNavigate);
  }
  if (input_.Pressed(Button::kA)) {
    controller_->audio().Play(SoundEvent::kSelect);
    if (market_index_ == 0) {
      controller_->OpenInstalledContent();
    } else {
      controller_->InstallContentPackage();
    }
  }
}

void DashboardDialog::DrawMarketplace(Painter& p) {
  const bool dark = on_dark_background();
  const ImU32 text = dark ? theme::kWhite : theme::kTextDark;
  const ImU32 sub = dark ? theme::kTextLightGray : theme::kTextGray;
  DrawScreenHeader(p, "game marketplace", "Content on this console");

  static constexpr std::pair<const char*, const char*> kRows[] = {
      {"Installed Add-ons and Updates",
       "Browse downloadable content, title updates and arcade titles already "
       "installed for your games."},
      {"Install From File",
       "Install a content package (DLC, title update or arcade game) you "
       "already own."},
  };
  float y = 160.f;
  for (size_t i = 0; i < std::size(kRows); i++) {
    const bool focused = i == market_index_;
    const ImVec2 min(theme::kSafeX, y);
    const ImVec2 max(theme::kSafeX + 700.f, y + 96.f);
    p.Shadow(min, max, focused ? 10.f : 4.f, focused ? 0.45f : 0.2f, 4.f);
    p.VerticalGradient(min, max,
                       focused ? theme::kWhite : theme::Rgb(0xF0F2EE, 200),
                       focused ? theme::Rgb(0xE6EAE2)
                               : theme::Rgb(0xDCE0D8, 200));
    p.Rect(min, ImVec2(min.x + 8.f, max.y), theme::kBladeMarketplace);
    p.Text(ImVec2(min.x + 28.f, y + 14.f), kRows[i].first, 24.f,
           theme::kTextDark, DashboardFont::kSemibold);
    p.Text(ImVec2(min.x + 28.f, y + 48.f), kRows[i].second, 16.f,
           theme::kTextGray, DashboardFont::kRegular, TextAlign::kLeft,
           650.f);
    y += 116.f;
  }
  p.Text(ImVec2(theme::kSafeX, y + 10.f),
         "The online Marketplace catalog requires Xbox LIVE servers, which are "
         "not available. Content you install appears in your games "
         "automatically.",
         17.f, sub, DashboardFont::kRegular, TextAlign::kLeft, 700.f);
  p.ButtonPrompts(ImVec2(theme::kSafeX, 662.f),
                  {{'A', "Select"}, {'B', "Back"}}, text);
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
