#pragma once
#include "AddressState.h"
#include "../config/Store.h"
#include "../txi2c2midi/ModeState.h"

namespace phz { namespace ii {
// Explicit address save, optionally with TX mode in the same atomic snapshot.
// This independent context never replaces the
// caller's globals/bank maps and never promotes a missing/invalid whole file
// to factory defaults. Repairing an invalid address in valid globals is allowed.
template<class Fs>
bool SaveAddress(Fs& fs, const char* filename, AddressState& state,
                 txi2c2midi::ModeState* mode = nullptr,
                 uint16_t extra_key = 0, uint64_t extra_value = 0) {
  config::Snapshot globals;
  if (config::Load(fs, filename, globals) == config::LoadResult::Invalid)
    return false;
  const auto metadata = globals.values.find(0x0100);
  if (metadata == globals.values.end() ||
      !(metadata->second & (uint64_t{1} << 17))) return false;
  uint64_t record = 0;
  if (!state.PendingRecord(record)) return false;
  globals.values[kSettingsKey] = record;
  if (mode) globals.values[txi2c2midi::kModeSettingsKey] = mode->PendingRecord();
  if (extra_key) globals.values[extra_key] = extra_value;
  const bool saved = config::Save(fs, filename, globals);
  state.Saved(saved);
  if (mode) mode->Saved(saved);
  return saved;
}
}} // namespace phz::ii
