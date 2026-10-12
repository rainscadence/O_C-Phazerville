#pragma once

#include <stdint.h>

#include "AudioPitch.h"
#include "TxoPitch.h"

namespace txi2c2midi {

#if defined(ARDUINO_TEENSY41)
#define TX_MIDI_FLASHMEM __attribute__((noinline,noclone,section(".flashmem")))
#else
#define TX_MIDI_FLASHMEM
#endif

enum class MidiSelectionResult : uint8_t {
  Applied,
  Invalid,
  Microtonal,
};

enum class MidiPitchResult : uint8_t {
  Passed,
  Quantized,
  Microtonal,
  OutOfRange,
};

// Ordinary MIDI has no fractional-semitone representation in this first slice.
// A slot is usable when every active scale position and its repeating span land
// on exact semitones. Masked-out microtonal positions cannot be produced and do
// not make an otherwise integral slot unavailable.
TX_MIDI_FLASHMEM inline bool MidiScaleCompatible(
    const OC::QuantizerSettings& settings, const braids::Scale& scale) {
  if (!scale.span || scale.span % kPitchUnitsPerSemitone) return false;
  unsigned active = 0;
  uint16_t mask = settings.mask;
  for (unsigned note = 0; note < scale.num_notes && note < 16; ++note) {
    if (mask & 1) {
      ++active;
      if (scale.notes[note] % kPitchUnitsPerSemitone) return false;
    }
    mask >>= 1;
  }
  return active != 0;
}

// Per-channel optional final quantization for the currently implemented
// USB-device MIDI destination. Configuration changes affect future notes only;
// NoteScheduler continues to own the exact pitches already emitted.
class MidiPitch {
 public:
  static constexpr unsigned kChannels = 16;
  static constexpr uint8_t kMaxSelection =
      uint8_t(QuantizerSelection::LocalQ8);

  TX_MIDI_FLASHMEM void Reset() {
    for (auto& selections : selections_) selections = 0;
    shared_slot_ = 0;
    compatible_slots_ = 0;
    for (unsigned slot = 0; slot < 8; ++slot) {
      settings_[slot] = OC::QuantizerSettings{};
      scales_[slot] = nullptr;
    }
  }

  TX_MIDI_FLASHMEM bool ConfigureSlot(
      unsigned slot, const OC::QuantizerSettings* settings,
      const braids::Scale* scale) {
    if (slot >= 8) return false;
    compatible_slots_ &= ~(uint8_t(1) << slot);
    settings_[slot] = OC::QuantizerSettings{};
    scales_[slot] = nullptr;
    if (!settings || !scale) return false;
    settings_[slot] = *settings;
    scales_[slot] = scale;
    if (MidiScaleCompatible(*settings,*scale))
      compatible_slots_ |= uint8_t(1) << slot;
    return true;
  }

  TX_MIDI_FLASHMEM bool SetSharedSlot(unsigned slot) {
    if (slot >= 8) return false;
    shared_slot_ = uint8_t(slot);
    return true;
  }

  TX_MIDI_FLASHMEM MidiSelectionResult TrySetSelection(
      unsigned channel, uint8_t selection) {
    if (channel >= kChannels || selection > kMaxSelection)
      return MidiSelectionResult::Invalid;
    if (selection != uint8_t(QuantizerSelection::Off) &&
        !SlotAvailable(selection))
      return MidiSelectionResult::Microtonal;
    WriteSelection(channel,selection);
    return MidiSelectionResult::Applied;
  }

  TX_MIDI_FLASHMEM uint8_t Selection(unsigned channel) const {
    return channel < kChannels ? ReadSelection(channel)
                               : uint8_t(QuantizerSelection::Off);
  }

  TX_MIDI_FLASHMEM bool RestoreSelection(
      unsigned channel, uint8_t selection) {
    if (channel >= kChannels || selection > kMaxSelection) return false;
    WriteSelection(channel,selection);
    return true;
  }

  TX_MIDI_FLASHMEM bool Available(unsigned channel) const {
    return channel < kChannels && SlotAvailable(ReadSelection(channel));
  }

  TX_MIDI_FLASHMEM MidiPitchResult Prepare(
      unsigned channel, uint8_t note, uint8_t& output) const {
    if (channel >= kChannels || note > 127)
      return MidiPitchResult::OutOfRange;
    const uint8_t selection = ReadSelection(channel);
    if (selection == uint8_t(QuantizerSelection::Off)) {
      output = note;
      return MidiPitchResult::Passed;
    }
    if (!SlotAvailable(selection))
      return MidiPitchResult::Microtonal;
    const unsigned slot = Slot(selection);

    const int32_t source =
        (int32_t(note) - kMidiC4) * kPitchUnitsPerSemitone;
    const int32_t result = QuantizeAudioPitch(
        settings_[slot], *scales_[slot], source);
    if (result % kPitchUnitsPerSemitone)
      return MidiPitchResult::Microtonal;
    const int32_t midi = kMidiC4 + result / kPitchUnitsPerSemitone;
    if (midi < 0 || midi > 127) return MidiPitchResult::OutOfRange;
    output = uint8_t(midi);
    return MidiPitchResult::Quantized;
  }

  TX_MIDI_FLASHMEM bool Apply(
      unsigned channel, uint8_t note, uint8_t& output) const {
    const auto result = Prepare(channel,note,output);
    return result == MidiPitchResult::Passed ||
           result == MidiPitchResult::Quantized;
  }

 private:
  TX_MIDI_FLASHMEM uint8_t ReadSelection(unsigned channel) const {
    const uint8_t packed = selections_[channel >> 1];
    return channel & 1 ? packed >> 4 : packed & 0x0f;
  }

  TX_MIDI_FLASHMEM void WriteSelection(unsigned channel, uint8_t selection) {
    uint8_t& packed = selections_[channel >> 1];
    if (channel & 1) packed = uint8_t((packed & 0x0f) | (selection << 4));
    else packed = uint8_t((packed & 0xf0) | selection);
  }

  TX_MIDI_FLASHMEM unsigned Slot(uint8_t selection) const {
    return selection == uint8_t(QuantizerSelection::Shared)
        ? shared_slot_ : selection - 2;
  }

  TX_MIDI_FLASHMEM bool SlotAvailable(uint8_t selection) const {
    if (selection == uint8_t(QuantizerSelection::Off)) return true;
    if (selection > kMaxSelection) return false;
    const unsigned slot = Slot(selection);
    return scales_[slot] && (compatible_slots_ & (uint8_t(1) << slot));
  }

  uint8_t selections_[kChannels / 2]{};
  uint8_t shared_slot_ = 0;
  uint8_t compatible_slots_ = 0;
  OC::QuantizerSettings settings_[8]{};
  const braids::Scale* scales_[8]{};
};

#undef TX_MIDI_FLASHMEM

}  // namespace txi2c2midi
