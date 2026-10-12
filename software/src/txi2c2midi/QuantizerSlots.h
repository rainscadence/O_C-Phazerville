#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../quantizer/QuantizerBank.h"
#include "../src/extern/braids_quantizer.h"

namespace txi2c2midi {

using QuantizerSlot = OC::QuantizerSettings;

// App-local Q1-Q8 snapshot. This class is deliberately independent of the
// stateful Braids quantizer so each destination can later materialize its own
// hysteresis state from these settings.
class QuantizerSlots {
 public:
  static constexpr unsigned kCount = OC::QuantizerBank::kCount;

  void Reset(int16_t chromatic_scale) {
    for (auto& slot : slots_) {
      slot = QuantizerSlot{};
      slot.scale = chromatic_scale;
    }
  }

  const QuantizerSlot& Get(unsigned slot) const { return slots_[slot & 7]; }
  QuantizerSlot& Get(unsigned slot) { return slots_[slot & 7]; }

  bool Set(unsigned slot, int scale, int root, int octave, uint16_t mask,
           int scale_count) {
    if (slot >= kCount || scale < 0 || scale >= scale_count || root < 0 ||
        root > 11 || octave < -8 || octave > 8 || !mask) return false;
    QuantizerSlot next;
    next.scale = int16_t(scale);
    next.root = int8_t(root);
    next.octave = int8_t(octave);
    next.mask = mask;
    slots_[slot] = next;
    return true;
  }

  bool SetRange(unsigned slot, int low, int high) {
    return slot < kCount && slots_[slot].range.Set(low, high);
  }

  bool DisableRange(unsigned slot) {
    if (slot >= kCount) return false;
    slots_[slot].range.Disable();
    return true;
  }

  template<class Source>
  void Import(const Source* source) {
    for (unsigned slot = 0; slot < kCount; ++slot) {
      slots_[slot].scale = source[slot].scale;
      slots_[slot].root = source[slot].root_note;
      slots_[slot].octave = source[slot].octave;
      slots_[slot].mask = source[slot].mask;
      slots_[slot].range = source[slot].range;
    }
  }

  bool Read(unsigned slot, QuantizerSlot& out) const {
    if (slot >= kCount) return false;
    out = slots_[slot];
    return true;
  }

  bool Replace(unsigned slot, const QuantizerSlot& settings, int scale_count) {
    if (!OC::QuantizerBank::Valid(slot, settings, scale_count)) return false;
    slots_[slot] = settings;
    return true;
  }

 private:
  QuantizerSlot slots_[kCount];
};

// Setup-local copies of USR1-USR4. A Q slot stores the scale index, while this
// bank owns the corresponding note positions so later Scale Editor changes do
// not silently alter a saved TXI2C2MIDI setup.
class UserScaleSnapshot {
 public:
  static constexpr unsigned kCount = 4;
  static constexpr unsigned kMaxNotes = 16;
  static constexpr unsigned kSerializedScaleBytes =
      sizeof(int16_t) + sizeof(uint8_t) + kMaxNotes * sizeof(int16_t);
  static constexpr unsigned kSerializedBytes = kCount * kSerializedScaleBytes;

  void Reset(const braids::Scale& fallback) {
    if (!Valid(fallback)) return;
    for (auto& scale : scales_) scale = fallback;
  }

  bool Capture(const braids::Scale* source, unsigned count) {
    if (!source || count < kCount) return false;
    for (unsigned scale = 0; scale < kCount; ++scale)
      if (!Valid(source[scale])) return false;
    for (unsigned scale = 0; scale < kCount; ++scale)
      scales_[scale] = source[scale];
    return true;
  }

  bool Replace(unsigned slot, const braids::Scale& scale) {
    if (slot >= kCount || !Valid(scale)) return false;
    scales_[slot] = scale;
    return true;
  }

  const braids::Scale& Get(unsigned slot) const {
    return scales_[slot < kCount ? slot : 0];
  }

  static bool Valid(const braids::Scale& scale) {
    if (scale.span < (12 << 7) || scale.span > (24 << 7) ||
        scale.num_notes < 1 || scale.num_notes > kMaxNotes ||
        scale.notes[0] != 0)
      return false;
    for (unsigned note = 1; note < scale.num_notes; ++note)
      if (scale.notes[note] <= scale.notes[note - 1] ||
          scale.notes[note] >= scale.span)
        return false;
    return true;
  }

 private:
  braids::Scale scales_[kCount]{};
};

static_assert(sizeof(QuantizerSlot) == 8, "Quantizer snapshot wire/storage size changed");
static_assert(sizeof(QuantizerSlots) == 64, "Q1-Q8 snapshot must stay compact");

// The T4.1 audio build keeps only 16 KiB of DTCM headroom for stack/locals.
// This snapshot is control-rate state, so place its single firmware instance
// in RAM2 rather than charging every active app object against that reserve.
extern QuantizerSlots quantizer_slots;
extern UserScaleSnapshot user_scale_snapshot;

} // namespace txi2c2midi
