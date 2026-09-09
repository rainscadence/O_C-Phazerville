#include "Domain.h"
namespace phz { namespace parameters {
bool NativeAllowed(const Domain& domain, int32_t value) {
  switch (domain.kind) {
    case Kind::Integer:
      return domain.minimum <= domain.maximum &&
             value >= domain.minimum && value <= domain.maximum;
    case Kind::Boolean:
      // 0 (false) is a valid native value, not a conversion failure.
      return value == 0 || value == 1;
    case Kind::Enumeration:
      if (!domain.options || !domain.option_count || domain.option_count > 256)
        return false;
      for (size_t i = 0; i < domain.option_count; ++i)
        if (domain.options[i].native == value) return true;
      return false;
    default:
      return false;
  }
}
}}
