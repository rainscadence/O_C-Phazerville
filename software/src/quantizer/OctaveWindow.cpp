#include "OctaveWindow.h"

namespace OC {

__attribute__((noinline)) int32_t OctaveWindow::Apply(
    int32_t pitch, int32_t octave_size) const {
  if (!enabled() || octave_size <= 0) return pitch;

  int32_t octave = pitch / octave_size;
  if (pitch < 0 && pitch % octave_size) --octave;
  if (octave < low_)
    pitch += (int32_t(low_) - octave) * octave_size;
  else if (octave > high_)
    pitch += (int32_t(high_) - octave) * octave_size;
  return pitch;
}

}  // namespace OC
