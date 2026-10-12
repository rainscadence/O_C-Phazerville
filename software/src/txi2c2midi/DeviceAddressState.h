#pragma once
#include <stdint.h>

namespace txi2c2midi {
// Qwen draft reviewed/corrected by Codex: fail-closed initialization,
// saved-valid/fault semantics, strict record validation and separate tests.
class DeviceAddressState {
 public:
  static bool ValidAddress(int address) {
    return address == 0 || (address >= 8 && address <= 0x77);
  }
  void Boot(bool valid_file, bool present, uint64_t record) {
    fault_ = !valid_file || (present &&
        ((record >> 8) != 1 || !ValidAddress(int(record & 0xff))));
    active_ = pending_ = saved_ = (!fault_ && present) ? uint8_t(record) : 0;
    saved_valid_ = !fault_;
  }
  bool Edit(int address) {
    if (!ValidAddress(address)) return false;
    pending_ = uint8_t(address);
    return true;
  }
  void Cancel() { pending_ = saved_; }
  uint64_t PendingRecord() const { return uint64_t{0x100} | pending_; }
  void Saved(bool success) {
    if (!success) return;
    saved_ = pending_;
    saved_valid_ = true;
  }
  uint8_t active() const { return active_; }
  uint8_t pending() const { return pending_; }
  bool faulted() const { return fault_; }
  bool dirty() const { return !saved_valid_ || pending_ != saved_; }
  bool reboot_required() const {
    return saved_valid_ && (fault_ || active_ != saved_);
  }
 private:
  uint8_t active_ = 0, pending_ = 0, saved_ = 0;
  bool fault_ = true, saved_valid_ = false;
};
}
