#include "AddressState.h"

namespace phz { namespace ii {
void AddressState::Boot(bool file_valid, bool present, uint64_t record) {
  const AddressConfig config = DecodeAddress(file_valid, present, record);
  if (config.status != AddressStatus::DisabledFault) {
    active_ = config.address;
    pending_ = config.address;
    saved_ = config.address;
    enabled_ = pending_enabled_ = saved_enabled_ = config.status == AddressStatus::Ready;
    faulted_ = false;
    saved_valid_ = true;
  } else {
    active_ = 0;
    pending_ = kDefaultAddress;
    saved_ = 0;
    enabled_ = false;
    pending_enabled_ = saved_enabled_ = false;
    faulted_ = true;
    saved_valid_ = false;
  }
}

void AddressState::FactoryDefaults() {
  active_ = kDefaultAddress;
  pending_ = kDefaultAddress;
  saved_ = kDefaultAddress;
  enabled_ = pending_enabled_ = saved_enabled_ = false;
  faulted_ = false;
  saved_valid_ = true;
}

bool AddressState::Edit(int address) {
  if (address < 8 || address > 119) return false;
  pending_ = static_cast<uint8_t>(address);
  return true;
}

bool AddressState::PendingRecord(uint64_t& record) const {
  return EncodeSettings(pending_, pending_enabled_, record);
}

void AddressState::Saved(bool success) {
  if (!success) return;
  saved_ = pending_;
  saved_enabled_ = pending_enabled_;
  saved_valid_ = true;
}
}} // namespace phz::ii
