#pragma once
#include "OutputRouting.h"

namespace phz { namespace routing {
// Quadrants lower-11-bit preset key, after ii records40..71 and usage72.
constexpr uint16_t kOutputRoutingKey = 73;

// Missing legacy records select identity, not the previous preset's routing.
// Malformed records reject without changing the staged result or live routes.
template <size_t N, class Read>
bool StageOutputRouting(Read read, uint64_t& staged) {
  OutputRouting<N> candidate;
  uint64_t record = 0;
  if (read(kOutputRoutingKey, record) && !candidate.Restore(record)) return false;
  staged = candidate.Encode();
  return true;
}
}} // namespace phz::routing
