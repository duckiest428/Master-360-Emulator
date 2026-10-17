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
#include <span>

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_WIN32
#include "xenia/apu/xaudio2/xaudio2_api.h"
#include "xenia/base/platform_win.h"

// Needs windows.h from platform_win.h first.
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
      return Synthesize(
          {{0.f, 0.22f, 0.f, 0.f, 0.35f}, {0.f, 0.08f, 900.f, 1300.f, 0.06f}});
    case SoundEvent::kChannelChange:
      return Synthesize({{0.f, 0.16f, 0.f, 0.f, 0.25f}});
    case SoundEvent::kGuideOpen:
      return Synthesize({{0.f, 0.18f, 0.f, 0.f, 0.22f},
                         {0.f, 0.12f, 784.f, 1175.f, 0.16f},
                         {0.06f, 0.16f, 1175.f, 1568.f, 0.14f}});
    case SoundEvent::kGuideClose:
      return Synthesize(
          {{0.f, 0.16f, 0.f, 0.f, 0.20f}, {0.f, 0.14f, 1175.f, 784.f, 0.14f}});
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

// Console recordings as named in DashX360's Assets/Audio/Sounds.
const char* OriginalFileName(SoundTheme theme, SoundEvent event) {
  const bool blades = theme == SoundTheme::kBlades;
  switch (event) {
    case SoundEvent::kNavigate:
      return blades ? "blades-hover.wav" : "guide-hover.wav";
    case SoundEvent::kSelect:
      return blades ? "blades-select.wav" : "guide-select.wav";
    case SoundEvent::kBack:
      return blades ? "blades-back.wav" : "guide-back.wav";
    case SoundEvent::kChannelChange:
      return blades ? "blades-switch-live-to-games.wav" : "tile-hover.wav";
    case SoundEvent::kGuideOpen:
      return blades ? "blades-guide-open.wav" : "hud-open.wav";
    case SoundEvent::kGuideClose:
      return blades ? "blades-guide-close.wav" : "hud-close.wav";
    case SoundEvent::kAchievement:
    case SoundEvent::kNotification:
      return "notify-popup.wav";
    case SoundEvent::kBladeSlide:  // See kBladeSlideFiles.
    case SoundEvent::kError:       // The console has no recording for it.
    case SoundEvent::kCount:
      break;
  }
  return nullptr;
}

constexpr const char* kMetroBladeSlideFiles[] = {
    "blade-switch-1.wav", "blade-switch-2.wav", "blade-switch-3.wav",
    "blade-switch-4.wav"};
constexpr const char* kBladesBladeSlideFiles[] = {
    "blades-switch-marketplace-to-live.wav", "blades-switch-live-to-games.wav",
    "blades-switch-games-to-media.wav", "blades-switch-media-to-system.wav"};

struct WaveView {
  const uint8_t* format = nullptr;  // PCMWAVEFORMAT
  uint32_t format_size = 0;
  const uint8_t* data = nullptr;
  uint32_t data_size = 0;
};

bool ParseWave(const std::vector<uint8_t>& file, WaveView& out) {
  if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) ||
      std::memcmp(file.data() + 8, "WAVE", 4)) {
    return false;
  }
  size_t offset = 12;
  while (offset + 8 <= file.size()) {
    uint32_t size;
    std::memcpy(&size, file.data() + offset + 4, 4);
    const uint8_t* body = file.data() + offset + 8;
    if (offset + 8 + size > file.size()) {
      size = uint32_t(file.size() - offset - 8);
    }
    if (!std::memcmp(file.data() + offset, "fmt ", 4)) {
      out.format = body;
      out.format_size = size;
    } else if (!std::memcmp(file.data() + offset, "data", 4)) {
      out.data = body;
      out.data_size = size;
    }
    offset += 8 + size + (size & 1);
  }
  return out.format && out.format_size >= 16 && out.data && out.data_size;
}

}  // namespace

#if XE_PLATFORM_WIN32

// A private XAudio2 engine so interface sounds can overlap, independent of the
// emulated title's audio (which is paused while the Guide is open).
class DashboardAudio::Mixer {
 public:
  static std::unique_ptr<Mixer> Create() {
    auto mixer = std::unique_ptr<Mixer>(new Mixer());
    return mixer->Initialize() ? std::move(mixer) : nullptr;
  }

  ~Mixer() {
    for (auto& voice : voices_) {
      voice.voice->DestroyVoice();
    }
    voices_.clear();
    if (master_) {
      master_->DestroyVoice();
    }
    if (engine_) {
      engine_->Release();
    }
    if (module_) {
      FreeLibrary(module_);
    }
  }

