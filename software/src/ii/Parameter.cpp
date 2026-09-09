#include "Parameter.h"

namespace phz { namespace ii {
// Pure conversion over the copied snapshot. No live instance dereference;
// output stays untouched on failure.
bool Convert(const Descriptor& descriptor, uint16_t code, int32_t& value) {
  switch (descriptor.kind) {
    case ValueKind::Integer:
      return DecodeInteger(code, descriptor.minimum, descriptor.maximum, value);
    case ValueKind::Boolean:
      value = DecodeBoolean(code) ? 1 : 0;
      return true;
    case ValueKind::Enumeration: {
      const size_t count = descriptor.option_count;
      if (!descriptor.options || count < 1 || count > 256) return false;
      // Reject duplicate wire codes anywhere in the table, even if the
      // duplicate is not the requested code.
      for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
          if (descriptor.options[i].code == descriptor.options[j].code)
            return false;
        }
      }
      // Exact code lookup; never normalize.
      for (size_t i = 0; i < count; ++i) {
        if (descriptor.options[i].code == code) {
          value = descriptor.options[i].native;
          return true;
        }
      }
      return false;
    }
    default:
      return false;
  }
}

// Compare every descriptor field explicitly, including pointer identities;
// never memcmp padding.
bool SameDescriptor(const Descriptor& a, const Descriptor& b) {
  return a.instance == b.instance && a.type == b.type &&
         a.slot == b.slot && a.parameter == b.parameter &&
         a.kind == b.kind && a.minimum == b.minimum &&
         a.maximum == b.maximum && a.options == b.options &&
         a.option_count == b.option_count && a.dependencies == b.dependencies;
}
}} // namespace phz::ii
