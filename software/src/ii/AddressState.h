#pragma once
#include "Protocol.h"

namespace phz { namespace ii {
// Boot is the only operation which changes active. Editing and saving never
// rebind the bus. A failed boot stays disabled until a successful reboot.
class AddressState {
 public:
  void Boot(bool file_valid, bool present, uint64_t record);
  void FactoryDefaults(); // explicit user-confirmed reset, before registration
  bool Edit(int address); // reject, do not clamp invalid inputs
  void EditEnabled(bool enabled) { pending_enabled_ = enabled; }
  bool PendingRecord(uint64_t& record) const;
  void Saved(bool success); // failure must preserve dirty / saved state
  uint8_t active() const { return active_; }
  uint8_t pending() const { return pending_; }
  bool enabled() const { return enabled_; }
  bool pending_enabled() const { return pending_enabled_; }
  bool faulted() const { return faulted_; }
  bool dirty() const {
    return pending_ != saved_ || pending_enabled_ != saved_enabled_ || !saved_valid_;
  }
  bool reboot_required() const {
    return saved_valid_ && (faulted_ || enabled_ != saved_enabled_ || active_ != saved_);
  }
 private:
  uint8_t active_ = 0;
  uint8_t pending_ = kDefaultAddress;
  uint8_t saved_ = 0;
  bool enabled_ = false;
  bool pending_enabled_ = false;
  bool saved_enabled_ = false;
  bool faulted_ = true;
  bool saved_valid_ = false;
};
}} // namespace phz::ii
