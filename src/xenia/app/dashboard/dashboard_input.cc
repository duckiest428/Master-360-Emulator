/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/dashboard_input.h"

#include <cstdlib>

#include "third_party/imgui/imgui.h"
#include "xenia/hid/input_system.h"

namespace xe {
namespace app {
namespace dashboard {

namespace {

constexpr float kRepeatDelay = 0.40f;
constexpr float kRepeatInterval = 0.09f;
constexpr int kStickThreshold = 16000;
constexpr uint8_t kTriggerThreshold = 96;

bool Repeats(Button button) {
  switch (button) {
    case Button::kUp:
    case Button::kDown:
    case Button::kLeft:
    case Button::kRight:
    case Button::kLeftBumper:
    case Button::kRightBumper:
    case Button::kLeftTrigger:
    case Button::kRightTrigger:
      return true;
    default:
      return false;
  }
}

}  // namespace

void DashboardInput::Update(hid::InputSystem* input_system, float delta_time,
                            bool has_focus) {
  std::array<bool, size_t(Button::kCount)> down = {};

  if (has_focus && input_system) {
    // Never block the UI thread on the input lock: a suspended guest thread
    // may be holding it. If it's busy this frame, reuse the last pad state so
    // held buttons don't appear released and re-trigger.
    auto lock = input_system->try_lock();
    if (!lock.owns_lock()) {
      down = last_pad_down_;
    } else {
      for (uint32_t user = 0; user < XUserMaxUserCount; user++) {
        hid::X_INPUT_STATE state = {};
        if (input_system->GetState(user, hid::X_INPUT_FLAG_GAMEPAD, &state) !=
            X_ERROR_SUCCESS) {
          continue;
        }
        const uint16_t b = state.gamepad.buttons;
        const int lx = state.gamepad.thumb_lx;
        const int ly = state.gamepad.thumb_ly;
        const int rx = state.gamepad.thumb_rx;
        const int ry = state.gamepad.thumb_ry;
        auto set = [&](Button button, bool value) {
          down[size_t(button)] = down[size_t(button)] || value;
        };
        set(Button::kUp, (b & hid::X_INPUT_GAMEPAD_DPAD_UP) ||
                             ly > kStickThreshold || ry > kStickThreshold);
        set(Button::kDown, (b & hid::X_INPUT_GAMEPAD_DPAD_DOWN) ||
                               ly < -kStickThreshold || ry < -kStickThreshold);
        set(Button::kLeft, (b & hid::X_INPUT_GAMEPAD_DPAD_LEFT) ||
                               lx < -kStickThreshold || rx < -kStickThreshold);
        set(Button::kRight, (b & hid::X_INPUT_GAMEPAD_DPAD_RIGHT) ||
                                lx > kStickThreshold || rx > kStickThreshold);
        set(Button::kA, b & hid::X_INPUT_GAMEPAD_A);
        set(Button::kB, b & hid::X_INPUT_GAMEPAD_B);
        set(Button::kX, b & hid::X_INPUT_GAMEPAD_X);
        set(Button::kY, b & hid::X_INPUT_GAMEPAD_Y);
        set(Button::kLeftBumper, b & hid::X_INPUT_GAMEPAD_LEFT_SHOULDER);
        set(Button::kRightBumper, b & hid::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
        set(Button::kLeftTrigger,
            state.gamepad.left_trigger > kTriggerThreshold);
        set(Button::kRightTrigger,
            state.gamepad.right_trigger > kTriggerThreshold);
        set(Button::kStart, b & hid::X_INPUT_GAMEPAD_START);
        set(Button::kBack, b & hid::X_INPUT_GAMEPAD_BACK);
      }
      last_pad_down_ = down;
    }
  } else {
    last_pad_down_ = {};
  }

  if (has_focus) {
    auto key = [&](Button button, ImGuiKey k) {
      down[size_t(button)] = down[size_t(button)] || ImGui::IsKeyDown(k);
    };
    key(Button::kUp, ImGuiKey_UpArrow);
    key(Button::kDown, ImGuiKey_DownArrow);
    key(Button::kLeft, ImGuiKey_LeftArrow);
    key(Button::kRight, ImGuiKey_RightArrow);
    key(Button::kA, ImGuiKey_Enter);
    key(Button::kA, ImGuiKey_Space);
    key(Button::kB, ImGuiKey_Escape);
    key(Button::kB, ImGuiKey_Backspace);
    key(Button::kX, ImGuiKey_X);
    key(Button::kY, ImGuiKey_Y);
    key(Button::kLeftBumper, ImGuiKey_PageUp);
    key(Button::kRightBumper, ImGuiKey_PageDown);
    key(Button::kLeftTrigger, ImGuiKey_Home);
    key(Button::kRightTrigger, ImGuiKey_End);
  }

  if (has_focus && !had_focus_) {
    // Regained focus (a dialog on top closed): ignore whatever is held.
    for (size_t i = 0; i < down.size(); i++) {
      swallowed_[i] = down[i];
    }
  }
  had_focus_ = has_focus;

  for (size_t i = 0; i < down.size(); i++) {
    pressed_[i] = false;
    if (!down[i]) {
      swallowed_[i] = false;
      held_[i] = false;
      hold_time_[i] = 0.f;
      continue;
    }
    if (swallowed_[i]) {
      continue;
    }
    if (!held_[i]) {
      held_[i] = true;
      pressed_[i] = true;
      hold_time_[i] = 0.f;
      next_repeat_[i] = kRepeatDelay;
      continue;
    }
    hold_time_[i] += delta_time;
    if (Repeats(Button(i)) && hold_time_[i] >= next_repeat_[i]) {
      pressed_[i] = true;
      next_repeat_[i] += kRepeatInterval;
    }
  }
}

int DashboardInput::Vertical() const {
  return (Pressed(Button::kDown) ? 1 : 0) - (Pressed(Button::kUp) ? 1 : 0);
}

int DashboardInput::Horizontal() const {
  return (Pressed(Button::kRight) ? 1 : 0) - (Pressed(Button::kLeft) ? 1 : 0);
}

void DashboardInput::SwallowHeldButtons() {
  for (size_t i = 0; i < held_.size(); i++) {
    if (held_[i]) {
      swallowed_[i] = true;
      held_[i] = false;
    }
  }
  // Force re-evaluation of focus next frame as well.
  had_focus_ = false;
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
