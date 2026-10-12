#pragma once
#include "AddressState.h"
#include "Dispatcher.h"
namespace phz { namespace ii {
// Called at boot or after StopFollower, after validated app initialization.
// Requires the GPIO layer's detected I2C expansion capability, not just T4.1.
// This is a pin-use guard, not electrical approval for an external bus.
void StartFollower(const AddressState& address, bool hardware_supported);
// Main-loop only, no other owner may use Wire concurrently. Explicitly detaches
// this follower; Wire.end() is a no-op in the pinned framework. A later start
// does not reactivate the host: the caller must complete its app lifecycle.
void StopFollower();
bool FollowerRegistered();
void ServiceFollower(Backend& backend);
Diagnostics FollowerDiagnostics();
// Capture before calculating a frame; publish only in that same open epoch.
uint32_t CaptureOutputEpoch();
void PublishOutputs(uint32_t epoch, const int32_t* values, size_t count);
}} // namespace phz::ii
