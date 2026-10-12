#include <cassert>
#include <cmath>

#include "../src/txi2c2midi/AudioPitch.h"
#include "../src/txi2c2midi/TxiQuantizer.h"

int main() {
  constexpr int semitone = txi2c2midi::kPitchUnitsPerSemitone;
  constexpr int octave = txi2c2midi::kPitchUnitsPerOctave;
  const braids::Scale major = {
      octave, 7, {0, 2 * semitone, 4 * semitone, 5 * semitone,
                  7 * semitone, 9 * semitone, 11 * semitone}};
  OC::QuantizerSettings settings;
  settings.scale = 1;

  // A pitch clearly above the A/B midpoint snaps to B.
  assert(txi2c2midi::QuantizeAudioPitch(
      settings, major, 10 * semitone + 80) == 11 * semitone);

  // The octave window folds first; the fixed octave transpose applies last.
  assert(settings.range.Set(0, 0));
  settings.octave = 1;
  assert(txi2c2midi::QuantizeAudioPitch(
      settings, major, 2 * octave + 7 * semitone) ==
      octave + 7 * semitone);

  // A-sharp 5 first snaps to A5, folds into octave zero as A4, then the
  // fixed +1 octave transpose returns A5. The fixed transpose must not be
  // applied before or during range folding.
  assert(txi2c2midi::QuantizeAudioPitch(
      settings, major, octave + 10 * semitone) == octave + 9 * semitone);

  const float a4 = txi2c2midi::AudioPitchFrequency(9 * semitone);
  assert(std::fabs(a4 - 440.0f) < 0.01f);

  // Audio quantization retains the Braids scale's 1/128-semitone precision;
  // it must not round a microtonal result to an ordinary MIDI note.
  const braids::Scale microtonal = {
      octave, 2, {0, semitone + semitone / 2}};
  OC::QuantizerSettings microtonal_settings;
  microtonal_settings.scale = 1;
  const int32_t microtonal_pitch = txi2c2midi::QuantizeAudioPitch(
      microtonal_settings, microtonal, 2 * semitone);
  assert(microtonal_pitch == semitone + semitone / 2);
  const float expected_microtonal =
      261.625565f * std::pow(2.0f, 1.5f / 12.0f);
  assert(std::fabs(txi2c2midi::AudioPitchFrequency(microtonal_pitch) -
                   expected_microtonal) < 0.01f);

  // TXI quantized and note selectors use the shared Q-slot settings while
  // retaining native TXI scale-degree note numbering rather than MIDI notes.
  OC::QuantizerSettings txi;
  txi.scale = 1;
  const auto d = txi2c2midi::QuantizeTxiPitch(
      txi, major, txi2c2midi::TxiKnobPitch(274));
  assert(d.value == 273 && d.note == 1);
  const auto negative = txi2c2midi::QuantizeTxiPitch(
      txi, major, -2 * semitone);
  assert(negative.value == 273 && negative.note == 1);

  txi.root = 1;
  txi.octave = 1;
  assert(txi.range.Set(0, 0));
  const auto rooted = txi2c2midi::QuantizeTxiPitch(
      txi, major, 3 * octave + semitone);
  assert(rooted.value == 1774 && rooted.note == 7);

  OC::QuantizerSettings txi_micro;
  const auto micro = txi2c2midi::QuantizeTxiPitch(
      txi_micro, microtonal, 2 * semitone);
  assert(micro.value == 204 && micro.note == 1);
}
