#pragma once

#include <stdint.h>

#include "AudioPitch.h"

namespace txi2c2midi {

struct TxiQuantizedValue {
  int16_t value = 0;
  int16_t note = 0;
};

// TXI's raw domain uses 16384 steps for 10 V while Phazerville pitch uses
// 1536 units per volt. Virtual PARAM knobs therefore use the same exact 15/16
// conversion as the TXO CV engine.
inline int32_t TxiKnobPitch(int16_t raw) {
  if (raw <= 0) return 0;
  if (raw >= 16383) return 10 * kPitchUnitsPerOctave;
  return int32_t(raw) * 15 / 16;
}

inline int16_t TxiQuantizedRaw(int32_t pitch) {
  const int32_t limit = 10 * kPitchUnitsPerOctave;
  if (pitch <= -limit) return -16384;
  if (pitch >= limit) return 16383;
  return int16_t(pitch * 16 / 15);
}

// Native TXI quantization ignores CV polarity before looking up a scale note.
// The selected Phazerville Q slot then supplies the shared scale, root, mask,
// octave window and fixed octave. The note reply remains TXI's zero-based
// scale-degree number; it is deliberately not a MIDI note number.
inline TxiQuantizedValue QuantizeTxiPitch(
    const OC::QuantizerSettings& settings,
    const braids::Scale& scale,
    int32_t pitch) {
  if (pitch < 0) pitch = -pitch;

  braids::Quantizer quantizer;
  quantizer.Init();
  quantizer.Configure(scale, settings.mask);
  int32_t quantized = quantizer.Process(
      pitch, int32_t(settings.root) * kPitchUnitsPerSemitone, 0);
  quantized = settings.range.Apply(quantized, kPitchUnitsPerOctave);
  quantized += int32_t(settings.octave) * kPitchUnitsPerOctave;

  const int32_t limit = 10 * kPitchUnitsPerOctave;
  if (quantized < -limit) quantized = -limit;
  if (quantized > limit) quantized = limit;

  // Re-read the degree after octave folding/transposition so the value and
  // note replies describe the same final pitch.
  quantizer.Requantize();
  quantizer.Process(
      quantized, int32_t(settings.root) * kPitchUnitsPerSemitone, 0);
  const int16_t note = int16_t(quantizer.GetLatestNoteNumber()) - 64;
  return {TxiQuantizedRaw(quantized), note};
}

}  // namespace txi2c2midi
