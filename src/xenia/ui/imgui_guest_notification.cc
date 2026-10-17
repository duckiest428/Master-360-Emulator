/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <cmath>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/imgui_notification.h"

#if XE_PLATFORM_WIN32
#include <playsoundapi.h>
#endif

DEFINE_string(notification_sound_path, "",
              "Path (including filename) to selected notification sound. Sound "
              "MUST be in wav format!",
              "General");

namespace xe {
namespace ui {

ImGuiGuestNotification::ImGuiGuestNotification(ui::ImGuiDrawer* imgui_drawer,
                                               std::string& title,
                                               std::string& description,
                                               uint8_t user_index,
                                               uint8_t position_id)
    : ImGuiNotification(imgui_drawer, NotificationType::Guest, title,
                        description, user_index, position_id),
      current_stage_(NotificationStage::Awaiting),
      notification_draw_progress_(0.0f) {
  imgui_drawer->AddNotification(this);
}

ImGuiGuestNotification::~ImGuiGuestNotification() {}

void ImGuiGuestNotification::UpdateNotificationState() {
  switch (current_stage_) {
    case NotificationStage::Awaiting:
      // TODO(Gliniak): Implement delayed notifications.
      current_stage_ = NotificationStage::FazeIn;
      notification_draw_progress_ = 0.2f;
#if XE_PLATFORM_WIN32
      if (!cvars::notification_sound_path.empty()) {
        auto notification_sound_path = cvars::notification_sound_path;
        if (std::filesystem::exists(notification_sound_path)) {
          PlaySound(std::wstring(notification_sound_path.begin(),
                                 notification_sound_path.end())
                        .c_str(),
                    NULL,
                    SND_FILENAME | SND_NODEFAULT | SND_NOSTOP | SND_ASYNC);
        }
      }
#endif

      break;
    case NotificationStage::FazeIn: {
      SetCreationTime(Clock::QueryHostUptimeMillis());
      if (notification_draw_progress_ < 1.1f) {
        notification_draw_progress_ += 0.02f;
      }

      // Mimics a bit original console behaviour when it makes window a bit
      // longer for few frames then decreases size
      if (notification_draw_progress_ >= 1.1f) {
        current_stage_ = NotificationStage::Present;
        notification_draw_progress_ = 1.0f;
      }
      break;
    }
    case NotificationStage::Present:
      if (IsNotificationClosingTime()) {
        current_stage_ = NotificationStage::FazeOut;
      }
      break;
    case NotificationStage::FazeOut: {
      if (notification_draw_progress_ > 0.2f) {
        notification_draw_progress_ -= 0.02f;
      } else {
        current_stage_ = NotificationStage::Finished;
      }
      break;
    }
    default:
      break;
  }
}

const ImVec2 ImGuiGuestNotification::CalculateNotificationSize(ImVec2 text_size,
                                                               float scale) {
  const ImVec2 result = ImVec2(floorf((default_notification_icon_size.x +
                                       default_notification_margin_size.x) *
                                      scale) +
                                   text_size.x,
                               floorf((default_notification_icon_size.y +
                                       default_notification_margin_size.y) *
                                      scale));

  return result;
}

namespace {

float EaseOutCubic(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}

float EaseInCubic(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * t;
}

float Phase(float now, float begin, float duration) {
  return std::clamp((now - begin) / duration, 0.f, 1.f);
}

ImU32 Gray(int value, float alpha) {
  return IM_COL32(value, value, value, int(std::clamp(alpha, 0.f, 1.f) * 255));
}

}  // namespace

bool ImGuiGuestNotification::DrawClassicToast(ImGuiIO& io,
                                              std::string_view line1,
                                              std::string_view line2) {
  const uint64_t now_ms = Clock::QueryHostUptimeMillis();
  if (!toast_start_ms_) {
    toast_start_ms_ = now_ms;
#if XE_PLATFORM_WIN32
    if (!cvars::notification_sound_path.empty()) {
      auto notification_sound_path = cvars::notification_sound_path;
      if (std::filesystem::exists(notification_sound_path)) {
        PlaySound(std::wstring(notification_sound_path.begin(),
                               notification_sound_path.end())
                      .c_str(),
                  NULL, SND_FILENAME | SND_NODEFAULT | SND_NOSTOP | SND_ASYNC);
      }
    }
#endif
  }
  // Timeline in milliseconds, matching the console.
  constexpr float kOrbIn = 0.f, kOrbSettle = 180.f, kOpen = 350.f,
                  kTextIn = 670.f, kHold = 3200.f;
  constexpr float kTextOut = kTextIn + 185.f + kHold, kClose = kTextOut + 335.f,
                  kOrbOut = kClose + 320.f, kEnd = kOrbOut + 210.f;
  const float t = float(now_ms - toast_start_ms_);
  if (t >= kEnd) {
    return false;
  }

  const ImVec2 screen = io.DisplaySize;
  const float scale = std::fminf(screen.x / default_drawing_resolution.x,
                                 screen.y / default_drawing_resolution.y);
  const float height = 58.f * scale;
  const float font_size = 19.f * scale;
  ImFont* font =
      GetDrawer()->GetDashboardFont(DashboardFont::kRegular, font_size);
  const bool two_lines = !line2.empty();
  const float line_width = std::max(
      font->CalcTextSizeA(font_size, FLT_MAX, 0.f, line1.data(),
                          line1.data() + line1.size())
          .x,
      two_lines ? font->CalcTextSizeA(font_size, FLT_MAX, 0.f, line2.data(),
                                      line2.data() + line2.size())
                      .x
                : 0.f);
  const float full_width = height + 12.f * scale + line_width + 26.f * scale;

  const ImVec2 position = CalculateNotificationScreenPosition(
      screen, ImVec2(full_width, height), GetPositionId());
  if (std::isnan(position.x) || std::isnan(position.y)) {
    return true;
  }
  const float center_x = position.x + full_width * 0.5f;
  const float center_y = position.y + height * 0.5f;

  // Capsule width: grows out of the orb and folds back into it.
  const float open = EaseOutCubic(Phase(t, kOpen, 265.f)) *
                     (1.f - EaseInCubic(Phase(t, kClose, 320.f)));
  const float width = height + (full_width - height) * open;
  const float left = center_x - width * 0.5f;
  const float right = left + width;

  ImDrawList* draw = ImGui::GetForegroundDrawList();
  if (open > 0.f) {
    const float rounding = height * 0.5f;
    const float rim = std::max(1.f, 2.f * scale);
    const float alpha = std::min(1.f, open * 4.f);
    // Soft shadow, light outer rim, dark glassy body.
    draw->AddRectFilled(ImVec2(left - rim, center_y - height * 0.5f + rim),
                        ImVec2(right + rim, center_y + height * 0.5f + rim * 2),
                        Gray(0, 0.35f * alpha), rounding + rim);
    draw->AddRectFilled(ImVec2(left - rim, center_y - height * 0.5f - rim),
                        ImVec2(right + rim, center_y + height * 0.5f + rim),
                        Gray(172, alpha), rounding + rim);
    draw->AddRectFilled(ImVec2(left, center_y - height * 0.5f),
                        ImVec2(right, center_y + height * 0.5f),
                        Gray(14, alpha), rounding);
    draw->AddRectFilled(
        ImVec2(left + rounding * 0.35f, center_y - height * 0.5f + rim),
        ImVec2(right - rounding * 0.35f, center_y - height * 0.04f),
        Gray(255, 0.07f * alpha), rounding * 0.9f, ImDrawFlags_RoundCornersTop);
    draw->AddRect(ImVec2(left + rim, center_y - height * 0.5f + rim),
                  ImVec2(right - rim, center_y + height * 0.5f - rim),
                  Gray(70, 0.8f * alpha), rounding - rim, 0, 1.f);

    const float text_alpha =
        Phase(t, kTextIn, 185.f) * (1.f - Phase(t, kTextOut, 185.f));
    if (text_alpha > 0.f) {
      const float text_x = left + height + 12.f * scale;
      const float block = two_lines ? font_size * 2.1f : font_size;
      float y = center_y - block * 0.5f - 1.f * scale;
      draw->PushClipRect(ImVec2(left, center_y - height * 0.5f),
                         ImVec2(right - 8.f * scale, center_y + height * 0.5f),
                         true);
      draw->AddText(font, font_size, ImVec2(text_x, y), Gray(248, text_alpha),
                    line1.data(), line1.data() + line1.size());
      if (two_lines) {
        y += font_size * 1.1f;
        draw->AddText(font, font_size, ImVec2(text_x, y), Gray(248, text_alpha),
                      line2.data(), line2.data() + line2.size());
      }
      draw->PopClipRect();
    }
  }

  // The orb pops in slightly oversized, settles, and shrinks away at the end.
  float orb_scale =
      0.56f + (1.04f - 0.56f) * EaseOutCubic(Phase(t, kOrbIn, 180.f));
  orb_scale += (1.f - 1.04f) * EaseOutCubic(Phase(t, kOrbSettle, 115.f));
  orb_scale += (0.52f - 1.f) * EaseInCubic(Phase(t, kOrbOut, 210.f));
  const float orb_alpha =
      Phase(t, kOrbIn, 150.f) * (1.f - Phase(t, kOrbOut, 210.f));
  const float orb_size = (height + 10.f * scale) * orb_scale;
  const ImVec2 orb_center(left + height * 0.5f, center_y);
  if (ImmediateTexture* icon = GetDrawer()->GetToastIcon(GetUserIndex())) {
    draw->AddImage(
        reinterpret_cast<ImTextureID>(icon),
        ImVec2(orb_center.x - orb_size * 0.5f, orb_center.y - orb_size * 0.5f),
        ImVec2(orb_center.x + orb_size * 0.5f, orb_center.y + orb_size * 0.5f),
        ImVec2(0, 0), ImVec2(1, 1), Gray(255, orb_alpha));
  }
  return true;
}

void AchievementNotificationWindow::OnDraw(ImGuiIO& io) {
  if (!DrawClassicToast(io, GetTitle(), GetDescription())) {
    delete this;
  }
}

void XNotifyWindow::OnDraw(ImGuiIO& io) {
  if (!DrawClassicToast(io, GetDescription(), {})) {
    delete this;
  }
}

}  // namespace ui
}  // namespace xe
