#pragma once

#include <stddef.h>
#include <stdint.h>

namespace txi2c2midi {

// Bounded TXO command engine. Receive() is safe for the Wire ISR: it only
// validates and copies a fixed-size command. Service() performs state changes.
template<class Lock>
class TxoEngine {
 public:
  static constexpr unsigned kChannels = 8;
  static constexpr uint8_t kDefaultCvMask = 0xf0;

  template<class Monitor>
  void Configure(uint8_t cv_mask, Monitor& monitor, uint32_t now) {
    {
      Lock lock;
      cv_mask_ = cv_mask;
      head_ = tail_ = count_ = 0;
      for (auto& output : outputs_) output = DefaultOutput();
      for (auto& value : rendered_) value = 0;
    }
    for (unsigned channel = 0; channel < kChannels; ++channel)
      monitor.ConfigureOutput(channel,
          IsCv(channel) ? Monitor::TxoMode::Cv : Monitor::TxoMode::Gate, now);
  }

  void Clear() {
    Lock lock;
    head_ = tail_ = count_ = 0;
    for (auto& output : outputs_) output = DefaultOutput();
    for (auto& value : rendered_) value = 0;
  }

  template<class Monitor>
  void SetMode(unsigned channel, bool cv, Monitor& monitor, uint32_t now) {
    if (channel >= kChannels) return;
    {
      Lock lock;
      if (cv) cv_mask_ |= uint8_t(1) << channel;
      else cv_mask_ &= ~(uint8_t(1) << channel);
      outputs_[channel] = DefaultOutput();
      rendered_[channel] = 0;
      RemoveQueued(channel);
    }
    monitor.ConfigureOutput(channel,
        cv ? Monitor::TxoMode::Cv : Monitor::TxoMode::Gate, now);
  }

  uint8_t CvMask() const {
    Lock lock;
    return cv_mask_;
  }

  bool IsCv(unsigned channel) const {
    if (channel >= kChannels) return false;
    Lock lock;
    return (cv_mask_ & (uint8_t(1) << channel)) != 0;
  }

#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  bool Receive(const uint8_t* frame, size_t size) {
    Command parsed{};
    if (!Decode(frame,size,parsed)) return false;

    Lock lock;
    if (!Compatible(parsed) || count_ >= kQueueDepth)
      return false;
    auto& command = queue_[tail_];
    command = parsed;
    tail_ = uint8_t((tail_ + 1) % kQueueDepth);
    ++count_;
    return true;
  }

  bool Accepts(const uint8_t* frame, size_t size) const {
    Command parsed{};
    if (!Decode(frame,size,parsed)) return false;
    Lock lock;
    return Compatible(parsed);
  }

  template<class Monitor>
  void Service(Monitor& monitor, uint32_t now) {
    Command command{};
    bool has_command = false;
    bool kill_command = false;
    uint8_t expired = 0;
    uint8_t publish_cv_mask = 0;
    unsigned kill_base = 0;
    Output killed[4]{};
    Output published{};
    {
      Lock lock;
      for (unsigned channel = 0; channel < kChannels; ++channel) {
        auto& output = outputs_[channel];
        if (output.pulse_active && Reached(now, output.pulse_deadline)) {
          output.pulse_active = false;
          output.gate_high = false;
          expired |= uint8_t(1) << channel;
        }
      }
      if (count_) {
        has_command = true;
        command = queue_[head_];
        head_ = uint8_t((head_ + 1) % kQueueDepth);
        --count_;
        auto& output = outputs_[command.channel];
        if (command.opcode == 0x20) {
          kill_command = true;
          kill_base = command.channel & 4u;
          publish_cv_mask = cv_mask_;
          for (unsigned i = 0; i < 4; ++i) {
            auto& killed_output = outputs_[kill_base + i];
            killed_output.gate_high = false;
            killed_output.pulse_active = false;
            killed_output.cv_start = killed_output.cv_target;
            killed_output.cv_slewing = false;
            killed[i] = killed_output;
          }
        }
        switch (command.opcode) {
          case 0x00:
            output.pulse_active = false;
            output.gate_high = command.value > 0;
            break;
          case 0x01:
            output.pulse_active = false;
            output.gate_high = !output.gate_high;
            break;
          case 0x02:
            output.pulse_ms = uint16_t(command.value);
            break;
          case 0x05:
            output.gate_high = true;
            output.pulse_active = true;
            output.pulse_deadline = now + output.pulse_ms;
            break;
          case 0x10:
            output.cv_start = CurrentCv(output, now);
            output.cv_target = command.value;
            output.cv_started_at = now;
            output.cv_duration = output.slew_ms;
            output.cv_slewing = output.cv_duration != 0;
            break;
          case 0x11:
            output.cv_start = output.cv_target = command.value;
            output.cv_slewing = false;
            break;
          case 0x12:
            output.slew_ms = uint16_t(command.value);
            break;
        }
        published = output;
      }
      UpdateRendered(now);
    }
    for (unsigned channel = 0; channel < kChannels; ++channel) {
      if (expired & (uint8_t(1) << channel))
        monitor.CommandGate(channel, false, PulseTime(channel), now);
    }
    if (!has_command) return;
    if (kill_command) {
      for (unsigned i = 0; i < 4; ++i) {
        const unsigned channel = kill_base + i;
        if (publish_cv_mask & (uint8_t(1) << channel))
          monitor.CommandCv(channel,killed[i].cv_target,
                            Millivolts(killed[i].cv_target),now,
                            killed[i].slew_ms);
        else
          monitor.CommandGate(channel,false,killed[i].pulse_ms,now);
      }
      return;
    }
    if (command.opcode <= 0x05) {
      monitor.CommandGate(command.channel, published.gate_high,
                          published.pulse_ms, now);
    } else {
      monitor.CommandCv(command.channel, published.cv_target,
                        Millivolts(published.cv_target), now,
                        published.slew_ms);
    }
  }

