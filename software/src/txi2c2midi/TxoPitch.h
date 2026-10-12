#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AudioPitch.h"

namespace txi2c2midi {

// Shared by audio voices and physical TXO CV outputs. Zero preserves raw CV;
// one follows the app's selected Shared Q slot; 2..9 select Local Q1..Q8.
enum class QuantizerSelection : uint8_t {
  Off = 0,
  Shared = 1,
  LocalQ1 = 2,
  LocalQ2 = 3,
  LocalQ3 = 4,
  LocalQ4 = 5,
  LocalQ5 = 6,
  LocalQ6 = 7,
  LocalQ7 = 8,
  LocalQ8 = 9,
};

// Main-loop TXO pitch preparation. Quantizer configuration and processing stay
// out of the IO interrupt; Process() only snapshots the already-prepared values.
template <class Lock> class TxoPitch {
public:
  static constexpr unsigned kChannels = 8;
  static constexpr uint8_t kMaxSelection =
      uint8_t(QuantizerSelection::LocalQ8);

  void Reset(uint8_t selection = uint8_t(QuantizerSelection::Shared)) {
    if (selection > kMaxSelection)
      selection = uint8_t(QuantizerSelection::Off);
    for (unsigned channel = 0; channel < kChannels; ++channel) {
      selections_[channel] = selection;
      enabled_[channel] = false;
      settings_[channel] = OC::QuantizerSettings{};
      quantizers_[channel].Init();
      prepared_[channel] = 0;
    }
  }

  bool SetSelection(unsigned channel, uint8_t selection) {
    if (channel >= kChannels || selection > kMaxSelection)
      return false;
    selections_[channel] = selection;
    if (selection == uint8_t(QuantizerSelection::Off))
      enabled_[channel] = false;
    return true;
  }

  uint8_t Selection(unsigned channel) const {
    return channel < kChannels ? selections_[channel]
                               : uint8_t(QuantizerSelection::Off);
  }

  void Configure(unsigned channel, const OC::QuantizerSettings *settings,
                 const braids::Scale *scale) {
    if (channel >= kChannels)
      return;
    if (selections_[channel] == uint8_t(QuantizerSelection::Off) ||
        !settings || !scale) {
      enabled_[channel] = false;
      return;
    }
    settings_[channel] = *settings;
    quantizers_[channel].Init();
    quantizers_[channel].Configure(*scale, settings->mask);
    quantizers_[channel].Requantize();
    enabled_[channel] = quantizers_[channel].enabled();
  }

  void Publish(const int32_t *values, size_t count, uint8_t cv_mask) {
    int32_t next[kChannels]{};
    if (count > kChannels)
      count = kChannels;
    for (unsigned channel = 0; channel < count; ++channel) {
      int32_t value = values[channel];
      if ((cv_mask & (uint8_t(1) << channel)) && enabled_[channel]) {
        const auto &settings = settings_[channel];
        value = quantizers_[channel].Process(
            value, int32_t(settings.root) * kPitchUnitsPerSemitone, 0);
        value = settings.range.Apply(value, kPitchUnitsPerOctave);
        value += int32_t(settings.octave) * kPitchUnitsPerOctave;
      }
      next[channel] = value;
    }
    Lock lock;
    for (unsigned channel = 0; channel < kChannels; ++channel)
      prepared_[channel] = next[channel];
  }

  template <class Values> void Snapshot(Values &values) const {
    Lock lock;
    for (unsigned channel = 0; channel < kChannels; ++channel)
      values[channel] = prepared_[channel];
  }

  void ClearPrepared() {
    Lock lock;
    for (auto &value : prepared_)
      value = 0;
  }

private:
  uint8_t selections_[kChannels]{};
  bool enabled_[kChannels]{};
  OC::QuantizerSettings settings_[kChannels]{};
  braids::Quantizer quantizers_[kChannels];
  int32_t prepared_[kChannels]{};
};

} // namespace txi2c2midi
