/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_DASHBOARD_THEME_H_
#define XENIA_APP_DASHBOARD_DASHBOARD_THEME_H_

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "third_party/imgui/imgui.h"

namespace xe {
namespace app {
namespace dashboard {

// All layout is authored against the console's 1280x720 output and scaled
// uniformly to the window, the same way the console scaled to 1080p.
// Colors, metrics and timings are hand-matched recreations, not values taken
// from Microsoft's dashboard resources.
namespace theme {

constexpr float kCanvasWidth = 1280.f;
constexpr float kCanvasHeight = 720.f;
// Title-safe area: content stays inside the inner 90% like console UI.
constexpr float kSafeX = 64.f;
constexpr float kSafeY = 36.f;

constexpr ImU32 Rgb(uint32_t rgb, uint8_t alpha = 255) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

inline ImU32 WithAlpha(ImU32 color, float alpha) {
  const uint32_t a = uint32_t(
      std::clamp(alpha, 0.f, 1.f) * float((color >> IM_COL32_A_SHIFT) & 0xFF));
  return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

inline ImU32 Lerp(ImU32 a, ImU32 b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  auto ch = [&](int shift) {
    const float x = float((a >> shift) & 0xFF);
    const float y = float((b >> shift) & 0xFF);
    return uint32_t(x + (y - x) * t + 0.5f) << shift;
  };
  return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) |
         ch(IM_COL32_A_SHIFT);
}

// Core palette.
constexpr ImU32 kXboxGreen = Rgb(0x5CB01E);
constexpr ImU32 kXboxGreenLight = Rgb(0x8FD43A);
constexpr ImU32 kXboxGreenDark = Rgb(0x2E6A0C);
constexpr ImU32 kWhite = Rgb(0xFFFFFF);
constexpr ImU32 kTextDark = Rgb(0x1E1E1E);
constexpr ImU32 kTextGray = Rgb(0x6E6E6E);
constexpr ImU32 kTextLightGray = Rgb(0xC8C8C8);
constexpr ImU32 kBlack = Rgb(0x000000);

// Controller face button colors used by on-screen button prompts.
constexpr ImU32 kButtonA = Rgb(0x6DB72C);
constexpr ImU32 kButtonB = Rgb(0xD5302B);
constexpr ImU32 kButtonX = Rgb(0x2B6FD5);
constexpr ImU32 kButtonY = Rgb(0xF0B321);

// NXE (2008) default theme: light background with a soft vignette.
constexpr ImU32 kNxeBackgroundTop = Rgb(0xF4F6F2);
constexpr ImU32 kNxeBackgroundBottom = Rgb(0xC9D0C4);
constexpr ImU32 kNxeTileShadow = Rgb(0x000000, 60);
constexpr ImU32 kNxeChannelInactive = Rgb(0x8A8F86);

// Per-channel accent colors for the NXE channel bar.
constexpr ImU32 kNxeAccentSpotlight = Rgb(0x5CB01E);
constexpr ImU32 kNxeAccentGames = Rgb(0x3E9A1A);
constexpr ImU32 kNxeAccentVideo = Rgb(0x7A3FA0);
constexpr ImU32 kNxeAccentMusic = Rgb(0xE07A12);
constexpr ImU32 kNxeAccentFriends = Rgb(0x1C7BC9);
constexpr ImU32 kNxeAccentMyXbox = Rgb(0x4B9A2F);

// Blades (2005) palette, one color per blade.
constexpr ImU32 kBladeMarketplace = Rgb(0xE3871E);
constexpr ImU32 kBladeLive = Rgb(0x5CB01E);
constexpr ImU32 kBladeGames = Rgb(0x8DC63F);
constexpr ImU32 kBladeMedia = Rgb(0x2C83C6);
constexpr ImU32 kBladeSystem = Rgb(0x9EA3A8);
constexpr ImU32 kBladeBackdrop = Rgb(0x101410);

// Guide overlay.
constexpr ImU32 kGuidePanel = Rgb(0x151A15, 246);
constexpr ImU32 kGuidePanelEdge = Rgb(0x5CB01E);
constexpr ImU32 kGuideDim = Rgb(0x000000, 110);
constexpr ImU32 kGuideHighlight = Rgb(0x5CB01E);

// Timings, in seconds.
constexpr float kBladeSlideTime = 0.26f;
constexpr float kChannelSlideTime = 0.30f;
constexpr float kTileSlideTime = 0.22f;
constexpr float kGuideSlideTime = 0.20f;
constexpr float kFadeTime = 0.18f;

// Exponential smoothing rate that settles in roughly `settle_time` seconds,
// frame-rate independent so animation is identical at 60 Hz and above.
inline float ApproachRate(float settle_time) { return 4.6f / settle_time; }

inline float Approach(float current, float target, float dt,
                      float settle_time) {
  const float k = 1.f - std::exp(-ApproachRate(settle_time) * dt);
  const float next = current + (target - current) * k;
  return std::abs(target - next) < 0.0005f ? target : next;
}

inline float EaseOutCubic(float t) {
  t = std::clamp(t, 0.f, 1.f);
  const float inv = 1.f - t;
  return 1.f - inv * inv * inv;
}

}  // namespace theme

// Maps the 1280x720 authoring canvas onto the display. The background may
// extend beyond the canvas on non-16:9 windows.
struct Canvas {
  ImVec2 display_size;
  ImVec2 origin;
  float scale = 1.f;

  static Canvas FromDisplay(ImVec2 display) {
    Canvas c;
    c.display_size = display;
    c.scale = std::min(display.x / theme::kCanvasWidth,
                       display.y / theme::kCanvasHeight);
    c.origin = ImVec2((display.x - theme::kCanvasWidth * c.scale) * 0.5f,
                      (display.y - theme::kCanvasHeight * c.scale) * 0.5f);
    return c;
  }

  ImVec2 P(float x, float y) const {
    return ImVec2(origin.x + x * scale, origin.y + y * scale);
  }
  ImVec2 P(ImVec2 v) const { return P(v.x, v.y); }
  float S(float v) const { return v * scale; }
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_THEME_H_
