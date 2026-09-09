#include "Follower.h"
#if defined(ARDUINO_TEENSY41)
#include <Wire.h>
#include "InterruptLock.h"
namespace phz { namespace ii {
namespace {
Dispatcher<InterruptLock> dispatcher;
bool registered = false;
void OnReceive(int count) {
  // Wire reads here only drain its memory buffer (at most BUFFER_LENGTH).
  // Prevent a higher-priority preset ISR from switching target between the
  // start of this receive and its queue admission. No bus waits or applet work.
  InterruptLock lock;
  Receive(Wire, count, BUFFER_LENGTH, dispatcher);
}
void OnRequest() {} // pinned Wire supplies zero fill; no application reply
}
void StartFollower(const AddressState& address) {
  if (registered || !address.enabled()) return;
  // State exists before IRQ-capable begin. Preserve any startup lifecycle fault.
  dispatcher.Begin();
  dispatcher.End(true);
  Wire.onReceive(OnReceive);
  Wire.onRequest(OnRequest);
  Wire.begin(address.active());
  registered = true;
}
bool FollowerRegistered() { return registered; }
void ServiceFollower(Backend& backend) {
  if (registered) dispatcher.Service(backend);
}
Diagnostics FollowerDiagnostics() { return dispatcher.Inspect(); }
void BeginTransition(bool recovery) { dispatcher.Begin(recovery); }
void EndTransition(bool success) { dispatcher.End(success); }
void SetHostActive(bool active) { dispatcher.SetActive(active); }
}} // namespace phz::ii
#endif
