#pragma once
#include "Protocol.h"
#include <string.h>

namespace phz { namespace ii {
// Caller owns synchronization: ISR capture; any future main-loop access must
// save/restore the prior IRQ state. No applet references live in this queue.
class ReceiveQueue {
 public:
  static constexpr size_t kCapacity = 8;
  void Clear() { head_ = count_ = 0; }
  void Reset() { head_ = count_ = 0; rejected_ = dropped_ = 0; }
  bool Capture(const uint8_t* bytes, size_t size) {
    if (!bytes || size != kFrameSize) { ++rejected_; return false; }
    if (count_ == kCapacity) { ++dropped_; return false; }
    memcpy(frames_[(head_ + count_) % kCapacity], bytes, kFrameSize);
    ++count_;
    return true;
  }
  bool Pop(uint8_t (&bytes)[kFrameSize]) {
    if (!count_) return false;
    memcpy(bytes, frames_[head_], kFrameSize);
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return true;
  }
  void Reject() { ++rejected_; }
  size_t count() const { return count_; }
  uint32_t rejected() const { return rejected_; }
  uint32_t dropped() const { return dropped_; }
 private:
  uint8_t frames_[kCapacity][kFrameSize]{};
  size_t head_ = 0, count_ = 0;
  uint32_t rejected_ = 0, dropped_ = 0;
};

// Wire caps long transactions at its buffer size; never accept their prefix.
// Every invocation consumes at most buffer_limit bytes, including bad lengths.
template<class Wire, class Sink>
void Receive(Wire& wire, int announced, size_t buffer_limit, Sink& queue) {
  uint8_t frame[kFrameSize]{};
  const int available = wire.available();
  bool valid = announced == int(kFrameSize) && available == announced &&
               size_t(announced) < buffer_limit;
  size_t consumed = 0;
  for (size_t i = 0; i < buffer_limit && wire.available() > 0; ++i) {
    const int byte = wire.read();
    ++consumed;
    if (byte < 0) valid = false;
    if (i < kFrameSize) frame[i] = static_cast<uint8_t>(byte);
  }
  if (valid && consumed == kFrameSize && wire.available() == 0)
    queue.Capture(frame, sizeof(frame));
  else queue.Reject();
}
}} // namespace phz::ii
