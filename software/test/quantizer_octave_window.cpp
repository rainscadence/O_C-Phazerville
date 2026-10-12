#include <cassert>

#include "../src/quantizer/OctaveWindow.h"

int main() {
  constexpr int octave = 12 << 7;
  OC::OctaveWindow range;

  assert(!range.enabled());
  assert(range.Apply(7 * octave + 321, octave) == 7 * octave + 321);
  assert(!range.Set(3, 2));
  assert(range.Set(-1, 1));

  // Notes are moved only by whole octaves, including pitches below zero.
  assert(range.Apply(4 * octave + 321, octave) == octave + 321);
  assert(range.Apply(-4 * octave + 321, octave) == -octave + 321);
  assert(range.Apply(-321, octave) == -321);
  assert(range.Set(0, 1));
  assert(range.Apply(-321, octave) == octave - 321);
  assert(range.Apply(octave + 321, octave) == octave + 321);

  range.Disable();
  range.AdjustLow(-2);
  assert(range.enabled() && range.low() == -2 && range.high() == 4);
  range.AdjustHigh(-20);
  assert(range.high() == range.low());
}
