#pragma once
#include "DeviceAddressState.h"
#include "../config/Store.h"

namespace txi2c2midi {
constexpr uint16_t kDeviceAddressKey = 0x0901;
// An independent snapshot preserves Quadrants and all musical bank settings.
template<class Fs>
bool SaveDeviceAddress(Fs& fs, const char* filename, DeviceAddressState& state) {
  phz::config::Snapshot globals;
  if (phz::config::Load(fs, filename, globals) == phz::config::LoadResult::Invalid)
    return false;
  const auto metadata = globals.values.find(0x0100);
  if (metadata == globals.values.end() ||
      !(metadata->second & (uint64_t{1} << 17))) return false;
  globals.values[kDeviceAddressKey] = state.PendingRecord();
  const bool saved = phz::config::Save(fs, filename, globals);
  state.Saved(saved);
  return saved;
}
}
