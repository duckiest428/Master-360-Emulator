/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/dashboard_painter.h"

#include <cfloat>
#include <string>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/imgui/imgui_internal.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace app {
namespace dashboard {

namespace {

ImTextureID ToTextureId(ui::ImmediateTexture* texture) {
  return reinterpret_cast<ImTextureID>(texture);
}

}  // namespace

ImFont* Painter::Font(ui::DashboardFont weight, float canvas_size) {
  return imgui_drawer_->GetDashboardFont(weight, canvas_.S(canvas_size));
}

void Painter::FillBackground(ImU32 top, ImU32 bottom) {
  dl_->AddRectFilledMultiColor(ImVec2(0, 0), canvas_.display_size, top, top,
                               bottom, bottom);
}

void Painter::Rect(ImVec2 min, ImVec2 max, ImU32 color, float rounding) {
  dl_->AddRectFilled(canvas_.P(min), canvas_.P(max), color,
                     canvas_.S(rounding));
}

void Painter::RectOutline(ImVec2 min, ImVec2 max, ImU32 color,
                          float thickness, float rounding) {
  dl_->AddRect(canvas_.P(min), canvas_.P(max), color, canvas_.S(rounding), 0,
               std::max(1.f, canvas_.S(thickness)));
}

void Painter::VerticalGradient(ImVec2 min, ImVec2 max, ImU32 top,
                               ImU32 bottom) {
  dl_->AddRectFilledMultiColor(canvas_.P(min), canvas_.P(max), top, top,
                               bottom, bottom);
}

void Painter::HorizontalGradient(ImVec2 min, ImVec2 max, ImU32 left,
                                 ImU32 right) {
  dl_->AddRectFilledMultiColor(canvas_.P(min), canvas_.P(max), left, right,
                               right, left);
}

void Painter::Shadow(ImVec2 min, ImVec2 max, float spread, float strength,
                     float rounding) {
  constexpr int kSteps = 6;
  for (int i = kSteps; i >= 1; i--) {
    const float grow = spread * float(i) / kSteps;
    const float alpha = strength * (1.f - float(i - 1) / kSteps) / kSteps;
    dl_->AddRectFilled(canvas_.P(min.x - grow, min.y - grow * 0.5f),
                       canvas_.P(max.x + grow, max.y + grow * 1.5f),
                       theme::WithAlpha(theme::kBlack, alpha),
                       canvas_.S(rounding + grow));
  }
}

void Painter::Circle(ImVec2 center, float radius, ImU32 color,
                     int segments) {
  dl_->AddCircleFilled(canvas_.P(center), canvas_.S(radius), color, segments);
}

void Painter::SoftGlow(ImVec2 center, float radius, ImU32 color) {
  constexpr int kRings = 10;
  for (int i = kRings; i >= 1; i--) {
    const float r = radius * float(i) / kRings;
    dl_->AddCircleFilled(canvas_.P(center), canvas_.S(r),
                         theme::WithAlpha(color, 1.f / kRings), 48);
  }
}

void Painter::Quad(const ImVec2 corners[4], ImU32 color) {
  dl_->AddQuadFilled(canvas_.P(corners[0]), canvas_.P(corners[1]),
                     canvas_.P(corners[2]), canvas_.P(corners[3]), color);
}

void Painter::QuadGradient(const ImVec2 corners[4], ImU32 top,
                           ImU32 bottom) {
  const ImVec2 uv = dl_->_Data->TexUvWhitePixel;
  dl_->PrimReserve(6, 4);
  const ImDrawIdx base = ImDrawIdx(dl_->_VtxCurrentIdx);
  dl_->PrimWriteIdx(base);
  dl_->PrimWriteIdx(ImDrawIdx(base + 1));
  dl_->PrimWriteIdx(ImDrawIdx(base + 2));
  dl_->PrimWriteIdx(base);
  dl_->PrimWriteIdx(ImDrawIdx(base + 2));
  dl_->PrimWriteIdx(ImDrawIdx(base + 3));
  dl_->PrimWriteVtx(canvas_.P(corners[0]), uv, top);
  dl_->PrimWriteVtx(canvas_.P(corners[1]), uv, top);
  dl_->PrimWriteVtx(canvas_.P(corners[2]), uv, bottom);
  dl_->PrimWriteVtx(canvas_.P(corners[3]), uv, bottom);
}

void Painter::ImageQuad(ui::ImmediateTexture* texture, const ImVec2 corners[4],
                        ImU32 tint) {
  if (!texture) {
    return;
  }
  dl_->AddImageQuad(ToTextureId(texture), canvas_.P(corners[0]),
                    canvas_.P(corners[1]), canvas_.P(corners[2]),
                    canvas_.P(corners[3]), ImVec2(0, 0), ImVec2(1, 0),
                    ImVec2(1, 1), ImVec2(0, 1), tint);
}

void Painter::Image(ui::ImmediateTexture* texture, ImVec2 min, ImVec2 max,
                    bool cover, ImU32 tint, float rounding) {
  if (!texture || !texture->width || !texture->height) {
    return;
  }
  const float box_w = max.x - min.x;
  const float box_h = max.y - min.y;
  const float aspect = float(texture->width) / float(texture->height);
  ImVec2 uv0(0, 0), uv1(1, 1);
  ImVec2 p0 = min, p1 = max;
  if (cover) {
    // Crop the texture to fill the box.
    if (box_w / box_h > aspect) {
      const float visible = (box_h / box_w) * aspect;
      uv0.y = (1.f - visible) * 0.5f;
      uv1.y = 1.f - uv0.y;
    } else {
      const float visible = (box_w / box_h) / aspect;
      uv0.x = (1.f - visible) * 0.5f;
      uv1.x = 1.f - uv0.x;
    }
  } else {
    // Letterbox inside the box.
    float w = box_w, h = box_w / aspect;
    if (h > box_h) {
      h = box_h;
      w = box_h * aspect;
    }
    p0 = ImVec2(min.x + (box_w - w) * 0.5f, min.y + (box_h - h) * 0.5f);
    p1 = ImVec2(p0.x + w, p0.y + h);
  }
  if (rounding > 0.f) {
    dl_->AddImageRounded(ToTextureId(texture), canvas_.P(p0), canvas_.P(p1),
                         uv0, uv1, tint, canvas_.S(rounding));
  } else {
    dl_->AddImage(ToTextureId(texture), canvas_.P(p0), canvas_.P(p1), uv0,
                  uv1, tint);
  }
}

ImVec2 Painter::MeasureText(std::string_view text, float size,
                            ui::DashboardFont weight, float wrap_width) {
  ImFont* font = Font(weight, size);
  const float px = canvas_.S(size);
  const ImVec2 measured = font->CalcTextSizeA(
      px, FLT_MAX, wrap_width > 0.f ? canvas_.S(wrap_width) : 0.f,
      text.data(), text.data() + text.size());
  return ImVec2(measured.x / canvas_.scale, measured.y / canvas_.scale);
}

float Painter::Text(ImVec2 pos, std::string_view text, float size,
                    ImU32 color, ui::DashboardFont weight, TextAlign align,
                    float wrap_width) {
  if (text.empty()) {
    return 0.f;
  }
  ImFont* font = Font(weight, size);
  const ImVec2 measured = MeasureText(text, size, weight, wrap_width);
  if (align == TextAlign::kCenter) {
    pos.x -= measured.x * 0.5f;
  } else if (align == TextAlign::kRight) {
    pos.x -= measured.x;
  }
  dl_->AddText(font, canvas_.S(size), canvas_.P(pos), color, text.data(),
               text.data() + text.size(),
               wrap_width > 0.f ? canvas_.S(wrap_width) : 0.f);
  return measured.x;
}

void Painter::TextClipped(ImVec2 pos, std::string_view text, float size,
                          ImU32 color, float max_width,
                          ui::DashboardFont weight, TextAlign align) {
  if (MeasureText(text, size, weight).x <= max_width) {
    Text(pos, text, size, color, weight, align);
    return;
  }
  // Binary search the longest prefix that fits with an ellipsis. Only cut on
  // UTF-8 code point boundaries.
  const std::string ellipsis = "\xE2\x80\xA6";
  size_t lo = 0, hi = text.size();
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    while (mid > 0 && (uint8_t(text[mid]) & 0xC0) == 0x80) {
      mid--;
    }
    if (mid <= lo) {
      break;
    }
    const std::string candidate = std::string(text.substr(0, mid)) + ellipsis;
    if (MeasureText(candidate, size, weight).x <= max_width) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  Text(pos, std::string(text.substr(0, lo)) + ellipsis, size, color, weight,
       align);
}

void Painter::TextVertical(ImVec2 pos, std::string_view text, float size,
                           ImU32 color, ui::DashboardFont weight) {
  if (text.empty()) {
    return;
  }
  ImFont* font = Font(weight, size);
  const int vtx_begin = dl_->VtxBuffer.Size;
  const ImVec2 pivot = canvas_.P(pos);
  dl_->AddText(font, canvas_.S(size), pivot, color, text.data(),
               text.data() + text.size());
  for (int i = vtx_begin; i < dl_->VtxBuffer.Size; i++) {
    ImDrawVert& v = dl_->VtxBuffer[i];
    const float dx = v.pos.x - pivot.x;
    const float dy = v.pos.y - pivot.y;
    // Rotate -90 degrees: text advances upwards, glyph tops face left.
    v.pos = ImVec2(pivot.x + dy, pivot.y - dx);
  }
}

float Painter::ButtonPrompt(ImVec2 pos, char button, std::string_view label,
                            ImU32 label_color, float size) {
  ImU32 color = theme::kButtonA;
  switch (button) {
    case 'B':
      color = theme::kButtonB;
      break;
    case 'X':
      color = theme::kButtonX;
      break;
    case 'Y':
      color = theme::kButtonY;
      break;
    default:
      break;
  }
  const float radius = size * 0.58f;
  const ImVec2 center(pos.x + radius, pos.y + size * 0.62f);
  Circle(center, radius, theme::WithAlpha(theme::kBlack, 0.35f), 24);
  Circle(center, radius * 0.92f, color, 24);
  Circle(ImVec2(center.x, center.y - radius * 0.25f), radius * 0.6f,
         theme::WithAlpha(theme::kWhite, 0.18f), 24);
  const char letter[2] = {button, 0};
  const ImVec2 letter_size =
      MeasureText(letter, size * 0.82f, ui::DashboardFont::kSemibold);
  Text(ImVec2(center.x - letter_size.x * 0.5f,
              center.y - letter_size.y * 0.52f),
       letter, size * 0.82f, theme::kWhite, ui::DashboardFont::kSemibold);
  const float label_x = pos.x + radius * 2.f + size * 0.35f;
  const float label_w =
      Text(ImVec2(label_x, pos.y), label, size, label_color);
  return (label_x - pos.x) + label_w + size * 1.2f;
}

void Painter::ButtonPrompts(
    ImVec2 pos, const std::vector<std::pair<char, std::string>>& prompts,
    ImU32 label_color, float size) {
  for (const auto& [button, label] : prompts) {
    pos.x += ButtonPrompt(pos, button, label, label_color, size);
  }
}

float Painter::Gamerscore(ImVec2 pos, uint32_t value, float size,
                          ImU32 color) {
  const float radius = size * 0.5f;
  const ImVec2 center(pos.x + radius, pos.y + size * 0.6f);
  dl_->AddCircle(canvas_.P(center), canvas_.S(radius), color, 24,
                 std::max(1.f, canvas_.S(size * 0.09f)));
  const ImVec2 g = MeasureText("G", size * 0.72f, ui::DashboardFont::kSemibold);
  Text(ImVec2(center.x - g.x * 0.5f, center.y - g.y * 0.52f), "G",
       size * 0.72f, color, ui::DashboardFont::kSemibold);
  const float w = Text(ImVec2(pos.x + radius * 2.f + size * 0.25f, pos.y),
                       fmt::format("{}", value), size, color);
  return radius * 2.f + size * 0.25f + w;
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
