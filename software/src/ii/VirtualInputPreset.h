#pragma once
#include "VirtualInputs.h"
namespace phz { namespace ii {
// Per-preset keys 40..71 in Quadrants (under its 11-bit preset key).
constexpr uint16_t kVirtualRangeKey = 40;
// Stage every range before publishing; missing legacy records use 0..5V.
// A malformed record rejects the load and leaves the destination unchanged.
template<class Read>
bool LoadVirtualRanges(Read read, VirtualInputs& destination) {
  VirtualInputs staged;
  for (size_t i = 0; i < VirtualInputs::kCount; ++i) {
    uint64_t record = 0;
    if (read(kVirtualRangeKey + i, record) && !staged.RestoreRange(i, record))
      return false;
  }
  destination = staged;
  return true;
}
}} // namespace phz::ii
