#pragma once
#include <stdint.h>

namespace txi2c2midi {
// Main-loop-only observation of calls to the USB output adapter. This is not
// an acknowledgement of receipt by a connected host. No MIDI RX is wired here.
class MidiMonitor {
 public:
  struct Channel {
    uint8_t note = 0, velocity = 0, active = 0;
    bool seen = false, on = false;
  };
  void Clear() { for (auto& c : channels_) c = {}; last_ = -1; }
  void Record(uint8_t ch, uint8_t note, uint8_t velocity, bool on) {
    if (ch >= 16 || note > 127 || velocity > 127) return;
    auto& c = channels_[ch];
    c.seen = true; c.on = on; c.note = note; c.velocity = velocity;
    if (on) { if (c.active < 32) ++c.active; }
    else if (c.active) --c.active;
    last_ = ch;
  }
  int Last() const { return last_; }
  const Channel& Get(unsigned ch) const { return channels_[ch < 16 ? ch : 0]; }
 private:
  Channel channels_[16];
  int8_t last_ = -1;
};
}
