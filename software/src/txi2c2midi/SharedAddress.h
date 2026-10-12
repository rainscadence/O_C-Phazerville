#pragma once
#include "../ii/AddressState.h"
#include "DeviceAddressState.h"
#include "ModeState.h"

namespace txi2c2midi {
// Only boot-active global settings may register the normal-mode listener.
// Zero means no listener; pending edits never alter the live bus.
inline uint8_t SingleAddress(const phz::ii::AddressState& address,
                             const ModeState& mode, bool hardware_supported) {
  return hardware_supported && !address.faulted() && address.enabled() &&
      !mode.faulted() && mode.active() == Mode::Single ? address.active() : 0;
}

// Read-only migration: retain the obsolete record on disk for recovery, never
// fall back to it. An invalid legacy record cannot poison valid global settings.
inline bool LegacyAddressIgnored(const phz::ii::AddressState& address,
                                 bool present, uint64_t record) {
  if (!present) return false;
  DeviceAddressState legacy;
  legacy.Boot(true, true, record);
  return legacy.faulted() || address.faulted() ||
      legacy.active() != (address.enabled() ? address.active() : 0);
}
}
