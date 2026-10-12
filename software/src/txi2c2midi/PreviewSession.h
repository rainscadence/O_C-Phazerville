#pragma once

namespace txi2c2midi {
// Main-loop lifecycle only. The preview never claims a TX address. Preserve
// whether the previous follower existed, including an intentionally disabled one.
class PreviewSession {
 public:
  template <typename Bus> void Enter(Bus& bus) {
    if (entered_) return;
    restore_ = bus.Registered();
    bus.Stop();
    entered_ = true;
  }
  template <typename Bus> void Leave(Bus& bus) {
    if (!entered_) return;
    if (restore_) bus.Restore();
    entered_ = false;
    restore_ = false;
  }
 private:
  bool entered_ = false;
  bool restore_ = false;
};
}
