#include <Arduino.h>

#include "MidiPitch.h"

namespace txi2c2midi {

#if defined(ARDUINO_TEENSY41)
DMAMEM MidiPitch midi_pitch;
#endif

}  // namespace txi2c2midi