  bool Play(std::shared_ptr<const std::vector<uint8_t>> file) {
    Reap();
    WaveView wave;
    if (!ParseWave(*file, wave)) {
      return false;
    }
    WAVEFORMATEX format = {};
    std::memcpy(&format, wave.format, 16);
    format.cbSize = 0;
    if (format.wFormatTag != WAVE_FORMAT_PCM) {
      return false;
    }
    apu::xaudio2::api::IXAudio2_8SourceVoice* voice = nullptr;
    if (FAILED(engine_->CreateSourceVoice(&voice, &format))) {
      return false;
    }
    apu::xaudio2::api::XAUDIO2_BUFFER buffer = {};
    buffer.Flags = 0x0040;  // XAUDIO2_END_OF_STREAM
    buffer.AudioBytes = wave.data_size;
    buffer.pAudioData = wave.data;
    if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start())) {
      voice->DestroyVoice();
      return false;
    }
    voices_.push_back({voice, std::move(file)});
    return true;
  }

 private:
  struct Voice {
    apu::xaudio2::api::IXAudio2_8SourceVoice* voice;
    // Keeps the samples alive until the voice is destroyed.
    std::shared_ptr<const std::vector<uint8_t>> file;
  };

  Mixer() = default;

  bool Initialize() {
    module_ = LoadLibraryW(L"XAudio2_9.dll");
    if (!module_) {
      module_ = LoadLibraryW(L"XAudio2_8.dll");
    }
    if (!module_) {
      return false;
    }
    using CreateFn =
        HRESULT(__stdcall*)(apu::xaudio2::api::IXAudio2_8**, UINT32,
                            apu::xaudio2::api::XAUDIO2_PROCESSOR);
    auto create =
        reinterpret_cast<CreateFn>(GetProcAddress(module_, "XAudio2Create"));
    if (!create ||
        FAILED(create(&engine_, 0,
                      apu::xaudio2::api::XE_XAUDIO2_8_DEFAULT_PROCESSOR))) {
      return false;
    }
    return SUCCEEDED(engine_->CreateMasteringVoice(&master_));
  }

  void Reap() {
    std::erase_if(voices_, [](const Voice& voice) {
      apu::xaudio2::api::XAUDIO2_VOICE_STATE state = {};
      voice.voice->GetState(
          &state, apu::xaudio2::api::XE_XAUDIO2_VOICE_NOSAMPLESPLAYED);
      if (state.BuffersQueued) {
        return false;
      }
      voice.voice->DestroyVoice();
      return true;
    });
  }

  HMODULE module_ = nullptr;
  apu::xaudio2::api::IXAudio2_8* engine_ = nullptr;
  apu::xaudio2::api::IXAudio2_8MasteringVoice* master_ = nullptr;
  std::vector<Voice> voices_;
};

#else

class DashboardAudio::Mixer {
 public:
  static std::unique_ptr<Mixer> Create() { return nullptr; }
  bool Play(std::shared_ptr<const std::vector<uint8_t>>) { return false; }
};

#endif  // XE_PLATFORM_WIN32

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

DashboardAudio::DashboardAudio(std::filesystem::path sound_dir,
                               std::filesystem::path original_sound_dir)
    : sound_dir_(std::move(sound_dir)),
      original_sound_dir_(std::move(original_sound_dir)) {
  if (!original_sound_dir_.empty()) {
    XELOGI("Dashboard: using console interface sounds from {}",
           xe::path_to_utf8(original_sound_dir_));
  }
  LoadAll();
}

DashboardAudio::~DashboardAudio() = default;

void DashboardAudio::SetTheme(SoundTheme theme) {
  std::lock_guard lock(mutex_);
  if (theme_ == theme) {
    return;
  }
  theme_ = theme;
  LoadAll();
}

std::vector<uint8_t> DashboardAudio::ReadWave(
    const std::filesystem::path& path) {
  std::error_code ec;
  if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
    return {};
  }
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
  WaveView wave;
  if (!ParseWave(buffer, wave)) {
    XELOGW("Dashboard: ignoring invalid sound file {}", xe::path_to_utf8(path));
    return {};
  }
  return buffer;
}

void DashboardAudio::LoadAll() {
  for (size_t i = 0; i < size_t(SoundEvent::kCount); i++) {
    Load(SoundEvent(i));
  }
  blade_slides_.clear();
  next_blade_slide_ = 0;
  if (!sounds_[size_t(SoundEvent::kBladeSlide)].empty() ||
      original_sound_dir_.empty()) {
    return;  // The user's own blade_slide.wav, or no console recordings.
  }
  for (const char* name : theme_ == SoundTheme::kBlades
                              ? std::span(kBladesBladeSlideFiles)
                              : std::span(kMetroBladeSlideFiles)) {
    auto wave = ReadWave(original_sound_dir_ / name);
    if (!wave.empty()) {
      blade_slides_.push_back(std::move(wave));
    }
  }
  if (!blade_slides_.empty()) {
    sounds_[size_t(SoundEvent::kBladeSlide)] = blade_slides_.front();
  }
}

void DashboardAudio::Load(SoundEvent event) {
  auto& buffer = sounds_[size_t(event)];
  buffer = ReadWave(sound_dir_ / (std::string(EventFileName(event)) + ".wav"));
  if (!buffer.empty() || event == SoundEvent::kBladeSlide) {
    return;
  }
  if (const char* name = OriginalFileName(theme_, event);
      name && !original_sound_dir_.empty()) {
    buffer = ReadWave(original_sound_dir_ / name);
    if (!buffer.empty()) {
      return;
    }
  }
  buffer = SynthesizeEvent(event);
}

void DashboardAudio::Play(SoundEvent event) {
  if (!enabled_ || event >= SoundEvent::kCount) {
    return;
  }
  std::lock_guard lock(mutex_);
  const std::vector<uint8_t>* source = &sounds_[size_t(event)];
  if (event == SoundEvent::kBladeSlide && !blade_slides_.empty()) {
    source = &blade_slides_[next_blade_slide_++ % blade_slides_.size()];
  }
  if (source->empty()) {
    if (event == SoundEvent::kBladeSlide) {
      source = &(sounds_[size_t(event)] = SynthesizeEvent(event));
    } else {
      return;
    }
  }
  // Copy into shared storage: a theme change may replace the buffers while a
  // voice still plays the old one.
  auto file = std::make_shared<const std::vector<uint8_t>>(*source);
  if (!mixer_) {
    mixer_ = Mixer::Create();
  }
  if (mixer_ && mixer_->Play(file)) {
    return;
  }
#if XE_PLATFORM_WIN32
  // Fallback: one sound at a time. PlaySound reads the buffer asynchronously,
  // so play from the long-lived copy.
  PlaySoundW(reinterpret_cast<LPCWSTR>(source->data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
