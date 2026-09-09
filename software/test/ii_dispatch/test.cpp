#include "ii/Dispatcher.h"
#include <assert.h>
#include <stdio.h>
#include <functional>
#include <limits>
#include <vector>
using namespace phz::ii;
struct TestLock {
  static int mask, depth, unlocks, at;
  static std::function<void()> hook;
  int previous;
  TestLock() : previous(mask) { mask = 1; ++depth; }
  ~TestLock() {
    assert(mask == 1); --depth; mask = previous;
    if (!mask && !depth && ++unlocks == at && hook) {
      auto event = std::move(hook); hook = {}; event();
    }
  }
};
int TestLock::mask = 0, TestLock::depth = 0, TestLock::unlocks = 0, TestLock::at = -1;
std::function<void()> TestLock::hook;
using Engine = Dispatcher<TestLock>;
struct Target : Backend {
  int32_t native = -1;
  int calls = 0;
  bool accepts = true, mode_matches = true;
  Descriptor d;
  std::vector<int32_t> effects;
  Target() { d.instance = &native; d.type = uint64_t{1} << 40; d.minimum = 0; d.maximum = 65535; }
  bool Describe(const Write& w, Descriptor& out) override {
    assert(TestLock::mask);
    if (!mode_matches || w.parameter != 1) return false;
    out = d; out.slot = w.slot; out.parameter = w.parameter; return true;
  }
  bool Apply(const Prepared& p) override {
    assert(TestLock::mask); ++calls;
    if (!accepts) return false;
    native = p.value; effects.push_back(native); return true;
  }
};
void Send(Engine& engine, uint16_t value = 1, uint16_t slot = 1, uint16_t param = 1) {
  uint8_t frame[7] = {1, uint8_t(slot >> 8), uint8_t(slot),
      uint8_t(param >> 8), uint8_t(param), uint8_t(value >> 8), uint8_t(value)};
  engine.Capture(frame, sizeof(frame));
  frame[6] = 222; // accepted queue must own the original bytes
}
void ConvertTests() {
  Descriptor d; int32_t out;
  d.kind = ValueKind::Boolean;
  for (uint32_t code = 0; code <= 65535; ++code) {
    out = -99;
    assert(Convert(d, code, out));
    assert(out == (code >= 32768 ? 1 : 0));
  }
  d.kind = ValueKind::Integer; d.minimum = -32768; d.maximum = 32767;
  for (uint32_t code = 0; code <= 65535; ++code) {
    assert(Convert(d, code, out) && out == int32_t(code) - 32768);
  }
  out = 19; d.minimum = 1; d.maximum = 0;
  assert(!Convert(d, 1, out) && out == 19);
  static const EnumOption options[] = {{0, 0}, {57, 224}, {65535, -123}};
  d.kind = ValueKind::Enumeration; d.options = options; d.option_count = 3;
  for (uint32_t code = 0; code <= 65535; ++code) {
    out = 99;
    const bool found = code == 0 || code == 57 || code == 65535;
    assert(Convert(d, code, out) == found);
    assert(out == (code == 0 ? 0 : code == 57 ? 224 : code == 65535 ? -123 : 99));
  }
  static const EnumOption duplicate[] = {{0, 42}, {1, 2}, {1, 3}};
  d.options = duplicate; out = 123;
  assert(!Convert(d, 0, out) && out == 123);
  d.options = nullptr; assert(!Convert(d, 0, out));
  d.options = options; d.option_count = 257; assert(!Convert(d, 0, out));
  d.option_count = 0; assert(!Convert(d, 0, out));
  d.kind = static_cast<ValueKind>(255); assert(!Convert(d, 0, out));
  Descriptor a, b;
  assert(SameDescriptor(a, b));
#define DIFFERENT(member, val) b = a; b.member = val; assert(!SameDescriptor(a, b))
  DIFFERENT(instance, &out); DIFFERENT(type, uint64_t{1} << 40);
  DIFFERENT(slot, 257); DIFFERENT(parameter, 2);
  DIFFERENT(kind, ValueKind::Boolean); DIFFERENT(minimum, -12);
  DIFFERENT(maximum, 11); DIFFERENT(options, options);
  DIFFERENT(option_count, 1); DIFFERENT(dependencies, uint64_t{1} << 50);
#undef DIFFERENT
  puts("Pure snapshot conversion: all65536 integer/Boolean/enum codes and field identity PASS");
}
void QueueTests() {
  Engine engine; Target target;
  Send(engine); assert(engine.Inspect().counts.blocked == 1);
  engine.SetActive(true);
  for (int n = 0; n < 9; ++n) Send(engine, n);
  auto status = engine.Inspect(); assert(status.queued == 8 && status.overflow == 1);
  for (int n = 0; n < 8; ++n) {
    const auto before = target.calls;
    engine.Service(target);
    assert(target.calls == before + 1 && target.native == n);
  }
  engine.Service(target); assert(target.calls == 8);
  Send(engine, 8, 1, 0); engine.Service(target);
  Send(engine, 8, 0); engine.Service(target);
  uint8_t bad[7] = {2}; engine.Capture(bad, 7); engine.Service(target);
  assert(engine.Inspect().counts.malformed == 3);
  Send(engine, 8, 1, 22); engine.Service(target);
  assert(engine.Inspect().counts.unsupported == 1 && target.calls == 8);
  target.accepts = false; Send(engine); engine.Service(target);
  assert(target.native == 7 && engine.Inspect().counts.unsupported == 2);
  puts("Dispatcher: owned FIFO8, overflow, one service/effect, malformed/unsupported inputs PASS");
}
void LifecycleTests() {
  Engine engine; Target target; engine.SetActive(true); Send(engine);
  engine.Begin(); engine.Begin(); engine.End(true);
  Send(engine); engine.Service(target); assert(target.calls == 0);
  engine.End(true); assert(!engine.Inspect().fault);
  Send(engine, 42); engine.Service(target); assert(target.native == 42);
  engine.Begin(); engine.Begin(); engine.End(false); engine.End(true);
  assert(engine.Inspect().fault);
  engine.Begin(); engine.End(true); assert(engine.Inspect().fault); // ordinary edit no recovery
  engine.Begin(true); engine.Begin(); engine.End(false); engine.End(true);
  assert(engine.Inspect().fault); // inner failure beats recovery
  engine.Begin(true); engine.End(true); assert(!engine.Inspect().fault);
  Send(engine); engine.SetActive(false); engine.SetActive(true); engine.Service(target);
  assert(target.calls == 1); // no replay across host switch
  Engine near_wrap(std::numeric_limits<uint32_t>::max() - 1);
  near_wrap.SetActive(true); near_wrap.Begin(); near_wrap.End(true);
  assert(near_wrap.Inspect().exhausted && near_wrap.Inspect().fault);
  near_wrap.Begin(true); near_wrap.End(true); Send(near_wrap);
  assert(near_wrap.Inspect().exhausted && near_wrap.Inspect().queued == 0);
  puts("Lifecycle: nested/same-type invalidation, sticky failure, explicit recovery, no epoch wrap PASS");
}
void RaceTests() {
  for (int phase : {1, 2}) {
    for (int change = 0; change < 8; ++change) {
      Engine engine; Target target; engine.SetActive(true); Send(engine, 123);
      TestLock::unlocks = 0; TestLock::at = phase;
      TestLock::hook = [&] {
        assert(!TestLock::mask);
        switch(change) {
          case 0: engine.Begin(); engine.End(true); break; // same-type reload
          case 1: engine.SetActive(false); engine.SetActive(true); break;
          case 2: engine.Begin(true); engine.Begin(); engine.End(false); engine.End(true); break;
          case 3: target.mode_matches = false; break;
          case 4: target.d.type ^= uint64_t{1} << 48; break;
          case 5: target.d.dependencies++; break;
          case 6: target.d.maximum = 127; break;
          case 7: engine.Service(target); break; // reject reentrant service
        }
      };
      engine.Service(target);
      if (change <= 3 || (phase == 2 && change <= 6)) assert(target.calls == 0);
      else assert(target.calls == 1);
      assert(!engine.Inspect().busy && !engine.Inspect().prepared);
      TestLock::hook = {}; TestLock::at = -1;
    }
  }
  Engine engine; Target target; engine.SetActive(true); Send(engine);
  TestLock::mask = 1;
  engine.Service(target);
  assert(TestLock::mask == 1 && target.calls == 1); // never enable caller IRQs
  engine.Begin(); engine.End(true); assert(TestLock::mask == 1);
  TestLock::mask = 0;
  puts("Deterministic interrupt interleavings: before/after snapshot, reentrancy and prior-mask restoration PASS");
}
void PreparedCapacityTest() {
  Engine engine; Target target; engine.SetActive(true); Send(engine, 123);
  TestLock::unlocks = 0; TestLock::at = 2;
  TestLock::hook = [&] {
    for (int n = 0; n < 9; ++n) Send(engine, n);
    const auto state = engine.Inspect();
    assert(state.prepared && state.busy && state.queued == 8 && state.overflow == 1);
  };
  engine.Service(target);
  assert(target.native == 123 && target.calls == 1);
  TestLock::at = -1;
  for (int n = 0; n < 8; ++n) { engine.Service(target); assert(target.native == n); }
  assert(target.calls == 9 && engine.Inspect().queued == 0);
  puts("Capacity: exactly one prepared plus eight queued, drop newest and FIFO after preparation PASS");
}

int main() { ConvertTests(); QueueTests(); LifecycleTests(); RaceTests(); PreparedCapacityTest(); }
