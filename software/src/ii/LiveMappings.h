#pragma once
#include "DirectionalMappings.h"

namespace phz { namespace ii {
// Qwen-drafted adapter; supervisor corrected overload hiding, Off handling and
// private-state access. This preserves the legacy INPUT-only surface.
class LiveMappings : public DirectionalMappings {
 public:
  LiveMappings() = default;
  LiveMappings& operator=(const DirectionalMappings& source) {
    static_cast<DirectionalMappings&>(*this) = source;
    return *this;
  }
  explicit LiveMappings(const VirtualInputs& source) : DirectionalMappings(source) {}
  LiveMappings& operator=(const VirtualInputs& source) {
    *this = LiveMappings(source);
    return *this;
  }
  VirtualInputs::Snapshot Get(size_t index) const {
    if (index >= kCount || DirectionalMappings::Get(index).direction != Direction::Input)
      return {VirtualInputs::Range::Off, 0, false, 0};
    return LegacySnapshot(index);
  }
  int32_t Value(size_t index) const { return InputValue(index); }
  bool Write(uint16_t channel, uint16_t code) { return DirectionalMappings::Write(2, channel, code); }
  bool WriteVoltage(uint16_t channel, uint16_t code) { return DirectionalMappings::Write(3, channel, code); }
  bool WriteNormalized(uint16_t channel, uint16_t code) { return DirectionalMappings::Write(4, channel, code); }
  void Reset() { *this = LiveMappings{}; }

  // Legacy input editor only; new Output assignments cannot be disabled here.
  // Preserves the existing ability to disable an input without clearing its
  // source bindings. The future direction UI must use confirmed binding edits.
  bool SetRange(size_t index, VirtualInputs::Range range) {
    if (index >= kCount || unsigned(range) > 3) return false;
    const auto direction = DirectionalMappings::Get(index).direction;
    if (direction == Direction::Output) return false;
    if (range == VirtualInputs::Range::Off) return SetDirection(index, Direction::Off, 0);
    if (direction == Direction::Off && !SetDirection(index, Direction::Input, 0)) return false;
    return SetInputRange(index, range);
  }
};
}}
