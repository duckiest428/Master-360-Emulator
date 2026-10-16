/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_DASHBOARD_INPUT_H_
#define XENIA_APP_DASHBOARD_DASHBOARD_INPUT_H_

#include <array>
#include <cstdint>

namespace xe {
namespace hid {
class InputSystem;
}  // namespace hid
}  // namespace xe

namespace xe {
namespace app {
namespace dashboard {

enum class Button : uint8_t {
  kUp,
  kDown,
  kLeft,
  kRight,
  kA,
  kB,
  kX,
  kY,
  kLeftBumper,
  kRightBumper,
  kLeftTrigger,
  kRightTrigger,
  kStart,
  kBack,
  kCount,
};

// Polls every connected controller (any pad can drive the dashboard, as on
// the console) plus the keyboard, and turns them into per-frame button
// presses with console-like auto-repeat for directions and bumpers. The
// left stick and right stick act as a D-pad.
class DashboardInput {
 public:
  // Must be called once per frame from inside an ImGui frame.
  void Update(hid::InputSystem* input_system, float delta_time,
              bool has_focus);

  // True on the frame the button went down, and on auto-repeat ticks.
  bool Pressed(Button button) const {
    return pressed_[size_t(button)];
  }
  bool Held(Button button) const { return held_[size_t(button)]; }
  // Moves taken this frame on the vertical/horizontal axis (-1, 0, +1).
  int Vertical() const;
  int Horizontal() const;

  // Ignores buttons currently held until they are released, so the press
  // that closed another screen doesn't also act on this one.
  void SwallowHeldButtons();

 private:
  std::array<bool, size_t(Button::kCount)> last_pad_down_ = {};
  std::array<bool, size_t(Button::kCount)> held_ = {};
  std::array<bool, size_t(Button::kCount)> pressed_ = {};
  std::array<bool, size_t(Button::kCount)> swallowed_ = {};
  std::array<float, size_t(Button::kCount)> hold_time_ = {};
  std::array<float, size_t(Button::kCount)> next_repeat_ = {};
  bool had_focus_ = false;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_INPUT_H_
