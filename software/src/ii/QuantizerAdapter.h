#pragma once

#include "ii/compact.h"
#include "quantizer/QuantizerCommand.h"

namespace phz { namespace ii {

// Convert validated family-5 wire syntax into the shared application command.
// Capture-by-chord stays outside this adapter until its source/completion
// transaction exists.
bool DecodeQuantizerRequest(const pc_qtz_request& request,
                            OC::QuantizerCommand& command);

}}  // namespace phz::ii
