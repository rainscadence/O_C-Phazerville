#pragma once

#include <stdint.h>

#include "OctaveWindow.h"

namespace OC {

struct QuantizerSettings {
  int16_t scale = 0;
  int8_t root = 0;
  int8_t octave = 0;
  uint16_t mask = 0xffff;
  OctaveWindow range;
};

// Shared seam for an active app's Q1-Q8 bank. Validation lives here so local
// UI and future ii commands cannot acquire subtly different accepted domains.
class QuantizerBank {
 public:
  static constexpr unsigned kCount = 8;

  static bool Valid(unsigned slot, const QuantizerSettings& settings,
                    int scale_count) {
    if (slot >= kCount || settings.scale < 0 ||
        settings.scale >= scale_count || settings.root < 0 ||
        settings.root > 11 || settings.octave < -8 || settings.octave > 8 ||
        !settings.mask ||
        (settings.range.enabled() &&
         (settings.range.low() < OctaveWindow::kMinOctave ||
          settings.range.high() > OctaveWindow::kMaxOctave)))
      return false;
    return true;
  }
};

static_assert(sizeof(QuantizerSettings) == 8,
              "Q slot persistence layout changed");

}  // namespace OC
