#pragma once
#include "Memory.h"
#include <stddef.h>
#include <stdint.h>
namespace phz { namespace parameters {
enum class Kind : uint8_t { Integer, Boolean, Enumeration };
struct Option { uint16_t code; int32_t native; };
struct Domain {
  Kind kind = Kind::Integer;
  int32_t minimum = 0, maximum = 0;
  const Option* options = nullptr;
  size_t option_count = 0;
  uint64_t dependencies = 0;
};
// Pure native-domain membership, not normalized/wire conversion. Integer uses
// inclusive min/max and rejects reversed bounds. Boolean accepts BOTH 0 and 1
// (false is data, NOT failure). Enumeration accepts exact native values in a
// nonnull immutable table of1..256 options; no normalization. Unknown kind
// rejects. No allocation, mutation or live instance access; O(option_count).
PHZ_PARAMETER_CODE bool NativeAllowed(const Domain&, int32_t value);
}}
