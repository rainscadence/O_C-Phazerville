#pragma once
#include <stdint.h>

namespace txi2c2midi {
enum class Mode : uint8_t { Single = 0, Legacy = 1 };
constexpr uint16_t kModeSettingsKey = 0x0900;
// Version1 record: version in bits 8..15, mode in bit 0; all other bits zero.
// Separate from Quadrants ii address/enable key0x0800.
class ModeState {
 public:
  void Boot(bool file_valid, bool present, uint64_t record) {
    fault_ = !file_valid || (present && record != 0x100 && record != 0x101);
    active_ = pending_ = saved_ = (!fault_ && present && (record & 1))
        ? Mode::Legacy : Mode::Single;
    saved_valid_ = !fault_;
  }
  bool Edit(Mode mode) {
    if (mode != Mode::Single && mode != Mode::Legacy) return false;
    pending_ = mode;
    return true;
  }
  void Cancel() { pending_ = saved_; }
  uint64_t PendingRecord() const { return 0x100 | uint64_t(pending_); }
  void Saved(bool success) {
    if (!success) return;
    saved_ = pending_;
    saved_valid_ = true;
  }
  Mode active() const { return active_; }
  Mode pending() const { return pending_; }
  bool faulted() const { return fault_; }
  bool dirty() const { return !saved_valid_ || pending_ != saved_; }
  bool reboot_required() const {
    return saved_valid_ && (fault_ || active_ != saved_);
  }
 private:
  Mode active_ = Mode::Single, pending_ = Mode::Single, saved_ = Mode::Single;
  bool fault_ = true, saved_valid_ = false;
};

// Main-loop only. Store owns atomic write/readback and preservation of all
// unrelated globals. This abstraction is not a production filesystem adapter.
template<class Store>
bool SaveMode(Store& store, ModeState& state) {
  const bool ok = store.SaveModeRecord(state.PendingRecord());
  state.Saved(ok);
  return ok;
}
}  // namespace txi2c2midi