  template<class Values>
  void Snapshot(Values& values) const {
    Lock lock;
    for (unsigned channel = 0; channel < kChannels; ++channel)
      values[channel] = rendered_[channel];
  }

 private:
  static constexpr unsigned kQueueDepth = 8;
  struct Command { uint8_t channel; uint8_t opcode; int16_t value; };
  struct Output {
    int16_t cv_start;
    int16_t cv_target;
    uint16_t slew_ms;
    uint16_t cv_duration;
    uint32_t cv_started_at;
    uint16_t pulse_ms;
    uint32_t pulse_deadline;
    bool cv_slewing;
    bool gate_high;
    bool pulse_active;
  };

  static Output DefaultOutput() {
    Output output{};
    output.slew_ms = 1;
    output.pulse_ms = 100;
    return output;
  }

  static bool Decode(const uint8_t* frame, size_t size, Command& command) {
    if (!frame || (size != 5 && size != 7) || frame[0] != 1 ||
        frame[1] != 1 || frame[2] < 1 || frame[2] > 2 || frame[4] > 3)
      return false;
    const uint8_t opcode = frame[3];
    const bool kill_command = opcode == 0x20;
    const bool cv_command = opcode >= 0x10 && opcode <= 0x12;
    const bool gate_command = opcode == 0x00 || opcode == 0x01 ||
                              opcode == 0x02 || opcode == 0x05;
    const bool command_only = opcode == 0x01 || opcode == 0x05 || kill_command;
    if ((!cv_command && !gate_command && !kill_command) ||
        size != (command_only ? 5u : 7u))
      return false;
    const int16_t value = size == 7
        ? int16_t((uint16_t(frame[5]) << 8) | frame[6]) : 0;
    if ((opcode == 0x02 || opcode == 0x12) && value < 0) return false;
    command = {uint8_t(unsigned(frame[2] - 1) * 4 + frame[4]),opcode,value};
    return true;
  }

  bool Compatible(const Command& command) const {
    const bool kill_command = command.opcode == 0x20;
    const bool cv_command = command.opcode >= 0x10 && command.opcode <= 0x12;
    const bool is_cv = (cv_mask_ & (uint8_t(1) << command.channel)) != 0;
    return kill_command || is_cv == cv_command;
  }

  static bool Reached(uint32_t now, uint32_t deadline) {
    return int32_t(now - deadline) >= 0;
  }

  static int16_t CurrentCv(const Output& output, uint32_t now) {
    if (!output.cv_slewing) return output.cv_target;
    const uint32_t elapsed = now - output.cv_started_at;
    if (elapsed >= output.cv_duration) return output.cv_target;
    const int32_t delta = int32_t(output.cv_target) - output.cv_start;
    return int16_t(int32_t(output.cv_start) +
                   delta * int32_t(elapsed) / output.cv_duration);
  }

  static int32_t ToPitch(int16_t value) {
    return int32_t(value) * 15 / 16;
  }

  static int32_t Millivolts(int16_t value) {
    return int32_t(value) * 10000 / 16384;
  }

  uint16_t PulseTime(unsigned channel) const {
    Lock lock;
    return outputs_[channel].pulse_ms;
  }

  void RemoveQueued(unsigned channel) {
    Command kept[kQueueDepth]{};
    uint8_t kept_count = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      const auto& command = queue_[(head_ + i) % kQueueDepth];
      if (command.channel != channel) kept[kept_count++] = command;
    }
    for (uint8_t i = 0; i < kept_count; ++i) queue_[i] = kept[i];
    head_ = 0;
    tail_ = kept_count % kQueueDepth;
    count_ = kept_count;
  }

  void UpdateRendered(uint32_t now) {
    for (unsigned channel = 0; channel < kChannels; ++channel) {
      const auto& output = outputs_[channel];
      rendered_[channel] = cv_mask_ & (uint8_t(1) << channel)
          ? ToPitch(CurrentCv(output, now)) : output.gate_high;
    }
  }

  uint8_t cv_mask_;
  Command queue_[kQueueDepth];
  Output outputs_[kChannels];
  int32_t rendered_[kChannels];
  uint8_t head_;
  uint8_t tail_;
  uint8_t count_;
};

} // namespace txi2c2midi
