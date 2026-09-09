#pragma once
#include "AddressState.h"
#include "../config/Store.h"

namespace phz { namespace ii {
// Explicit address-only save. This independent context never replaces the
// caller's globals/bank maps and never promotes a missing/invalid whole file
// to factory defaults. Repairing an invalid address in valid globals is allowed.
template<class Fs>
bool SaveAddress(Fs& fs, const char* filename, AddressState& state) {
  config::Snapshot globals;
  if (config::Load(fs, filename, globals) == config::LoadResult::Invalid)
    return false;
  const auto metadata = globals.values.find(0x0100);
  if (metadata == globals.values.end() ||
      !(metadata->second & (uint64_t{1} << 17))) return false;
  uint64_t record = 0;
  if (!state.PendingRecord(record)) return false;
  globals.values[kSettingsKey] = record;
  const bool saved = config::Save(fs, filename, globals);
  state.Saved(saved);
  return saved;
}
}} // namespace phz::ii
