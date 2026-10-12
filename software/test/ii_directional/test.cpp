#include "ii/DirectionalMappings.h"
#include "ii/MappingSelection.h"
#include "ii/LiveMappings.h"
#include "ii/DirectionChangePrompt.h"
#include "ii/DirectionalPreset.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <initializer_list>
#include <vector>

using phz::ii::DirectionalMappings;
using Direction = DirectionalMappings::Direction;
using Range = phz::ii::VirtualInputs::Range;

int main() {
  for(size_t slot=0;slot<32;++slot) {
    DirectionalMappings preset;
    assert(preset.SetDirection(slot,Direction::Output,0));
    uint64_t record=phz::ii::IiUsageRecord(0);
    auto read=[&](uint16_t key,uint64_t& value){assert(key==72);value=record;return true;};
    assert(phz::ii::ValidateIiUsage(read,preset));
    for(size_t bit=0;bit<32;++bit) {
      record=phz::ii::IiUsageRecord(uint32_t(1)<<bit);
      assert(phz::ii::ValidateIiUsage(read,preset)==(slot!=bit));
    }
    for(unsigned bit=32;bit<64;++bit) {
      record=phz::ii::IiUsageRecord(0)^(UINT64_C(1)<<bit);
      assert(!phz::ii::ValidateIiUsage(read,preset));
    }
    assert(!phz::ii::ValidateIiUsage([](uint16_t,uint64_t&){return false;},preset));
  }
  puts("PASS: directional preset usage evidence, all 32x32 conflicts, missing record and reserved/version/magic corruption");
  phz::ii::DirectionChangePrompt prompt;
  assert(!prompt.Pending() && !prompt.Matches(0,0,0));
  prompt.Refresh(5,2,77); assert(!prompt.Pending());
  for(int slot=0;slot<32;++slot) for(unsigned target=0;target<3;++target) {
    prompt.Begin(slot,target,5,3,123);
    assert(prompt.Pending() && prompt.Slot()==slot && prompt.Target()==target && prompt.Users()==5);
    assert(prompt.Matches(5,3,123));
    assert(!prompt.Matches(6,3,123) && !prompt.Matches(5,4,123));
    assert(!prompt.Matches(5,3,124)); // same count, different bindings
    prompt.Refresh(6,4,124);assert(prompt.Matches(6,4,124));
    prompt.Cancel();assert(!prompt.Pending() && !prompt.Matches(0,0,0));
  }
  for(int slot : {-1,32,INT_MIN,INT_MAX}) {
    prompt.Begin(1,1,1,0,1);prompt.Begin(slot,1,1,0,1);assert(!prompt.Pending());
  }
  for(unsigned target : {3u,UINT_MAX}) {
    prompt.Begin(1,1,1,0,1);prompt.Begin(1,target,1,0,1);assert(!prompt.Pending());
  }
  puts("PASS: direction confirmation state, cancellation, invalid targets and same-count binding identity changes");
  phz::ii::VirtualInputs disabled;
  assert(disabled.SetRange(0, Range::Off));
  phz::ii::LiveMappings imported(disabled);
  assert(imported.Get(0).range == Range::Off);
  uint64_t imported_record = 0;
  assert(imported.Record(0, imported_record));
  DirectionalMappings imported_roundtrip;
  assert(imported_roundtrip.Restore(0, imported_record, 8));
  assert(imported_roundtrip.Get(0).direction == Direction::Off);
  assert(imported.SetDirection(0, Direction::Input, 0));
  assert(imported.Get(0).range == Range::Unipolar5);
  assert(imported.WriteVoltage(1, 4096) && imported.Value(0) == 3840);
  phz::ii::LiveMappings live;
  phz::ii::VirtualInputs legacy;
  for (unsigned range = 0; range <= 3; ++range) {
    assert(live.SetRange(0, Range(range)) && legacy.SetRange(0, Range(range)));
    for (unsigned command = 2; command <= 4; ++command) for (unsigned code = 0; code < 65536; ++code) {
      bool a, b;
      if (command == 2) { a = live.Write(1, code); b = legacy.Write(1, code); }
      else if (command == 3) { a = live.WriteVoltage(1, code); b = legacy.WriteVoltage(1, code); }
      else { a = live.WriteNormalized(1, code); b = legacy.WriteNormalized(1, code); }
      assert(a == b);
      const auto x = live.Get(0), y = legacy.Get(0);
      assert(x.range == y.range && x.code == y.code && x.received == y.received && x.value == y.value);
      assert(live.Value(0) == legacy.Value(0));
    }
  }
  live = legacy;
  assert(live.Get(0).value == legacy.Get(0).value);
  assert(live.SetDirection(0, Direction::Output, 0));
  assert(live.AssignOutput(0, 3, 8) && live.Publish(0, 3, 3840));
  assert(live.Get(0).range == Range::Off && !live.Get(0).received && live.Value(0) == 0);
  assert(!live.Write(1, 123) && !live.WriteVoltage(1, 123) && !live.WriteNormalized(1, 123));
  for (unsigned r = 0; r <= 3; ++r) assert(!live.SetRange(0, Range(r)));
  uint16_t out = 0; assert(live.ReadVoltage(1, out) && out == 4096);
  live.ClearValues(); assert(!live.ReadVoltage(1, out));
  assert(live.DirectionalMappings::Get(0).producer == 3);
  live.Reset(); assert(live.Get(0).range == Range::Unipolar5 && !live.Get(0).received);
  puts("PASS: live adapter equals legacy bank across all 786432 command/range/code combinations; output isolation and lifecycle preserved");
  using phz::ii::CanSelectMapping;
  using phz::ii::NextMapping;
  // Independent ordered-list oracle: compare all cursor positions, roles,
  // producer contexts and extreme encoder deltas across varied mapping banks.
  for (unsigned pattern = 0; pattern < 5; ++pattern) {
    DirectionalMappings select;
    for (size_t i = 0; i < 32; ++i) {
      const auto d = pattern < 3 ? Direction(pattern) : Direction((i + pattern) % 3);
      assert(select.SetDirection(i, d, 0));
      if (d == Direction::Output && i % 2) assert(select.AssignOutput(i, i + 1, 32));
    }
    uint64_t before[32]; for (size_t i = 0; i < 32; ++i) assert(select.Record(i, before[i]));
    for (Direction role : {Direction::Off, Direction::Input, Direction::Output, Direction(255)})
    for (uint16_t producer : {0,1,2,32}) {
      const bool valid = (role == Direction::Input && producer == 0) ||
                         (role == Direction::Output && producer != 0);
      std::vector<unsigned> choices;
      if (valid) choices.push_back(0);
      for (unsigned ch = 1; ch <= 32; ++ch) {
        const auto s = select.Get(ch - 1);
        const bool allow = valid && s.direction == role &&
          (role == Direction::Input || s.producer == 0 || s.producer == producer);
        assert(CanSelectMapping(select, ch, role, producer) == allow);
        if (allow) choices.push_back(ch);
      }
      assert(CanSelectMapping(select, 0, role, producer) == valid);
      for (uint16_t current = 0; current <= 33; ++current)
      for (int delta : {INT_MIN,-1,0,1,INT_MAX}) {
        unsigned expected = 0;
        if (valid && current <= 32) {
          if (delta == 0) {
            for (unsigned c : choices) if (c == current) expected = c;
          } else {
            unsigned distance = 34;
            for (unsigned c : choices) {
              unsigned d = delta > 0 ? (c + 33 - current) % 33 : (current + 33 - c) % 33;
              if (d == 0) d = 33;
              if (d < distance) { distance = d; expected = c; }
            }
          }
        }
        assert(NextMapping(select, current, delta, role, producer) == expected);
      }
    }
    for (size_t i = 0; i < 32; ++i) { uint64_t after; assert(select.Record(i, after) && after == before[i]); }
  }
  puts("PASS: direction-filtered selectors against independent ordered-list oracle; None, wrap, conflicts, invalid contexts, INT_MIN; no mutation");
  DirectionalMappings bank;
  for (size_t i = 0; i < 32; ++i) {
    assert(bank.Get(i).direction == Direction::Input);
    assert(bank.Get(i).range == Range::Unipolar5);
    assert(!bank.Get(i).valid);
    for (unsigned legacy = 0; legacy <= 3; ++legacy) {
      assert(bank.Restore(i, legacy, 8));
      assert(bank.Get(i).direction == (legacy ? Direction::Input : Direction::Off));
      assert(!bank.Get(i).valid);
    }
    assert(bank.Restore(i, 1, 8));
  }
  assert(bank.SetInputRange(0, Range::Bipolar5));
  for (unsigned code = 0; code < 65536; ++code) {
    assert(bank.Write(3, 1, code));
    phz::ii::VirtualInputs baseline;
    baseline.SetRange(0, Range::Bipolar5);
    assert(baseline.WriteVoltage(1, code));
    assert(bank.InputValue(0) == baseline.Value(0));
  }
  assert(!bank.SetDirection(0, Direction::Output, 1));
  assert(bank.Get(0).direction == Direction::Input);
  assert(bank.SetDirection(0, Direction::Output, 0));
  assert(bank.InputValue(0) == 0 && !bank.Get(0).valid);
  assert(!bank.Write(2, 1, 123) && !bank.Write(3, 1, 123) && !bank.Write(4, 1, 123));
  assert(!bank.AssignOutput(0, 9, 8));
  assert(bank.AssignOutput(0, 3, 8));
  assert(!bank.AssignOutput(0, 4, 8));
  assert(!bank.SetDirection(0, Direction::Off, 0));
  assert(!bank.Publish(0, 4, 123));
  uint16_t reply = 999;
  assert(!bank.ReadVoltage(1, reply) && reply == 999);
  for (int value = -30720; value <= 30720; ++value) {
    assert(bank.Publish(0, 3, value));
    assert(bank.ReadVoltage(1, reply));
    long long want = (static_cast<long long>(value < 0 ? -value : value) * 16 + 7) / 15;
    if (value < 0) want = -want;
    if (want > 32767) want = 32767;
    if (want < -32768) want = -32768;
    int got = reply < 32768 ? int(reply) : int(reply) - 65536;
    assert(got == want);
    assert(bank.InputValue(0) == 0);
  }
  assert(bank.Publish(0, 3, INT_MIN) && bank.ReadVoltage(1, reply) && reply == 32768);
  assert(bank.Publish(0, 3, INT_MAX) && bank.ReadVoltage(1, reply) && reply == 32767);
  assert(bank.Publish(0, 3, 3840) && bank.ReadVoltage(1, reply) && reply == 4096);
  assert(bank.Publish(0, 3, -3840) && bank.ReadVoltage(1, reply) && reply == 61440);
  uint64_t saved = 0;
  assert(bank.Record(0, saved));
  DirectionalMappings restored;
  assert(restored.Restore(0, saved, 8));
  assert(restored.Get(0).producer == 3 && restored.Get(0).direction == Direction::Output);
  assert(!restored.Get(0).valid && restored.Get(0).value == 0);
  for (uint64_t bad : {uint64_t(4), uint64_t(255), saved | (uint64_t(1) << 63),
                       saved ^ (uint64_t(1) << 24), (saved & ~uint64_t(15)),
                       (saved & ~uint64_t(0xf0)) | 0x30}) {
    assert(!restored.Restore(0, bad, 8));
    uint64_t unchanged = 0; assert(restored.Record(0, unchanged) && unchanged == saved);
  }
  assert(bank.Write(3, 2, 8192));
  assert(bank.Get(1).valid && bank.Get(1).value == 7680);
  bank.ClearValues();
  assert(!bank.Get(0).valid && !bank.Get(1).valid);
  assert(bank.Get(0).producer == 3 && bank.Get(0).direction == Direction::Output);
  assert(!bank.ReadVoltage(1, reply));
  assert(bank.AssignOutput(0, 0, 8));
  assert(bank.SetDirection(0, Direction::Input, 0));
  assert(bank.Write(3, 1, 4096) && bank.InputValue(0) == 3840);
  for (int failure = 0; failure < 32; ++failure) {
    auto read = [failure](size_t index, uint64_t& value) {
      value = int(index) == failure ? 255 : 0;
      return true;
    };
    assert(!phz::ii::LoadDirectionalMappings(read, 8, bank));
    assert(bank.InputValue(0) == 3840);
  }
  auto absent = [](size_t, uint64_t&) { return false; };
  assert(phz::ii::LoadDirectionalMappings(absent, 8, bank));
  for (size_t i = 0; i < 32; ++i) assert(bank.Get(i).direction == Direction::Input && !bank.Get(i).valid);
  assert(!bank.Write(3, 0, 1) && !bank.Write(3, 33, 1));
  assert(!bank.SetDirection(32, Direction::Output, 0));
  assert(!bank.Publish(32, 1, 1) && !bank.ReadVoltage(33, reply));
  for (unsigned d = 0; d < 3; ++d) for (unsigned r = 1; r <= 3; ++r)
  for (unsigned producer = 0; producer <= 8; ++producer) {
    if (d != 2 && producer) continue;
    DirectionalMappings source, target;
    assert(source.SetInputRange(0, Range(r)));
    assert(source.SetDirection(0, Direction(d), 0));
    if (d == 2) assert(source.AssignOutput(0, producer, 8));
    uint64_t record = 0; assert(source.Record(0, record));
    assert(target.Restore(0, record, 8));
    const auto actual = target.Get(0);
    assert(unsigned(actual.direction) == d && unsigned(actual.range) == r);
    assert(actual.producer == producer && !actual.valid && actual.value == 0);
    if (producer) assert(!target.Restore(0, record, producer - 1));
  }
  puts("PASS: legacy migration, input compatibility, direction/producer guards, signed output conversion, lifecycle clearing, transactional restore");
  DirectionalMappings routes;
  for (unsigned i : {0u,1u,2u}) assert(routes.SetDirection(i, Direction::Output, 0));
  assert(routes.RouteOutput(3, 1, 8));
  assert(!routes.AssignOutput(2, 3, 8));
  assert(routes.Publish(0, 3, 3840));
  assert(routes.RouteOutput(4, 2, 8));
  assert(!routes.RouteOutput(3, 2, 8)); // cannot steal producer 4's slot
  assert(routes.Get(0).producer == 3 && routes.Get(0).valid);
  assert(!routes.RouteOutput(3, 4, 8)); // input mapping is not a destination
  assert(routes.Get(0).producer == 3 && routes.Get(0).valid);
  assert(routes.RouteOutput(3, 1, 8) && routes.Get(0).valid); // no-op preserves value
  assert(routes.RouteOutput(3, 3, 8));
  assert(routes.Get(0).producer == 0 && !routes.Get(0).valid);
  assert(routes.Get(2).producer == 3 && !routes.Get(2).valid);
  assert(routes.Get(1).producer == 4);
  assert(routes.RouteOutput(3, 0, 8));
  assert(routes.Get(2).producer == 0 && !routes.Get(2).valid);
  assert(!routes.RouteOutput(0, 1, 8) && !routes.RouteOutput(9, 1, 8));
  DirectionalMappings duplicate;
  assert(duplicate.SetDirection(0, Direction::Output, 0));
  assert(duplicate.AssignOutput(0, 3, 8));
  uint64_t duplicate_record = 0; assert(duplicate.Record(0, duplicate_record));
  auto duplicate_read = [duplicate_record](size_t index, uint64_t& value) {
    value = duplicate_record; return index < 2;
  };
  assert(!phz::ii::LoadDirectionalMappings(duplicate_read, 8, bank));
  assert(bank.Get(0).direction == Direction::Input);
  puts("PASS: output reassignment validates before detaching; occupied/input targets preserve original route");
}
