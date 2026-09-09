#include "Schema.h"
namespace phz { namespace parameters {
const Entry* Owner::Find(uint16_t id) const {
  const auto schema = Parameters();
  if (!id || !schema.entries || schema.count > 65535) return nullptr;
  size_t lo = 0, hi = schema.count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const auto& e = schema.entries[mid];
    if (e.id == id) return e.read && e.write ? &e : nullptr;
    if (e.id < id) lo = mid + 1; else hi = mid;
  }
  return nullptr;
}
bool Owner::Describe(const Entry& e, Domain& out) const {
  Domain d = e.domain;
  if (e.describe && !e.describe(*this, d)) return false;
  // Custom snapshots can change bounds/availability/dependencies, not the
  // stable kind/options mapping declared by the applet.
  if (d.kind != e.domain.kind || d.options != e.domain.options ||
      d.option_count != e.domain.option_count || d.minimum > d.maximum ||
      (d.kind == Kind::Boolean && (d.minimum != 0 || d.maximum != 1))) return false;
  out = d;
  return true;
}
bool Owner::DescribeParameter(uint16_t id, Domain& out) const {
  const auto* e = Find(id);
  return e && Describe(*e, out);
}
bool Owner::ReadParameter(uint16_t id, int32_t& out) const {
  const auto* e = Find(id);
  Domain d;
  if (!e || !Describe(*e, d)) return false;
  out = e->read(*this);
  return true;
}
bool Owner::SetParameter(uint16_t id, int32_t value, uint64_t dependencies) {
  const auto* e = Find(id);
  Domain d;
  return e && Describe(*e, d) && d.dependencies == dependencies &&
    NativeAllowed(d, value) && e->write(*this, value, dependencies);
}
}}
