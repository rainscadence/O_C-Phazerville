#pragma once
#include "VirtualInputs.h"

namespace phz { namespace ii {
// Fixed-storage routing core. Caller supplies synchronization and lifecycle
// ownership; no hardware, filesystem, applet lookup or allocation occurs here.
class DirectionalMappings {
 public:
  static constexpr size_t kCount = VirtualInputs::kCount;
  enum class Direction : uint8_t { Off, Input, Output };
  struct Snapshot {
    Direction direction;
    VirtualInputs::Range range;
    uint16_t producer; // one-based shared output index; zero = unassigned
    bool valid;
    int32_t value; // native CV units, not a physical measurement
  };

  Snapshot Get(size_t index) const {
    if (index >= kCount) return {Direction::Off, VirtualInputs::Range::Off, 0, false, 0};
    const auto input = inputs_.Get(index);
    const auto& out = outputs_[index];
    const auto direction = directions_[index];
    return {direction, input.range, out.producer,
      direction == Direction::Input ? input.received :
      direction == Direction::Output ? out.valid : false,
      direction == Direction::Input ? input.value :
      direction == Direction::Output && out.valid ? out.value : 0};
  }

  // The routing owner must count ALL input bindings, not just visible applets.
  // No forced path: clear bindings through a separately confirmed transaction.
  bool SetDirection(size_t index, Direction direction, size_t input_users) {
    if (index >= kCount || unsigned(direction) > unsigned(Direction::Output)) return false;
    if (directions_[index] == direction) return true;
    if (input_users || outputs_[index].producer) return false;
    directions_[index] = direction;
    Clear(index);
    return true;
  }

  bool SetInputRange(size_t index, VirtualInputs::Range range) {
    if (index >= kCount || directions_[index] != Direction::Input ||
        range == VirtualInputs::Range::Off || unsigned(range) > 3) return false;
    return inputs_.SetRange(index, range);
  }

  // Explicit unbind (producer=0) is distinct from replacing another producer.
  bool AssignOutput(size_t index, uint16_t producer, uint16_t output_count) {
    if (index >= kCount || directions_[index] != Direction::Output ||
        producer > output_count) return false;
    auto& out = outputs_[index];
    if (producer && out.producer && producer != out.producer) return false;
    if (producer) for (size_t i = 0; i < kCount; ++i)
      if (i != index && outputs_[i].producer == producer) return false;
    if (producer != out.producer) { out.producer = producer; Clear(index); }
    return true;
  }

  bool Write(uint8_t command, uint16_t channel, uint16_t value) {
    if (!IsInput(channel)) return false;
    switch (command) {
      case 2: return inputs_.Write(channel, value);
      case 3: return inputs_.WriteVoltage(channel, value);
      case 4: return inputs_.WriteNormalized(channel, value);
      default: return false;
    }
  }

  // One applet output selector chooses one ii destination (0 = None).
  uint16_t OutputMappingFor(uint16_t producer) const {
    if (!producer) return 0;
    for (size_t i = 0; i < kCount; ++i)
      if (directions_[i] == Direction::Output && outputs_[i].producer == producer)
        return uint16_t(i + 1);
    return 0;
  }

  // Validate before removing the old route. Caller holds the lifecycle/IRQ lock
  // across the complete operation, so a callback cannot observe half a move.
  bool RouteOutput(uint16_t producer, uint16_t channel, uint16_t output_count) {
    if (!producer || producer > output_count || channel > kCount) return false;
    if (channel) {
      const auto& target = outputs_[channel - 1];
      if (directions_[channel - 1] != Direction::Output ||
          (target.producer && target.producer != producer)) return false;
    }
    for (size_t i = 0; i < kCount; ++i) {
      if (outputs_[i].producer == producer && i + 1 != channel) {
        outputs_[i].producer = 0;
        Clear(i);
      }
    }
    return !channel || AssignOutput(channel - 1, producer, output_count);
  }

  int32_t InputValue(size_t index) const {
    return index < kCount && directions_[index] == Direction::Input ? inputs_.Value(index) : 0;
  }

  // Publish a cached, post-processing applet output while its physical path
  // continues unchanged. An unrelated producer cannot overwrite this mapping.
  bool Publish(size_t index, uint16_t producer, int32_t value) {
    if (index >= kCount || directions_[index] != Direction::Output ||
        !producer || outputs_[index].producer != producer) return false;
    outputs_[index].value = value;
    outputs_[index].valid = true;
    return true;
  }

