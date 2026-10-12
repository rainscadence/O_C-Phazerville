#pragma once
#include "Parameter.h"
#include "ReceiveQueue.h"
#include "HostAction.h"
#include "HostIdentity.h"
#include <stdint.h>
#include <limits>

namespace phz { namespace ii {
class Backend {
 public:
  virtual ~Backend() = default;
  // Both methods run under the caller's IRQ lock. They must be bounded,
  // allocation-free and perform no file/serial I/O or lifecycle operations.
  virtual bool Describe(const Write&, Descriptor&) = 0;
  // Final application-owned dependency validation and absolute assignment.
  // Return false WITHOUT any mutation when dependencies cannot be satisfied.
  virtual bool Apply(const Prepared&) = 0;
  // Main-loop only, WITHOUT the IRQ lock. Rejected must not mutate live state;
  // Failed denotes partial mutation and keeps ingress faulted closed.
  virtual HostActionResult ApplyHostAction(const HostAction&) { return HostActionResult::Rejected; }
  // Main-loop snapshot under a short IRQ lock; no allocation, I/O or mutation.
  virtual bool SnapshotHostIdentity(HostIdentity&) { return false; }
  // Optional signal-bank sink. Same bounded, allocation-free lock contract.
  virtual bool ApplyVirtualCV(uint16_t, uint16_t) { return false; }
  virtual bool ApplyVirtualSignal(uint8_t command, uint16_t channel, uint16_t value) {
    return command == kWriteVirtualCV && ApplyVirtualCV(channel, value);
  }
  // Bounded active-app quantizer commit under the same short IRQ lock used by
  // ordinary parameter writes. No allocation, storage or lifecycle changes.
  virtual bool ApplyQuantizer(const pc_qtz_request&) { return false; }
};
struct Counters {
  uint32_t blocked = 0, malformed = 0, unsupported = 0, stale = 0;
  uint32_t applied = 0, serviced = 0, invalidations = 0;
};
struct Diagnostics {
  Counters counts;
  uint32_t epoch;
  size_t queued;
  uint32_t rejected, overflow;
  uint16_t depth;
  bool active, fault, prepared, busy, exhausted;
};

// One producer (Wire ISR), one main-loop service. Every shared access is
// protected by Lock, including lifecycle calls which may originate in ISR.
// Lock must save AND restore the caller's previous interrupt mask.
template<class Lock>
class Dispatcher {
 public:
  explicit Dispatcher(uint32_t initial_epoch = 1) : epoch_(initial_epoch) {}
  bool Capture(const uint8_t* bytes, size_t size) {
    Lock lock;
    if (!Open()) { ++counts_.blocked; return false; }
    return queue_.Capture(bytes, size);
  }
  bool CaptureQuantizer(const pc_qtz_request& request) {
    Lock lock;
    if (!Open()) { ++counts_.blocked; return false; }
    return queue_.CaptureQuantizer(request);
  }
  void Reject() { Lock lock; queue_.Reject(); }
  void SetActive(bool active) {
    Lock lock;
    if (active_ != active) Invalidate();
    active_ = active;
  }
  void Begin(bool recovery = false) {
    Lock lock;
    if (!depth_) { batch_failed_ = false; recovery_ = recovery; }
    Invalidate();
    if (depth_ == std::numeric_limits<uint16_t>::max()) {
      fault_ = exhausted_ = true; // cannot safely track another nesting level
      return;
    }
    ++depth_;
  }
  void End(bool success) {
    Lock lock;
    if (!depth_) { fault_ = true; Invalidate(); return; }
    if (!success) batch_failed_ = fault_ = true;
    --depth_;
    if (!depth_ && recovery_ && !batch_failed_ && !exhausted_)
      fault_ = false;
  }
  Diagnostics Inspect() {
    Lock lock;
    return {counts_, epoch_, queue_.count(), queue_.rejected(), queue_.dropped(),
            depth_, active_, fault_, prepared_, busy_, exhausted_};
  }
  void Service(Backend& backend) {
    ReceiveQueue::Item item;
    uint32_t epoch;
    {
      Lock lock;
      if (busy_ || !Open() || !queue_.Pop(item)) return;
      busy_ = true;
      epoch = epoch_;
      ++counts_.serviced;
    }
    if (item.kind == ReceiveQueue::Kind::Quantizer) {
      Lock lock;
      if (!Current(epoch)) ++counts_.stale;
      else if (backend.ApplyQuantizer(item.quantizer)) ++counts_.applied;
      else ++counts_.unsupported;
      Finish();
      return;
    }
    const uint8_t* frame = item.frame;
    Write write{};
    if (frame[0] == 7 || frame[0] == 8) {
      HostAction action{};
      {
        Lock lock;
        if (!Current(epoch)) { ++counts_.stale; Finish(); return; }
        if (!DecodeHostAction(frame, kFrameSize, action)) {
          ++counts_.malformed; Finish(); return;
        }
        Begin(false); // Purge old work and block ingress across host changes.
      }
      const auto result = backend.ApplyHostAction(action);
      {
        Lock lock;
        End(result != HostActionResult::Failed);
        if (result == HostActionResult::Applied && !fault_) ++counts_.applied;
        else ++counts_.unsupported;
        Finish();
      }
      return;
    }
    if (frame[0] >= kWriteVirtualCV && frame[0] <= kWriteVirtualNormalized) {
      const bool decoded = DecodeVirtualSignal(frame, kFrameSize, write);
      Lock lock;
      if (!Current(epoch)) ++counts_.stale;
      else if (!decoded) ++counts_.malformed;
      else if (backend.ApplyVirtualSignal(frame[0], write.slot, write.value)) ++counts_.applied;
      else ++counts_.unsupported;
      Finish();
      return;
    }
    if (!DecodeWrite(frame, kFrameSize, write)) {
      Lock lock;
      ++counts_.malformed;
      Finish();
      return;
    }
    Prepared update{};
    {
      Lock lock;
      if (!Current(epoch)) { ++counts_.stale; Finish(); return; }
      if (!backend.Describe(write, update.descriptor) ||
          !update.descriptor.instance || update.descriptor.slot != write.slot ||
          update.descriptor.parameter != write.parameter) {
        ++counts_.unsupported;
        Finish();
        return;
      }
      prepared_ = true;
    }
    // Only immutable descriptor data is used here; never dereference instance.
    const bool converted = Convert(update.descriptor, write.value, update.value);
    {
      Lock lock;
      Descriptor current{};
      if (!Current(epoch) || !prepared_) {
        ++counts_.stale;
      } else if (!converted) {
        ++counts_.unsupported;
      } else if (!backend.Describe(write, current) ||
                 !SameDescriptor(current, update.descriptor)) {
        ++counts_.stale;
      } else {
        // Consume the prepared right before effect, preventing repeat effects.
        prepared_ = false;
        if (backend.Apply(update)) ++counts_.applied;
        else ++counts_.unsupported;
      }
      Finish();
    }
  }
 private:
  bool Open() const { return active_ && !fault_ && !depth_ && !exhausted_; }
  bool Current(uint32_t epoch) const { return Open() && epoch == epoch_; }
  void Finish() { busy_ = prepared_ = false; }
  void Invalidate() {
    queue_.Clear();
    prepared_ = false;
    ++counts_.invalidations;
    if (epoch_ == std::numeric_limits<uint32_t>::max()) {
      fault_ = exhausted_ = true; // reboot required, never wrap to an old token
    } else ++epoch_;
  }
  ReceiveQueue queue_;
  Counters counts_;
  uint32_t epoch_;
  uint16_t depth_ = 0;
  bool active_ = false, fault_ = false, prepared_ = false, busy_ = false;
  bool batch_failed_ = false, recovery_ = false, exhausted_ = false;
};
}} // namespace phz::ii
