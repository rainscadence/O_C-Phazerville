#pragma once
#include "LiveMappings.h"

namespace phz { namespace ii {
// One shared signal bank. Target mutations use InterruptLock; readers execute
// in the control ISR or take the same lock in the UI. Values are never saved.
inline LiveMappings virtual_inputs;
// Qwen-drafted predicate; use the directional interface, not legacy Get().
inline bool SelectableIiInput(const DirectionalMappings& bank, uint8_t source) {
  return (source & 0xe0) != 0xc0 ||
    bank.Get(source & 31).direction == DirectionalMappings::Direction::Input;
}
}} // namespace phz::ii
