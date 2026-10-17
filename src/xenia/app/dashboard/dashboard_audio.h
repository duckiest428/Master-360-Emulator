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
#include <memory>
#include <mutex>
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

// Which console era's interface sounds to use.
enum class SoundTheme : uint8_t {
  kMetro,   // 2010+ dashboard and Guide
  kBlades,  // 2005 Blades dashboard and Guide
};

// Plays dashboard UI sounds, mixing them so they can overlap like on the
// console. For each event the first available source wins:
//  1. <sound_dir>/<name>.wav provided by the user,
//  2. the console recordings that ship with DashX360 (Assets/Audio/Sounds),
//  3. a short synthesized placeholder.
// No Microsoft audio is bundled with Xenia.
class DashboardAudio {
 public:
  DashboardAudio(std::filesystem::path sound_dir,
                 std::filesystem::path original_sound_dir);
  ~DashboardAudio();

  void Play(SoundEvent event);
  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }

  void SetTheme(SoundTheme theme);
  SoundTheme theme() const { return theme_; }

  static const char* EventFileName(SoundEvent event);

 private:
  class Mixer;

  void LoadAll();
  void Load(SoundEvent event);
  static std::vector<uint8_t> ReadWave(const std::filesystem::path& path);

  std::filesystem::path sound_dir_;
  std::filesystem::path original_sound_dir_;
  SoundTheme theme_ = SoundTheme::kMetro;
  bool enabled_ = true;
  std::mutex mutex_;
  std::unique_ptr<Mixer> mixer_;
  // RIFF/WAVE images, kept alive for asynchronous playback.
  std::array<std::vector<uint8_t>, size_t(SoundEvent::kCount)> sounds_;
  // Blades has a different whoosh for each blade; rotate through them.
  std::vector<std::vector<uint8_t>> blade_slides_;
  size_t next_blade_slide_ = 0;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_DASHBOARD_AUDIO_H_
