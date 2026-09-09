#pragma once
#include "Protocol.h"
#include "../parameters/Domain.h"
#include <stddef.h>
#include <stdint.h>
namespace phz { namespace ii {
using ValueKind = parameters::Kind;
using EnumOption = parameters::Option;
// Snapshot under IRQ lock. Option tables must be immutable, static lifetime;
// never point into live mutable applet data. Zero-initialize before Describe.
struct Descriptor {
  const void* instance = nullptr;
  uint64_t type = 0;
  uint16_t slot = 0, parameter = 0;
  ValueKind kind = ValueKind::Integer;
  int32_t minimum = 0, maximum = 0;
  const EnumOption* options = nullptr;
  size_t option_count = 0;
  // Adapter supplies a token for mutable dependencies relevant to this write.
  // Commit must revalidate those dependencies; equality alone is not a setter.
  uint64_t dependencies = 0;
};
struct Prepared { Descriptor descriptor; int32_t value = 0; };
// Pure conversion. No live instance dereference; output unchanged on failure.
// Integer delegates to DecodeInteger. For Boolean EVERY uint16 code succeeds:
// write 0 below32768, otherwise1, then return true. DecodeBoolean returns the
// VALUE, not a success flag; false is a valid assignment, not a rejection.
// Enumeration: nonnull static table, 1..256 unique wire codes. Reject duplicate
// codes anywhere (even not requested); exact code lookup, never normalization.
// Unknown ValueKind rejects.
bool Convert(const Descriptor& descriptor, uint16_t code, int32_t& value);
// Compare every descriptor field explicitly (never memcmp padding).
bool SameDescriptor(const Descriptor& a, const Descriptor& b);
}} // namespace phz::ii
