#include <cassert>
#include "../src/txi2c2midi/AudioState.h"

int main() {
  txi2c2midi::AudioState state;
  state.Reset();
  for (unsigned voice = 0; voice < 2; ++voice) {
    assert(!state.Voice(voice).enabled);
    assert(state.Voice(voice).level_percent == 25);
    assert(state.Voice(voice).frequency_millihz == 440000);
  }
  assert(!state.SetFrequency(0, 99));
  assert(state.SetFrequency(0, 100));
  assert(state.SetFrequency(0, 20000000));
  assert(!state.SetFrequency(0, 20000001));
  assert(state.SetWaveform(1, 5));
  assert(!state.SetWaveform(1, 6));
  assert(state.SetLevel(1, 100));
  assert(!state.SetLevel(1, 101));
  state.Voice(0).enabled = state.Voice(1).enabled = true;
  state.Silence();
  assert(!state.Voice(0).enabled && !state.Voice(1).enabled);
}
