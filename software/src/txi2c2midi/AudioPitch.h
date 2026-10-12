#pragma once

#include <math.h>
#include <stdint.h>

#include "../quantizer/QuantizerBank.h"
#include "../src/extern/braids_quantizer.h"

namespace txi2c2midi {

static constexpr int32_t kPitchUnitsPerSemitone = 128;
static constexpr int32_t kPitchUnitsPerOctave = 12 * kPitchUnitsPerSemitone;
static constexpr int32_t kMidiC4 = 60;

inline int32_t QuantizeAudioPitch(const OC::QuantizerSettings& settings,
                                  const braids::Scale& scale,
                                  int32_t pitch) {
  braids::Quantizer quantizer;
  quantizer.Init();
  quantizer.Configure(scale, settings.mask);
  int32_t result = quantizer.Process(
      pitch, int32_t(settings.root) * kPitchUnitsPerSemitone, 0);
  result = settings.range.Apply(result, kPitchUnitsPerOctave);
  return result + int32_t(settings.octave) * kPitchUnitsPerOctave;
}

inline float AudioPitchFrequency(int32_t pitch) {
  // Audio pitch zero is C4, matching 0 V as the neutral Q-slot pitch.
  return 261.625565f * exp2f(float(pitch) / float(kPitchUnitsPerOctave));
}

}  // namespace txi2c2midi
