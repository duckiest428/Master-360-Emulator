/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_DASHBOARD_PAINTER_H_
#define XENIA_APP_DASHBOARD_DASHBOARD_PAINTER_H_

#include <string_view>
#include <utility>
#include <vector>

#include "third_party/imgui/imgui.h"
#include "xenia/app/dashboard/dashboard_theme.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace ui {
class ImmediateTexture;
}  // namespace ui
}  // namespace xe

namespace xe {
namespace app {
namespace dashboard {

enum class TextAlign : uint8_t { kLeft, kCenter, kRight };

// Thin drawing layer over an ImDrawList that works in 1280x720 canvas
// coordinates and uses the dashboard fonts.
class Painter {
 public:
  Painter(ImDrawList* draw_list, ui::ImGuiDrawer* imgui_drawer,
          const Canvas& canvas)
      : dl_(draw_list), imgui_drawer_(imgui_drawer), canvas_(canvas) {}

  ImDrawList* draw_list() const { return dl_; }
  const Canvas& canvas() const { return canvas_; }

  // Fills the whole display (not just the canvas) with a vertical gradient.
  void FillBackground(ImU32 top, ImU32 bottom);

  void Rect(ImVec2 min, ImVec2 max, ImU32 color, float rounding = 0.f);
  void RectOutline(ImVec2 min, ImVec2 max, ImU32 color, float thickness,
                   float rounding = 0.f);
  void VerticalGradient(ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom);
  void HorizontalGradient(ImVec2 min, ImVec2 max, ImU32 left, ImU32 right);
  // Soft drop shadow made of stacked translucent rounded rects.
  void Shadow(ImVec2 min, ImVec2 max, float spread, float strength,
              float rounding = 4.f);
  void Circle(ImVec2 center, float radius, ImU32 color, int segments = 0);
  void SoftGlow(ImVec2 center, float radius, ImU32 color);

  // Arbitrary quad (clockwise from top-left), used for perspective tiles.
  void Quad(const ImVec2 corners[4], ImU32 color);
  void QuadGradient(const ImVec2 corners[4], ImU32 top, ImU32 bottom);
  void ImageQuad(ui::ImmediateTexture* texture, const ImVec2 corners[4],
                 ImU32 tint = IM_COL32_WHITE);
  // Image fitted inside (contain) or cropped to (cover) the rect.
  void Image(ui::ImmediateTexture* texture, ImVec2 min, ImVec2 max,
             bool cover = false, ImU32 tint = IM_COL32_WHITE,
             float rounding = 0.f);

  float Text(ImVec2 pos, std::string_view text, float size, ImU32 color,
             ui::DashboardFont weight = ui::DashboardFont::kRegular,
             TextAlign align = TextAlign::kLeft, float wrap_width = 0.f);
  // Draws text that fits `max_width`, truncating with an ellipsis.
  void TextClipped(ImVec2 pos, std::string_view text, float size,
                   ImU32 color, float max_width,
                   ui::DashboardFont weight = ui::DashboardFont::kRegular,
                   TextAlign align = TextAlign::kLeft);
  // Text rotated 90 degrees counter-clockwise, reading bottom to top, with
  // its baseline-start at `pos` (used for blade tabs).
  void TextVertical(ImVec2 pos, std::string_view text, float size,
                    ImU32 color,
                    ui::DashboardFont weight = ui::DashboardFont::kRegular);
  ImVec2 MeasureText(std::string_view text, float size,
                     ui::DashboardFont weight = ui::DashboardFont::kRegular,
                     float wrap_width = 0.f);

  // Controller face button glyph ("A", "B", "X", "Y") with a label.
  // Returns the canvas width consumed.
  float ButtonPrompt(ImVec2 pos, char button, std::string_view label,
                     ImU32 label_color, float size = 18.f);
  // Row of prompts laid out left to right.
  void ButtonPrompts(ImVec2 pos,
                     const std::vector<std::pair<char, std::string>>& prompts,
                     ImU32 label_color, float size = 18.f);

  // Draws the "G" gamerscore badge followed by the value.
  float Gamerscore(ImVec2 pos, uint32_t value, float size, ImU32 color);

 private:
  ImFont* Font(ui::DashboardFont weight, float canvas_size);

  ImDrawList* dl_;
  ui::ImGuiDrawer* imgui_drawer_;
  Canvas canvas_;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_PAINTER_H_
