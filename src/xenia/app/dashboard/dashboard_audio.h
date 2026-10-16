/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_DASHBOARD_AUDIO_H_
#define XENIA_APP_DASHBOARD_DASHBOARD_AUDIO_H_

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace xe {
namespace app {
namespace dashboard {

enum class SoundEvent : uint8_t {
  kNavigate,
  kSelect,
  kBack,
  kBladeSlide,
  kChannelChange,
  kGuideOpen,
  kGuideClose,
  kAchievement,
  kNotification,
  kError,
  kCount,
};

// Plays dashboard UI sounds. Each event loads <sound_dir>/<name>.wav when the
// user provides one (for example sounds recorded from their own console);
// otherwise a short synthesized placeholder is used. No Microsoft audio is
// bundled.
class DashboardAudio {
 public:
  explicit DashboardAudio(std::filesystem::path sound_dir);

  void Play(SoundEvent event);
  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }

  static const char* EventFileName(SoundEvent event);

 private:
  void LoadOrSynthesize(SoundEvent event);

  std::filesystem::path sound_dir_;
  bool enabled_ = true;
  // RIFF/WAVE images kept alive for asynchronous playback.
  std::array<std::vector<uint8_t>, size_t(SoundEvent::kCount)> sounds_;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_AUDIO_H_
