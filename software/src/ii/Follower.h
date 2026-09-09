#pragma once
#include "AddressState.h"
#include "Dispatcher.h"
namespace phz { namespace ii {
// Called once at boot, after complete global validation and app initialization.
// This registration does not yet apply received commands to applets (#27/#28).
void StartFollower(const AddressState& address);
bool FollowerRegistered();
void ServiceFollower(Backend& backend);
Diagnostics FollowerDiagnostics();
}} // namespace phz::ii
