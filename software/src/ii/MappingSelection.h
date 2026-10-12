#pragma once
#include "DirectionalMappings.h"

namespace phz { namespace ii {
// Qwen draft, supervisor-corrected: validate context before None; advance before
// testing; use the actual bank interface. Pure selection, no state mutation.
inline bool CanSelectMapping(const DirectionalMappings& bank, uint16_t channel,
                            DirectionalMappings::Direction role, uint16_t producer = 0) {
  using Direction = DirectionalMappings::Direction;
  if ((role != Direction::Input && role != Direction::Output) ||
      (role == Direction::Input && producer != 0) ||
      (role == Direction::Output && producer == 0)) return false;
  if (channel == 0) return true;
  if (channel > DirectionalMappings::kCount) return false;
  const auto s = bank.Get(channel - 1);
  return s.direction == role &&
    (role == Direction::Input || s.producer == 0 || s.producer == producer);
}

inline uint16_t NextMapping(const DirectionalMappings& bank, uint16_t current,
                           int direction, DirectionalMappings::Direction role,
                           uint16_t producer = 0) {
  if (current > DirectionalMappings::kCount || !CanSelectMapping(bank, 0, role, producer)) return 0;
  if (direction == 0) return CanSelectMapping(bank, current, role, producer) ? current : 0;
  uint16_t candidate = current;
  for (size_t attempt = 0; attempt <= DirectionalMappings::kCount; ++attempt) {
    if (direction > 0) candidate = candidate == DirectionalMappings::kCount ? 0 : candidate + 1;
    else candidate = candidate == 0 ? DirectionalMappings::kCount : candidate - 1;
    if (CanSelectMapping(bank, candidate, role, producer)) return candidate;
  }
  return 0;
}
}}
