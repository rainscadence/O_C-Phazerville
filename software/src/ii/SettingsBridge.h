#pragma once
#include "Parameter.h"
#include "../parameters/Schema.h"
namespace phz { namespace ii {
// Single transport adapter shared by every declared applet, including future
// registry entries. No applet-name switch, parallel mapping or cursor probing.
inline bool DescribeSettings(const parameters::Owner& owner, uint16_t id, Descriptor& out) {
  parameters::Domain d;
  if (!owner.DescribeParameter(id, d)) return false;
  out.kind = d.kind; out.minimum = d.minimum; out.maximum = d.maximum;
  out.options = d.options; out.option_count = d.option_count;
  out.dependencies = d.dependencies;
  return true;
}
inline bool ApplySettings(parameters::Owner& owner, uint16_t id, int32_t value, uint64_t dependencies) {
  return owner.SetParameter(id, value, dependencies);
}
}}
