#include <cassert>
#include "../src/txi2c2midi/QuantizerSlots.h"

struct Source {
  int scale;
  int root_note;
  int octave;
  uint16_t mask;
  OC::OctaveWindow range;
};

int main() {
  txi2c2midi::QuantizerSlots slots;
  slots.Reset(3);
  assert(slots.Get(0).scale == 3 && slots.Get(7).mask == 0xffff);
  assert(!slots.Set(0, 1, 0, 0, 0, 20));
  assert(!slots.Set(0, 20, 0, 0, 1, 20));
  assert(slots.Set(0, 4, 11, -2, 0x555, 20));
  Source source[8]{};
  for (int i = 0; i < 8; ++i) {
    source[i].scale = i;
    source[i].root_note = i % 12;
    source[i].octave = i - 4;
    source[i].mask = uint16_t(1u << i);
  }
  assert(source[3].range.Set(-2, 3));
  slots.Import(source);
  assert(slots.Get(7).scale == 7 && slots.Get(7).octave == 3);
  assert(slots.Get(3).range.enabled());
  assert(slots.Get(3).range.low() == -2 && slots.Get(3).range.high() == 3);
  assert(slots.SetRange(2, -1, 2));
  assert(!slots.SetRange(2, 3, 2));
  assert(slots.DisableRange(2) && !slots.Get(2).range.enabled());

  OC::QuantizerSettings replacement;
  replacement.scale = 4;
  replacement.root = 3;
  replacement.octave = 2;
  replacement.mask = 0x55;
  assert(replacement.range.Set(-1, 3));
  assert(slots.Replace(1, replacement, 20));
  OC::QuantizerSettings readback;
  assert(slots.Read(1, readback));
  assert(readback.scale == 4 && readback.root == 3 && readback.octave == 2);
  replacement.mask = 0;
  assert(!slots.Replace(1, replacement, 20));
  replacement.mask = 1;
  replacement.root = 12;
  assert(!slots.Replace(1, replacement, 20));
}
