#pragma once
#include "DirectionalMappings.h"
#include "VirtualInputPreset.h"

namespace phz { namespace ii {
constexpr uint16_t kIiUsageKey = 72; // after map records 40..71
inline uint64_t IiUsageRecord(uint32_t mask) {
  return (UINT64_C(0x4955) << 48) | (UINT64_C(1) << 32) | mask;
}
// Writer-generated, CRC-protected evidence, not an independent blob decoder.
template<class Read>
bool ValidateIiUsage(Read read, const DirectionalMappings& bank) {
  uint32_t outputs = 0;
  for (size_t i = 0; i < DirectionalMappings::kCount; ++i)
    if (bank.Get(i).direction == DirectionalMappings::Direction::Output)
      outputs |= uint32_t(1) << i;
  if (!outputs) return true; // legacy input-only presets need no new record
  uint64_t record = 0;
  return read(kIiUsageKey, record) && (record >> 32) == UINT64_C(0x49550001) &&
    !(uint32_t(record) & outputs);
}
// Qwen-drafted compatibility helpers; reviewed and made header-safe/C++11.
inline bool PresetMappingRecord(const DirectionalMappings& bank, size_t index,
                               uint64_t& out) {
  if (index >= DirectionalMappings::kCount) return false;
  const auto snap = bank.Get(index);
  switch (snap.direction) {
    case DirectionalMappings::Direction::Off: out = 0; return true;
    case DirectionalMappings::Direction::Input:
      out = static_cast<uint64_t>(snap.range); return true;
    case DirectionalMappings::Direction::Output: return bank.Record(index, out);
  }
  return false;
}

inline bool ValidPresetCvSource(const DirectionalMappings& bank, uint8_t source) {
  if ((source & 0xe0) != 0xc0) return true;
  // Disabled legacy Inputs may retain bindings; Outputs must never be inputs.
  return bank.Get(source & 31).direction != DirectionalMappings::Direction::Output;
}

// Digital records pack two 32-bit maps. Missing pages retain live mappings;
// old-format trigger records predate ii sources entirely.
template<class Read, class Map, size_t N>
bool ValidateDigitalPreset(Read read, uint16_t key, uint16_t old_key,
                           const Map (&current)[N], const DirectionalMappings& bank) {
  static_assert(N % 2 == 0, "digital maps pack in pairs");
  uint64_t record=0;
  bool present=read(key,record);
  if (!present && read(old_key,record)) return true;
  for(size_t page=0;page<N/2;++page) {
    if(page && present) present=read(key+page,record);
    for(size_t j=0;j<2;++j) {
      const uint8_t source=present ? uint8_t(record>>(32*j)) : uint8_t(current[page*2+j].Pack());
      if(!ValidPresetCvSource(bank,source)) return false;
    }
  }
  return true;
}

// Stage both banks before any applet is changed. Missing CV pages preserve the
// existing prefix-load semantics; validate retained bindings too, not just pages
// present in the file. Map::Unpack performs the actual legacy source migration.
template<class Read, class Map, size_t N>
bool StageDirectionalPreset(Read read, uint16_t cv_key, uint16_t output_count,
                            const Map (&current)[N], Map (&destination)[N],
                            DirectionalMappings& bank) {
  static_assert(N % 4 == 0, "CV records pack four maps");
  DirectionalMappings staged;
  if (!LoadDirectionalMappings([&](size_t index, uint64_t& value) {
        return read(kVirtualRangeKey + index, value);
      }, output_count, staged)) return false;
  if (!ValidateIiUsage(read, staged)) return false;
  Map maps[N];
  for (size_t i = 0; i < N; ++i) maps[i] = current[i];
  for (size_t page = 0; page < N / 4; ++page) {
    uint64_t record = 0;
    if (!read(cv_key + page, record)) break;
    for (size_t j = 0; j < 4; ++j)
      maps[page * 4 + j].Unpack(uint16_t(record >> (j * 16)));
  }
  for (size_t i = 0; i < N; ++i)
    if (!ValidPresetCvSource(staged, uint8_t(maps[i].Pack()))) return false;
  bank = staged;
  for (size_t i = 0; i < N; ++i) destination[i] = maps[i];
  return true;
}
}}