  // Returns two's-complement Teletype CV units (16384 = 10V), not a wire frame.
  // False leaves result untouched: invalid/off/unassigned must not masquerade
  // as a valid zero. The transport must define its own explicit validity reply.
  bool ReadVoltage(uint16_t channel, uint16_t& result) const {
    if (channel < 1 || channel > kCount) return false;
    const auto& out = outputs_[channel - 1];
    if (directions_[channel - 1] != Direction::Output || !out.producer || !out.valid) return false;
    const int64_t product = int64_t(out.value) * 16384;
    int64_t code = product < 0 ? -((-product + 7680) / 15360) : (product + 7680) / 15360;
    if (code < -32768) code = -32768;
    if (code > 32767) code = 32767;
    result = static_cast<uint16_t>(code < 0 ? code + 65536 : code);
    return true;
  }

  void ClearValues() {
    inputs_.ClearValues();
    for (size_t i = 0; i < kCount; ++i) { outputs_[i].valid = false; outputs_[i].value = 0; }
  }

  // Legacy 0..3 retains exactly its old meaning. The new tagged record also
  // saves direction and producer, NEVER live values. Failures are non-mutating.
  bool Restore(size_t index, uint64_t record, uint16_t output_count) {
    if (index >= kCount) return false;
    Direction direction;
    VirtualInputs::Range range;
    uint16_t producer = 0;
    if (record <= 3) {
      direction = record == 0 ? Direction::Off : Direction::Input;
      range = record == 0 ? VirtualInputs::Range::Unipolar5 : VirtualInputs::Range(record);
    } else {
      // 32..47: magic 0x4949, 24..31: version 1, 8..23: producer,
      // 4..7: direction, 0..3: range. Every other bit must be zero.
      if ((record >> 32) != 0x4949 || ((record >> 24) & 0xff) != 1) return false;
      const auto d = unsigned((record >> 4) & 15);
      const auto r = unsigned(record & 15);
      producer = uint16_t((record >> 8) & 0xffff);
      if (d > 2 || r < 1 || r > 3 || producer > output_count || (d != 2 && producer)) return false;
      direction = Direction(d); range = VirtualInputs::Range(r);
    }
    directions_[index] = direction;
    inputs_.SetRange(index, range);
    outputs_[index].producer = producer;
    Clear(index);
    return true;
  }

  bool Record(size_t index, uint64_t& result) const {
    if (index >= kCount) return false;
    const auto s = Get(index);
    result = (uint64_t(0x4949) << 32) | (uint64_t(1) << 24) |
      (uint64_t(s.producer) << 8) | (uint64_t(s.direction) << 4) | uint64_t(s.range);
    return true;
  }

  DirectionalMappings() {
    for (size_t i = 0; i < kCount; ++i) directions_[i] = Direction::Input;
  }

  // Bridge the validated legacy preset staging path without re-quantizing values.
  explicit DirectionalMappings(const VirtualInputs& source) : inputs_(source) {
    for (size_t i = 0; i < kCount; ++i) {
      directions_[i] = source.Get(i).range == VirtualInputs::Range::Off ? Direction::Off : Direction::Input;
      // Off is a direction, not a disabled retained input range in this bank.
      if (directions_[i] == Direction::Off)
        inputs_.SetRange(i, VirtualInputs::Range::Unipolar5);
    }
  }

 protected:
  VirtualInputs::Snapshot LegacySnapshot(size_t index) const { return inputs_.Get(index); }

 private:
  struct Output { uint16_t producer = 0; bool valid = false; int32_t value = 0; };
  VirtualInputs inputs_;
  Direction directions_[kCount];
  Output outputs_[kCount];
  bool IsInput(uint16_t channel) const {
    return channel >= 1 && channel <= kCount && directions_[channel - 1] == Direction::Input;
  }
  void Clear(size_t index) {
    // Preserve range while resetting this input without changing other slots.
    const auto range = inputs_.Get(index).range;
    inputs_.SetRange(index, VirtualInputs::Range::Off);
    inputs_.SetRange(index, range);
    outputs_[index].valid = false;
    outputs_[index].value = 0;
  }
};

// Whole-bank staging: no partial mutation when any preset record is invalid.
template<class Read>
bool LoadDirectionalMappings(Read read, uint16_t output_count, DirectionalMappings& destination) {
  DirectionalMappings staged;
  for (size_t i = 0; i < DirectionalMappings::kCount; ++i) {
    uint64_t record = 0;
    if (read(i, record) && !staged.Restore(i, record, output_count)) return false;
    const auto producer = staged.Get(i).producer;
    if (producer) for (size_t j = 0; j < i; ++j)
      if (staged.Get(j).producer == producer) return false;
  }
  destination = staged;
  return true;
}
}}
