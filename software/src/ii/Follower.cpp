#include "Follower.h"
#if defined(ARDUINO_TEENSY41)
#include <Wire.h>
#include "InterruptLock.h"
#include "VirtualInputState.h"
#include "OutputReply.h"
#include "CompactReceiver.h"
namespace phz { namespace ii {
namespace {
Dispatcher<InterruptLock> dispatcher;
bool registered = false;
uint8_t active_family = 0;
uint16_t quantizer_scale_count = 0;
OutputReply output_reply;
HostIdentityReply identity_reply;
bool identity_selected=false;
CompactReceiver compact_reply;
bool compact_selected=false;
bool Open(const Diagnostics& d) {
  return d.active && !d.fault && !d.depth && !d.exhausted;
}
class SignalBackend : public Backend {
 public:
  explicit SignalBackend(Backend& target) : target_(target) {}
  bool Describe(const Write& w, Descriptor& d) override { return target_.Describe(w, d); }
  bool Apply(const Prepared& p) override { return target_.Apply(p); }
  HostActionResult ApplyHostAction(const HostAction& action) override {
    return target_.ApplyHostAction(action);
  }
  bool ApplyVirtualCV(uint16_t channel, uint16_t code) override {
    return virtual_inputs.Write(channel, code);
  }
  bool ApplyVirtualSignal(uint8_t command, uint16_t channel, uint16_t value) override {
    switch (command) {
      case kWriteVirtualCV: return ApplyVirtualCV(channel, value);
      case kWriteVirtualVoltage: return virtual_inputs.WriteVoltage(channel, value);
      case kWriteVirtualNormalized: return virtual_inputs.WriteNormalized(channel, value);
      default: return false;
    }
  }
  bool ApplyQuantizer(const pc_qtz_request& request) override {
    return target_.ApplyQuantizer(request);
  }
 private:
  Backend& target_;
};
void OnReceive(int count) {
  // Wire reads here only drain its memory buffer (at most BUFFER_LENGTH).
  // Prevent a higher-priority preset ISR from switching target between the
  // start of this receive and its queue admission. No bus waits or applet work.
  InterruptLock lock;
  if (!registered) return;
  // Accept exact query or write frames, never a truncated long-frame prefix.
  uint8_t frame[16]{};
  bool valid = (count == 3 || count == 6 || count == 7 || count == 8 ||
                count == 9 || count == 16) &&
               Wire.available() == count;
  size_t consumed = 0;
  while (consumed < BUFFER_LENGTH && Wire.available() > 0) {
    const int byte = Wire.read();
    if (byte < 0) valid = false;
    if (consumed < sizeof(frame)) frame[consumed] = uint8_t(byte);
    ++consumed;
  }
  if (!valid || consumed != size_t(count) || Wire.available()) {
    compact_reply.Clear(); compact_selected=false;
    output_reply.Clear(); identity_reply.Clear(); identity_selected=false; dispatcher.Reject(); return;
  }
  if (frame[0] == 0x50) {
    output_reply.Clear(); identity_reply.Clear(); identity_selected=false;
    compact_selected=true; // Never fall back to the raw decoder.
    if (!compact_reply.Receive(frame,size_t(count),active_family,
                               quantizer_scale_count,
                               virtual_inputs,dispatcher)) dispatcher.Reject();
    return;
  }
  compact_reply.Clear(); compact_selected=false;
  if (count != 3 && count != int(kFrameSize)) {
    output_reply.Clear(); identity_reply.Clear(); identity_selected=false;
    dispatcher.Reject(); return;
  }
  if (count == 3) {
    const auto d = dispatcher.Inspect();
    identity_selected=HostIdentityReply::IsCommand(frame[0]);
    if(identity_selected){
      output_reply.Clear();
      if(!identity_reply.Select(frame,3,d.epoch,Open(d)))dispatcher.Reject();
    } else if (!output_reply.Select(frame, 3, virtual_inputs, d.epoch, Open(d))) dispatcher.Reject();
  } else {
    output_reply.Clear();
    identity_selected=false;
    dispatcher.Capture(frame, kFrameSize);
  }
}
void OnRequest() {
  InterruptLock lock;
  if (!registered) return;
  const auto d = dispatcher.Inspect();
  if (compact_selected) {
    compact_reply.Take(d.epoch, [](void*, const uint8_t* bytes, size_t size) {
      return Wire.write(bytes,size)==size ? 1 : 0;
    }, nullptr);
    return;
  }
  uint8_t reply[2];
  if(identity_selected)identity_reply.Take(reply,d.epoch,Open(d));
  else output_reply.Take(reply, d.epoch, Open(d));
  // Buffer-only; never initiate a bus transaction. A short copy cannot validate.
  if (Wire.write(reply, sizeof(reply)) != sizeof(reply)) { output_reply.Clear();identity_reply.Clear(); }
}
}
void StartFollower(const AddressState& address, bool hardware_supported) {
  InterruptLock lock;
  // Do not claim SDA/SCL or install callbacks on an unsupported pin layout.
  if (registered || !hardware_supported || !address.enabled()) return;
  // State exists before IRQ-capable begin. Preserve any startup lifecycle fault.
  dispatcher.Begin();
  dispatcher.End(true);
  compact_reply.Clear(); compact_selected=false;
  Wire.onReceive(OnReceive);
  Wire.onRequest(OnRequest);
  NVIC_CLEAR_PENDING(IRQ_LPI2C1);
  registered = true; // IRQs remain masked until begin and state are complete.
  Wire.begin(address.active());
}
void StopFollower() {
  InterruptLock lock;
  if (!registered) return; // Never touch a peripheral owned by another app.
  NVIC_DISABLE_IRQ(IRQ_LPI2C1);
  IMXRT_LPI2C1.SIER = 0;
  IMXRT_LPI2C1.SCR = LPI2C_SCR_RST;
  IMXRT_LPI2C1.SCR = 0;
  NVIC_CLEAR_PENDING(IRQ_LPI2C1);
  registered = false;
  active_family = 0;
  quantizer_scale_count = 0;
  compact_reply.Clear(); compact_selected=false;
  Wire.onReceive(nullptr);
  Wire.onRequest(nullptr);
  dispatcher.SetActive(false);
  dispatcher.Begin(); // Invalidate even if the host was already inactive.
  dispatcher.End(true); // Preserve existing fault state; this is not recovery.
  output_reply.Clear();
  identity_reply.Clear();
  identity_selected = false;
  virtual_inputs.ClearValues();
#if defined(__arm__)
  asm volatile("dsb" ::: "memory");
#endif
}
bool FollowerRegistered() { return registered; }
void ServiceFollower(Backend& backend) {
  if (registered) {
    SignalBackend signals(backend);
    dispatcher.Service(signals);
    InterruptLock lock;
    const auto d=dispatcher.Inspect();
    HostIdentity identity;
    if(Open(d) && backend.SnapshotHostIdentity(identity))identity_reply.Publish(identity,d.epoch,true);
    else identity_reply.Clear();
  }
}
Diagnostics FollowerDiagnostics() { return dispatcher.Inspect(); }
uint32_t CaptureOutputEpoch() {
  InterruptLock lock;
  const auto d = dispatcher.Inspect();
  return Open(d) ? d.epoch : 0;
}
void PublishOutputs(uint32_t epoch, const int32_t* values, size_t count) {
  InterruptLock lock;
  const auto d = dispatcher.Inspect();
  if (!values || count != 8 || !epoch || d.epoch != epoch || !Open(d)) return;
  for (size_t i = 0; i < DirectionalMappings::kCount; ++i) {
    const auto slot = virtual_inputs.DirectionalMappings::Get(i);
    if (slot.producer && slot.producer <= count)
      virtual_inputs.Publish(i, slot.producer, values[slot.producer - 1]);
  }
}
void BeginTransition(bool recovery) {
  InterruptLock lock;
  dispatcher.Begin(recovery);
  compact_reply.Clear(); compact_selected=false;
  output_reply.Clear();
  identity_reply.Clear();
  virtual_inputs.ClearValues();
}
void EndTransition(bool success) { dispatcher.End(success); }
void SetHostActive(bool active, uint8_t family, uint16_t scale_count) {
  InterruptLock lock;
  compact_reply.Clear(); compact_selected=false;
  output_reply.Clear();
  identity_reply.Clear();
  if (!active) virtual_inputs.ClearValues();
  active_family = active ? family : 0;
  quantizer_scale_count = active ? scale_count : 0;
  dispatcher.SetActive(active);
}
}} // namespace phz::ii
#endif
