#pragma once
#include "Domain.h"
#include <limits>
#include <type_traits>
#include <utility>
namespace phz { namespace parameters {
class Owner;
struct Entry {
  uint16_t id;
  const char* key; // immutable semantic key, NOT UI label
  const char* label;
  const char* units;
  Domain domain;
  int32_t (*read)(const Owner&);
  // False MUST leave all state unchanged. Absolute native assignment only.
  bool (*write)(Owner&, int32_t, uint64_t);
  // Optional applet-specific availability/bounds/dependency snapshot. May NOT
  // change kind/enum table. Runs under caller protection; bounded, no I/O/alloc.
  bool (*describe)(const Owner&, Domain&) = nullptr;
};
struct Schema {
  const Entry* entries = nullptr;
  size_t count = 0;
};
constexpr bool Equal(const char* a, const char* b) {
  if (!a || !b) return a == b;
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
// Compile-time declaration checks. Sorted immutable IDs allow bounded binary
// lookup (<=16 comparisons), independent of UI/registry order. Deleted IDs stay
// as tombstones (both callbacks null), never reassigned to a different setting.
template<size_t N> constexpr bool Valid(const Entry (&entries)[N]) {
  if (!N || N > 65535) return false;
  for (size_t i = 0; i < N; ++i) {
    const auto& e = entries[i];
    if (!e.id || (i && e.id <= entries[i-1].id) || !e.key || !*e.key ||
        !e.label || !*e.label || !e.units || !*e.units || bool(e.read) != bool(e.write)) return false;
    for (size_t j = 0; j < i; ++j) if (Equal(e.key, entries[j].key)) return false;
    const auto& d = e.domain;
    if (d.kind == Kind::Integer) { if (d.minimum > d.maximum) return false; }
    else if (d.kind == Kind::Boolean) {
      if (d.minimum != 0 || d.maximum != 1) return false;
    } else if (d.kind == Kind::Enumeration) {
      if (!d.options || !d.option_count || d.option_count > 256) return false;
      for (size_t a = 0; a < d.option_count; ++a)
        for (size_t b = 0; b < a; ++b)
          if (d.options[a].code == d.options[b].code) return false;
    } else return false;
  }
  return true;
}
// All accesses require caller-owned synchronization for that applet. ii uses
// its final IRQ lock; the firmware UI uses its protected native-set wrapper.
// Parameter callbacks must not start transitions or perform unbounded work.
class Owner {
 public:

  PHZ_PARAMETER_CODE virtual Schema Parameters() const { return {}; } // legacy applets reject
  PHZ_PARAMETER_CODE bool DescribeParameter(uint16_t id, Domain& out) const;
  PHZ_PARAMETER_CODE bool ReadParameter(uint16_t id, int32_t& out) const;
  PHZ_PARAMETER_CODE bool SetParameter(uint16_t id, int32_t value, uint64_t dependencies = 0);
 protected:
  // The applet host owns lifetime; never delete an applet through this view.
  ~Owner() = default;
 private:
  PHZ_PARAMETER_CODE const Entry* Find(uint16_t id) const;
  PHZ_PARAMETER_CODE bool Describe(const Entry&, Domain&) const;
};
// One declaration connects a scalar/array element to its native storage. Use a
// custom Entry write callback for coupled state/DSP side effects instead.
template<class App, auto Member, size_t Index = 0> struct Field {
  template<class A> static decltype(auto) Get(A& app) {
    using MemberType = std::remove_reference_t<decltype(app.*Member)>;
    if constexpr (std::is_array_v<MemberType>) {
      static_assert(Index < std::extent_v<MemberType>);
      return (app.*Member)[Index];
    } else { static_assert(Index == 0); return (app.*Member); }
  }
  using Value = std::remove_reference_t<decltype(Get(std::declval<App&>()))>;
  static_assert(std::is_integral_v<Value> && sizeof(Value) <= sizeof(int32_t),
                "Use a custom accessor for non-integral or wider native settings");
  static_assert(int64_t(std::numeric_limits<Value>::min()) >= INT32_MIN &&
                uint64_t(std::numeric_limits<Value>::max()) <= INT32_MAX,
                "Use custom accessors when native storage can exceed int32");
  static PHZ_PARAMETER_CODE int32_t Read(const Owner& owner) {
    return Get(static_cast<const App&>(owner));
  }
  static PHZ_PARAMETER_CODE bool Write(Owner& owner, int32_t value, uint64_t) {
    if (int64_t(value) < int64_t(std::numeric_limits<Value>::min()) ||
        int64_t(value) > int64_t(std::numeric_limits<Value>::max())) return false;
    Get(static_cast<App&>(owner)) = static_cast<Value>(value);
    return true;
  }
  static constexpr Entry Make(uint16_t id, const char* key, const char* label,
                              const char* units, int32_t min, int32_t max) {
    return {id, key, label, units,
      {std::is_same_v<Value, bool> ? Kind::Boolean : Kind::Integer, min, max},
      Read, Write};
  }
};
}}
