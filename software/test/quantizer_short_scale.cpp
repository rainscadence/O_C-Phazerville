#include <cassert>

#include "../src/src/extern/braids_quantizer.h"

int main() {
  constexpr int octave = 12 << 7;
  braids::Scale one_note = {octave, 1, {0}};
  braids::Quantizer quantizer;
  quantizer.Init();
  quantizer.Configure(one_note);

  assert(quantizer.enabled());
  assert(quantizer.Lookup(64) == 0);
  assert(quantizer.Lookup(65) == octave);
  assert(quantizer.Process(0) == 0);
  quantizer.Requantize();
  assert(quantizer.Process(octave + 300) == octave);
  quantizer.Requantize();
  assert(quantizer.Process(-300) == 0);
}
