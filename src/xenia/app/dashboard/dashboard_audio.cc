/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/dashboard_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <numbers>

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#include <playsoundapi.h>
#endif

namespace xe {
namespace app {
namespace dashboard {

namespace {

constexpr uint32_t kSampleRate = 22050;

struct Partial {
  float start;       // seconds
  float duration;    // seconds
  float freq_begin;  // Hz, 0 for noise
  float freq_end;    // Hz
  float amplitude;   // 0..1
};

void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    out.push_back(uint8_t(v >> (i * 8)));
  }
}

void AppendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
}

std::vector<uint8_t> MakeWave(const std::vector<int16_t>& samples) {
  std::vector<uint8_t> wav;
  const uint32_t data_size = uint32_t(samples.size() * sizeof(int16_t));
  wav.reserve(44 + data_size);
  wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
  AppendU32(wav, 36 + data_size);
  wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  AppendU32(wav, 16);
  AppendU16(wav, 1);  // PCM
  AppendU16(wav, 1);  // mono
  AppendU32(wav, kSampleRate);
  AppendU32(wav, kSampleRate * 2);
  AppendU16(wav, 2);
  AppendU16(wav, 16);
  wav.insert(wav.end(), {'d', 'a', 't', 'a'});
  AppendU32(wav, data_size);
  const size_t offset = wav.size();
  wav.resize(offset + data_size);
  std::memcpy(wav.data() + offset, samples.data(), data_size);
  return wav;
}

std::vector<uint8_t> Synthesize(std::initializer_list<Partial> partials) {
  float length = 0.f;
  for (const auto& p : partials) {
    length = std::max(length, p.start + p.duration);
  }
  std::vector<float> mix(size_t(length * kSampleRate) + 1, 0.f);
  uint32_t noise_state = 0x1234567u;
  for (const auto& p : partials) {
    const size_t begin = size_t(p.start * kSampleRate);
    const size_t count = size_t(p.duration * kSampleRate);
    double phase = 0.0;
    float lowpass = 0.f;
    for (size_t i = 0; i < count && begin + i < mix.size(); i++) {
      const float t = float(i) / float(kSampleRate);
      const float u = float(i) / float(count);
      // 4 ms attack, exponential release.
      const float attack = std::min(1.f, t / 0.004f);
      const float envelope = attack * std::exp(-4.5f * u) * (1.f - u);
      float value;
      if (p.freq_begin <= 0.f) {
        noise_state = noise_state * 1664525u + 1013904223u;
        const float white = float(int32_t(noise_state)) / 2147483648.f;
        // Sweeping one-pole low-pass gives a soft "whoosh".
        const float cutoff = 0.05f + 0.25f * std::sin(u * 3.14159f);
        lowpass += (white - lowpass) * cutoff;
        value = lowpass * 2.5f;
      } else {
        const float freq = p.freq_begin + (p.freq_end - p.freq_begin) * u;
        phase += 2.0 * std::numbers::pi * freq / kSampleRate;
        value = float(std::sin(phase) + 0.25 * std::sin(phase * 2.0));
      }
      mix[begin + i] += value * envelope * p.amplitude;
    }
  }
  std::vector<int16_t> samples(mix.size());
  for (size_t i = 0; i < mix.size(); i++) {
    samples[i] = int16_t(std::clamp(mix[i], -1.f, 1.f) * 32000.f);
  }
  return MakeWave(samples);
}

std::vector<uint8_t> SynthesizeEvent(SoundEvent event) {
  switch (event) {
    case SoundEvent::kNavigate:
      return Synthesize({{0.f, 0.035f, 2100.f, 1900.f, 0.18f}});
    case SoundEvent::kSelect:
      return Synthesize({{0.f, 0.07f, 1046.f, 1046.f, 0.25f},
                         {0.045f, 0.10f, 1568.f, 1568.f, 0.22f}});
    case SoundEvent::kBack:
      return Synthesize({{0.f, 0.07f, 1568.f, 1568.f, 0.22f},
                         {0.045f, 0.10f, 1046.f, 1000.f, 0.22f}});
    case SoundEvent::kBladeSlide:
      return Synthesize({{0.f, 0.22f, 0.f, 0.f, 0.35f},
                         {0.f, 0.08f, 900.f, 1300.f, 0.06f}});
    case SoundEvent::kChannelChange:
      return Synthesize({{0.f, 0.16f, 0.f, 0.f, 0.25f}});
    case SoundEvent::kGuideOpen:
      return Synthesize({{0.f, 0.18f, 0.f, 0.f, 0.22f},
                         {0.f, 0.12f, 784.f, 1175.f, 0.16f},
                         {0.06f, 0.16f, 1175.f, 1568.f, 0.14f}});
    case SoundEvent::kGuideClose:
      return Synthesize({{0.f, 0.16f, 0.f, 0.f, 0.20f},
                         {0.f, 0.14f, 1175.f, 784.f, 0.14f}});
    case SoundEvent::kAchievement:
      return Synthesize({{0.f, 0.25f, 1318.f, 1318.f, 0.22f},
                         {0.09f, 0.28f, 1661.f, 1661.f, 0.22f},
                         {0.18f, 0.45f, 1975.f, 1975.f, 0.24f}});
    case SoundEvent::kNotification:
      return Synthesize({{0.f, 0.12f, 1396.f, 1396.f, 0.2f},
                         {0.08f, 0.18f, 1760.f, 1760.f, 0.2f}});
    case SoundEvent::kError:
      return Synthesize({{0.f, 0.18f, 196.f, 185.f, 0.3f}});
    case SoundEvent::kCount:
      break;
  }
  return {};
}

}  // namespace

