#pragma once

#include <stddef.h>
#include <stdint.h>
#include "TxiRaw.h"

namespace txi2c2midi {

// Shared read-only monitor state. Callers provide the same interrupt lock used
// by the follower so Process(), foreground UI, and later command handlers see
// coherent records without making the renderer perform I/O or engine work.
template<class Lock>
class Telemetry {
 public:
  static constexpr unsigned kChannels = 8;

  struct TxiInput {
    int32_t raw = 0;          // OC IOFrame units: 10V == 4096.
    int32_t pitch = 0;        // OC pitch units: 1V == 1536.
    bool available = false;
  };

  struct TxiKnob {
    int16_t value = 0;
    bool available = false;
  };

  enum class ReadbackKind : uint8_t { Raw, Quantized, Note };
  struct TxiReadback {
    int16_t value = 0;
    ReadbackKind kind = ReadbackKind::Raw;
    bool available = false;
  };

  enum class TxoMode : uint8_t { Unavailable, Cv, Gate };
  struct TxoOutput {
    int32_t commanded = 0;
    int16_t millivolts = 0;
    uint16_t duration_ms = 0;
    uint32_t changed_at_ms = 0;
    TxoMode mode = TxoMode::Unavailable;
    bool gate_high = false;
  };

  void Clear() {
    Lock lock;
    for (unsigned i = 0; i < kChannels; ++i) {
      inputs_[i] = {};
      knobs_[i] = {};
      readbacks_[i] = {};
      outputs_[i] = {};
    }
  }

  // App resume invalidates live measurements and commanded-output state, but
  // virtual knobs are saved controls rather than bus observations.
  void ClearRuntime() {
    Lock lock;
    for (unsigned i = 0; i < kChannels; ++i) {
      inputs_[i] = {};
      readbacks_[i] = {};
      outputs_[i] = {};
    }
  }

  void CaptureInput(unsigned channel, int32_t raw, int32_t pitch) {
    if (channel >= kChannels) return;
    Lock lock;
    auto& input = inputs_[channel];
    input.raw = raw;
    input.pitch = pitch;
    input.available = true;
  }

  template<class RawValues, class PitchValues>
  void CaptureInputs(const RawValues& raw, const PitchValues& pitch,
                     unsigned count) {
    Lock lock;
    if (count > kChannels) count = kChannels;
    for (unsigned channel = 0; channel < count; ++channel) {
      inputs_[channel].raw = raw[channel];
      inputs_[channel].pitch = pitch[channel];
      inputs_[channel].available = true;
      readbacks_[channel] = {TxiRawValue(raw[channel]), ReadbackKind::Raw, true};
    }
  }

  void SetKnob(unsigned channel, int16_t value) {
    if (channel >= kChannels) return;
    Lock lock;
    knobs_[channel] = {value, true};
  }

  void SetReadback(unsigned channel, ReadbackKind kind, int16_t value) {
    if (channel >= kChannels) return;
    Lock lock;
    readbacks_[channel] = {value, kind, true};
  }

  void CommandCv(unsigned channel, int32_t commanded, int32_t millivolts,
                 uint32_t now, uint16_t duration_ms = 0) {
    if (channel >= kChannels) return;
    Lock lock;
    auto& output = outputs_[channel];
    output.commanded = commanded;
    if (millivolts < -32768) millivolts = -32768;
    if (millivolts > 32767) millivolts = 32767;
    output.millivolts = int16_t(millivolts);
    output.duration_ms = duration_ms;
    output.changed_at_ms = now;
    output.mode = TxoMode::Cv;
    output.gate_high = false;
  }

  void ConfigureOutput(unsigned channel, TxoMode mode, uint32_t now) {
    if (channel >= kChannels) return;
    Lock lock;
    outputs_[channel] = {};
    outputs_[channel].mode = mode;
    outputs_[channel].changed_at_ms = now;
  }

  void CommandGate(unsigned channel, bool high, uint16_t duration_ms,
                   uint32_t now) {
    if (channel >= kChannels) return;
    Lock lock;
    auto& output = outputs_[channel];
    output.commanded = high ? 1 : 0;
    output.millivolts = 0;
    output.duration_ms = duration_ms;
    output.changed_at_ms = now;
    output.mode = TxoMode::Gate;
    output.gate_high = high;
  }

  TxiInput Input(unsigned channel) const {
    Lock lock;
    return inputs_[Bound(channel)];
  }
  TxiKnob Knob(unsigned channel) const {
    Lock lock;
    return knobs_[Bound(channel)];
  }
  TxiReadback Readback(unsigned channel) const {
    Lock lock;
    return readbacks_[Bound(channel)];
  }
  TxoOutput Output(unsigned channel) const {
    Lock lock;
    return outputs_[Bound(channel)];
  }

 private:
  static unsigned Bound(unsigned channel) {
    return channel < kChannels ? channel : 0;
  }

  TxiInput inputs_[kChannels]{};
  TxiKnob knobs_[kChannels]{};
  TxiReadback readbacks_[kChannels]{};
  TxoOutput outputs_[kChannels]{};
};

} // namespace txi2c2midi
