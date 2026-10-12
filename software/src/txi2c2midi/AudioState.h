#pragma once

#include <stdint.h>

namespace txi2c2midi {

enum class AudioPitchMode : uint8_t { Frequency, Note };

enum class AudioQuantizerSelection : uint8_t {
  Off = 0,
  Shared = 1,
  LocalQ1 = 2,
  LocalQ8 = 9,
};

struct AudioVoiceState {
  bool enabled = false;            // Runtime safety state; never restored on entry.
  uint8_t waveform = 0;            // OC::AudioIO::TxWaveform.
  uint8_t level_percent = 25;
  // Bit0 selects Note mode; bits1..4 select Off/Shared/Local Q1-Q8. This uses
  // the byte that was formerly alignment padding, keeping each voice at 8 B.
  uint8_t pitch_flags = uint8_t(AudioQuantizerSelection::Shared) << 1;
  union {
    uint32_t frequency_millihz = 440000;
    int16_t pitch;
  };

  AudioPitchMode PitchMode() const {
    return pitch_flags & 1 ? AudioPitchMode::Note : AudioPitchMode::Frequency;
  }
  uint8_t QuantizerSelection() const { return (pitch_flags >> 1) & 0x0f; }
};

static_assert(sizeof(AudioVoiceState) == 8,
              "Audio voice state must not consume additional DTCM");

class AudioState {
 public:
  static constexpr unsigned kVoices = 2;
  static constexpr uint32_t kMinFrequencyMillihz = 100;
  static constexpr uint32_t kMaxFrequencyMillihz = 20000000;
  static constexpr uint8_t kWaveformCount = 6;
  static constexpr int16_t kMinPitch = -60 * 128; // MIDI note 0.
  static constexpr int16_t kMaxPitch = 67 * 128;  // MIDI note 127.

  void Reset() {
    for (auto& voice : voices_) voice = AudioVoiceState{};
  }

  AudioVoiceState& Voice(unsigned voice) { return voices_[voice & 1]; }
  const AudioVoiceState& Voice(unsigned voice) const { return voices_[voice & 1]; }

  bool SetFrequency(unsigned voice, uint32_t millihz) {
    if (voice >= kVoices || millihz < kMinFrequencyMillihz ||
        millihz > kMaxFrequencyMillihz) return false;
    voices_[voice].frequency_millihz = millihz;
    return true;
  }

  bool SetWaveform(unsigned voice, uint8_t waveform) {
    if (voice >= kVoices || waveform >= kWaveformCount) return false;
    voices_[voice].waveform = waveform;
    return true;
  }

  bool SetLevel(unsigned voice, uint8_t level) {
    if (voice >= kVoices || level > 100) return false;
    voices_[voice].level_percent = level;
    return true;
  }

  bool SetPitch(unsigned voice, int pitch) {
    if (voice >= kVoices || pitch < kMinPitch || pitch > kMaxPitch)
      return false;
    voices_[voice].pitch = int16_t(pitch);
    return true;
  }

  bool SetPitchMode(unsigned voice, AudioPitchMode mode) {
    if (voice >= kVoices) return false;
    auto& state = voices_[voice];
    if (state.PitchMode() == mode) return true;
    if (mode == AudioPitchMode::Note) {
      state.pitch = 9 * 128;
      state.pitch_flags |= 1;
    } else {
      state.frequency_millihz = 440000;
      state.pitch_flags &= ~uint8_t(1);
    }
    return true;
  }

  bool SetQuantizer(unsigned voice, unsigned selection) {
    if (voice >= kVoices || selection > uint8_t(AudioQuantizerSelection::LocalQ8))
      return false;
    voices_[voice].pitch_flags = uint8_t(
        (voices_[voice].pitch_flags & 1) | (selection << 1));
    return true;
  }

  void Silence() {
    for (auto& voice : voices_) voice.enabled = false;
  }

 private:
  AudioVoiceState voices_[kVoices];
};

} // namespace txi2c2midi