const char* DashboardAudio::EventFileName(SoundEvent event) {
  switch (event) {
    case SoundEvent::kNavigate:
      return "navigate";
    case SoundEvent::kSelect:
      return "select";
    case SoundEvent::kBack:
      return "back";
    case SoundEvent::kBladeSlide:
      return "blade_slide";
    case SoundEvent::kChannelChange:
      return "channel_change";
    case SoundEvent::kGuideOpen:
      return "guide_open";
    case SoundEvent::kGuideClose:
      return "guide_close";
    case SoundEvent::kAchievement:
      return "achievement";
    case SoundEvent::kNotification:
      return "notification";
    case SoundEvent::kError:
      return "error";
    case SoundEvent::kCount:
      break;
  }
  return "";
}

DashboardAudio::DashboardAudio(std::filesystem::path sound_dir)
    : sound_dir_(std::move(sound_dir)) {
  for (size_t i = 0; i < size_t(SoundEvent::kCount); i++) {
    LoadOrSynthesize(SoundEvent(i));
  }
}

void DashboardAudio::LoadOrSynthesize(SoundEvent event) {
  auto& buffer = sounds_[size_t(event)];
  const auto path =
      sound_dir_ / (std::string(EventFileName(event)) + ".wav");
  std::error_code ec;
  if (std::filesystem::is_regular_file(path, ec)) {
    std::ifstream file(path, std::ios::binary);
    buffer.assign(std::istreambuf_iterator<char>(file),
                  std::istreambuf_iterator<char>());
    if (buffer.size() > 44 && std::memcmp(buffer.data(), "RIFF", 4) == 0) {
      return;
    }
    XELOGW("Dashboard: ignoring invalid sound file {}",
           xe::path_to_utf8(path));
  }
  buffer = SynthesizeEvent(event);
}

void DashboardAudio::Play(SoundEvent event) {
  if (!enabled_ || event >= SoundEvent::kCount) {
    return;
  }
  const auto& buffer = sounds_[size_t(event)];
  if (buffer.empty()) {
    return;
  }
#if XE_PLATFORM_WIN32
  PlaySoundW(reinterpret_cast<LPCWSTR>(buffer.data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
