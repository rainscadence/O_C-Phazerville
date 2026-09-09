#include "config/Store.h"
#include "ii/AddressState.h"
#include "ii/ReceiveQueue.h"
#include "ii/Follower.h"
#include "ii/Lifecycle.h"
#include "ii/Configuration.h"
#include <Wire.h>
TestWire Wire;
#include "ii/InterruptLock.h"
namespace phz { namespace ii {
namespace { uint32_t test_mask = 0; }
InterruptLock::InterruptLock() : previous_(test_mask) { test_mask = 1; }
InterruptLock::~InterruptLock() { test_mask = previous_; }
}}
#include <assert.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <limits>
using phz::config::Snapshot;
using phz::config::LoadResult;
using Bytes = std::vector<uint8_t>;
struct PowerLoss {};
struct FakeFs;
struct FakeFile {
  FakeFs* fs = nullptr;
  std::string path;
  size_t pos = 0;
  bool writable = false;
  explicit operator bool() const { return fs != nullptr; }
  size_t read(void* dst, size_t count);
  size_t write(const void* src, size_t count);
  size_t position() const { return pos; }
  size_t size() const;
  void flush();
  void close();
};
struct FakeFs {
  std::map<std::string, Bytes> files;
  int cut = -1, operations = 0;
  size_t budget = std::numeric_limits<size_t>::max();
  bool fail_close = false, fail_flush = false, fail_open = false;
  bool fail_readback = false, fail_remove = false;
  int fail_rename = 0, renames = 0;
  void step() { if (++operations == cut) throw PowerLoss{}; }
  bool exists(const char* p) { return files.count(p); }
  bool remove(const char* p) {
    if (fail_remove) return false;
    bool result = files.erase(p);
    step();
    return result;
  }
  bool rename(const char* from, const char* to) {
    if (++renames == fail_rename || !exists(from) || exists(to)) return false;
    files[to] = files.at(from);
    files.erase(from);
    step();
    return true;
  }
  FakeFile open(const char* p, int mode) {
    if (mode) {
      if (fail_open) return {};
      files.try_emplace(p);
      step();
    } else if (!exists(p) ||
               (fail_readback && std::string(p).find(".tmp") != std::string::npos)) {
      return {};
    }
    return {this, p, 0, mode != 0};
  }
};
size_t FakeFile::read(void* dst, size_t count) {
  auto& bytes = fs->files.at(path);
  count = std::min(count, bytes.size() - pos);
  std::copy_n(bytes.data() + pos, count, static_cast<uint8_t*>(dst));
  pos += count;
  return count;
}
size_t FakeFile::write(const void* src, size_t count) {
  count = std::min(count, fs->budget);
  fs->budget -= count;
  auto& bytes = fs->files[path];
  bytes.resize(std::max(bytes.size(), pos + count));
  std::copy_n(static_cast<const uint8_t*>(src), count, bytes.data() + pos);
  pos += count;
  fs->step();
  return count;
}
size_t FakeFile::size() const { return fs->files.at(path).size(); }
void FakeFile::flush() {
  if (writable && fs->fail_flush) fs->files[path].clear();
  if (writable) fs->step();
}
void FakeFile::close() {
  if (writable && fs->fail_close) fs->files[path].clear();
  if (writable) fs->step();
  fs = nullptr;
}
const char* const path = "GLOBALS.CFG";
Snapshot Old() { return {{{0x100, 0x20000}, {0x800, 0x145}, {0x333, 0xabcdef}},
                          {{7, 99}, {19, 42}}}; }
Snapshot New() { auto n = Old(); n.values[0x800] = 0x146; return n; }
Bytes Serialize(const Snapshot& s, bool legacy = false) {
  FakeFs fs;
  auto file = fs.open(path, 2);
  assert(phz::config::Write(file, s));
  Bytes bytes = fs.files[path];
  if (legacy) {
    const size_t second = 12 + s.values.size() * 10;
    bytes[1] = 'Z'; bytes[second + 1] = 'X';
    bytes.resize(bytes.size() - 8);
  }
  return bytes;
}
void AssertOld(FakeFs& fs) {
  Snapshot result;
  assert(phz::config::Load(fs, path, result) != LoadResult::Invalid);
  assert(result == Old());
}
void TestFiles() {
  assert(phz::config::Crc(0xffffffff, reinterpret_cast<const uint8_t*>("123456789"), 9)
         == (0xcbf43926u ^ 0xffffffffu));
  for (bool legacy : {false, true}) {
    for (const Snapshot& s : {Snapshot{}, Old(), Snapshot{{{3, 0}}, {}}}) {
      FakeFs fs;
      fs.files[path] = Serialize(s, legacy);
      Snapshot result = New();
      assert(phz::config::Load(fs, path, result) == LoadResult::Primary);
      assert(result == s);
    }
  }
  auto original = Serialize(Old());
  for (size_t n = 0; n < original.size(); ++n) {
    FakeFs fs;
    fs.files[path] = Bytes(original.begin(), original.begin() + n);
    Snapshot untouched = New();
    assert(phz::config::Load(fs, path, untouched) == LoadResult::Invalid);
    assert(untouched == New());
  }
  for (size_t n = 0; n < original.size(); ++n) {
    FakeFs fs;
    fs.files[path] = original;
    fs.files[path][n] ^= 1;
    Snapshot untouched = New();
    assert(phz::config::Load(fs, path, untouched) == LoadResult::Invalid);
    assert(untouched == New());
  }
  { // Duplicate keys with otherwise correct legacy XOR are invalid.
    FakeFs fs;
    auto bytes = Serialize(Old(), true);
    bytes[22] = bytes[12]; bytes[23] = bytes[13];
    fs.files[path] = bytes;
    Snapshot result;
    assert(phz::config::Load(fs, path, result) == LoadResult::Invalid);
  }
  { // No partial publication after a valid first chunk and broken second.
    FakeFs fs;
    auto bytes = Serialize(Old(), true);
    bytes.back() ^= 1;
    fs.files[path] = bytes;
    Snapshot result = New();
    assert(phz::config::Load(fs, path, result) == LoadResult::Invalid);
    assert(result == New());
  }
  { // Trailing junk and repeated chunks are not a valid file.
    FakeFs fs;
    auto bytes = Serialize(Old(), true);
    bytes.push_back(0);
    fs.files[path] = bytes;
    Snapshot result;
    assert(phz::config::Load(fs, path, result) == LoadResult::Invalid);
  }
}
void TestSave() {
  const auto old = Serialize(Old(), true);
  for (size_t budget = 0; budget < Serialize(New()).size(); ++budget) {
    FakeFs fs;
    fs.files[path] = old;
    fs.budget = budget;
    assert(!phz::config::Save(fs, path, New()));
    AssertOld(fs);
  }
  for (int failure = 0; failure < 7; ++failure) {
    FakeFs fs;
    fs.files[path] = old;
    if (failure == 0) fs.fail_close = true;
    if (failure == 1) fs.fail_flush = true;
    if (failure == 2) fs.fail_open = true;
    if (failure == 3) fs.fail_readback = true;
    if (failure == 4) fs.fail_rename = 1;
    if (failure == 5) fs.fail_rename = 2;
    if (failure == 6) {
      fs.files[std::string(path)+".bak"] = Serialize(New());
      fs.fail_remove = true;
    }
    assert(!phz::config::Save(fs, path, New()));
    fs.fail_readback = false;
    AssertOld(fs);
  }
  FakeFs baseline;
  baseline.files[path] = old;
  assert(phz::config::Save(baseline, path, New()));
  assert(baseline.files[std::string(path)+".bak"] == old); // legacy preserved
  Snapshot read;
  assert(phz::config::Load(baseline, path, read) == LoadResult::Primary);
  assert(read == New());
  const int steps = baseline.operations;
  for (int cut = 1; cut <= steps; ++cut) {
    FakeFs fs;
    fs.files[path] = old; fs.cut = cut;
    try { (void)phz::config::Save(fs, path, New()); } catch (PowerLoss&) {}
    Snapshot result;
    assert(phz::config::Load(fs, path, result) != LoadResult::Invalid);
    assert(result == Old() || result == New());
  }
  { // Staged-only data never becomes active.
    FakeFs fs;
    fs.files[std::string(path)+".tmp"] = Serialize(New());
    assert(phz::config::Load(fs, path, read) == LoadResult::Invalid);
  }
  { // Bad primary + valid backup recovers; failed save keeps backup.
    FakeFs fs;
    fs.files[path] = {1, 2};
    fs.files[std::string(path)+".bak"] = old;
    fs.fail_rename = 1;
    assert(phz::config::Load(fs, path, read) == LoadResult::Backup);
    assert(read == Old());
    assert(!phz::config::Save(fs, path, New()));
    AssertOld(fs);
    fs.fail_rename = 0;
    assert(phz::config::Save(fs, path, New()));
  }
  { // Cannot silently overwrite sole corrupt primary on normal save.
    FakeFs fs;
    fs.files[path] = {1};
    assert(!phz::config::Save(fs, path, New()));
    assert(fs.files[path] == Bytes{1});
  }
  { // First explicit initialization has no previous version.
    FakeFs fs;
    assert(phz::config::Save(fs, path, Old()));
    AssertOld(fs);
  }
  printf("Configuration: legacy migration, byte corruption/truncation, short writes, failure recovery and %d power-cut points PASS\n", steps);
}
void TestAddress() {
  using phz::ii::AddressState;
  AddressState s;
  assert(!s.enabled() && s.dirty());
  s.Boot(true, false, 0);
  assert(s.enabled() && s.active() == 0x45 && !s.dirty());
  for (int value = -512; value < 512; ++value) {
    const uint8_t before = s.pending();
    const bool valid = value >= 8 && value <= 0x77;
    assert(s.Edit(value) == valid);
    assert(s.active() == 0x45);
    assert(s.pending() == (valid ? value : before));
  }
  assert(s.Edit(0x46));
  assert(s.dirty() && !s.reboot_required());
  s.Saved(false);
  assert(s.dirty() && !s.reboot_required());
  s.Saved(true);
  assert(!s.dirty() && s.reboot_required() && s.active() == 0x45);
  uint64_t record = 0;
  assert(s.PendingRecord(record) && record == 0x146);
  s.Boot(true, true, record);
  assert(s.enabled() && s.active() == 0x46 && !s.reboot_required());
  for (const auto record_bad : {uint64_t(0), uint64_t(0x1450000), uint64_t(0x245)}) {
    s.Boot(true, true, record_bad);
    assert(!s.enabled() && s.active() == 0 && s.dirty());
    assert(s.Edit(0x44)); s.Saved(true);
    assert(!s.enabled() && s.reboot_required()); // never hot rebind
  }
  s.Boot(false, false, 0);
  assert(!s.enabled());
  s.FactoryDefaults();
  assert(s.enabled() && s.active() == 0x45 && !s.dirty());
  printf("Address state: legacy/fault/factory paths, edit/save/reboot separation PASS\n");
}
void TestContext() {
  Snapshot bank = New();
  {
    phz::config::ContextScope globals(bank);
    assert(bank == Snapshot{});
    bank = Old();
    {
      phz::config::ContextScope nested(bank);
      bank.values[91] = 17;
    }
    assert(bank == Old());
  }
  assert(bank == New());
  try {
    phz::config::ContextScope scope(bank);
    bank = Old();
    throw PowerLoss{};
  } catch (PowerLoss&) {}
  assert(bank == New());
  puts("Context: nested/failed global operations preserve unsaved bank PASS");
}
void TestReceive() {
  using namespace phz::ii;
  ReceiveQueue queue;
  uint8_t frame[kFrameSize]{1, 0, 1, 0, 1, 0, 2};
  for (int round = 0; round < 10; ++round) {
    for (int i = 0; i < 8; ++i) { frame[6] = i; assert(queue.Capture(frame, sizeof(frame))); }
    frame[6] = 99;
    assert(!queue.Capture(frame, sizeof(frame)));
    for (int i = 0; i < 8; ++i) { assert(queue.Pop(frame)); assert(frame[6] == i); }
    assert(!queue.Pop(frame));
  }
  assert(queue.dropped() == 10);
  queue.Reset();
  for (size_t n = 0; n < 300; ++n) {
    TestWire wire; wire.supply(n);
    Receive(wire, n, BUFFER_LENGTH, queue);
    assert(wire.reads <= int(BUFFER_LENGTH));
    assert(queue.count() == (n == 7 ? 1u : 0u));
    queue.Reset();
  }
  { TestWire wire; wire.supply(7); Receive(wire, 8, BUFFER_LENGTH, queue); assert(queue.count() == 0); }
  { TestWire wire; wire.supply(7); wire.early_empty = true; Receive(wire, 7, BUFFER_LENGTH, queue); assert(queue.count() == 0); }
  { TestWire wire; wire.stuck = true; Receive(wire, 7, BUFFER_LENGTH, queue); assert(wire.reads == int(BUFFER_LENGTH) && !queue.count()); }
  puts("Receive: FIFO ownership, drop-newest, malformed and bounded callbacks PASS");
}
void TestAddressSave() {
  using namespace phz::ii;
  FakeFs fs;
  fs.files[path] = Serialize(Old(), true);
  AddressState state; state.Boot(true, true, 0x145); state.Edit(0x46);
  assert(SaveAddress(fs, path, state));
  Snapshot result;
  assert(phz::config::Load(fs, path, result) == LoadResult::Primary);
  assert(result == New()); // all other config and data records preserved
  assert(state.active() == 0x45 && state.reboot_required());
  state.Edit(0x47); fs.fail_rename = fs.renames + 1;
  assert(!SaveAddress(fs, path, state));
  assert(state.dirty() && state.active() == 0x45);
  for (int scenario = 0; scenario < 4; ++scenario) {
    FakeFs invalid;
    if (scenario == 0) invalid.files[path] = {1, 2, 3};
    if (scenario == 1) invalid.files[path] = Serialize(Snapshot{});
    if (scenario == 2) { auto g = Old(); g.values[0x100] = 0; invalid.files[path] = Serialize(g); }
    const auto original = invalid.files;
    assert(!SaveAddress(invalid, path, state));
    assert(invalid.files == original && state.dirty());
  }
  puts("Actual address-only save: unrelated records preserved, invalid globals rejected, failed save stays dirty PASS");
}
void TestRegistration() {
  using namespace phz::ii;
  AddressState state;
  StartFollower(state);
  assert(!FollowerRegistered() && Wire.starts == 0);
  state.Edit(0x45); state.Saved(true);
  StartFollower(state);
  assert(Wire.starts == 0); // save cannot enable a faulted boot
  state.Boot(true, true, 0x145);
  StartFollower(state);
  assert(FollowerRegistered() && Wire.starts == 1 && Wire.address == 0x45);
  Wire.read_check = [] { assert(test_mask == 1); };
  Wire.supply(7); Wire.receive(7); assert(Wire.reads == 7 && test_mask == 0);
  Wire.supply(300); Wire.receive(300); assert(Wire.reads == int(BUFFER_LENGTH));
  Wire.request(); assert(Wire.transmitted == 0);
  state.Edit(0x46); state.Saved(true); StartFollower(state);
  assert(Wire.starts == 1 && Wire.address == 0x45);
  puts("Actual follower: fault disabled, callbacks before begin, no hot rebind, empty request PASS");
}

void TestNativeDispatch() {
  using namespace phz::ii;
  struct Target final : Backend {
    int32_t native = -1;
    bool Describe(const Write& w, Descriptor& out) override {
      assert(test_mask == 1);
      if (w.slot != 1 || w.parameter != 1) return false;
      out.instance = this; out.type = uint64_t{1} << 40;
      out.slot = w.slot; out.parameter = w.parameter; out.maximum = 65535;
      return true;
    }
    bool Apply(const Prepared& p) override {
      assert(test_mask == 1); native = p.value; return true;
    }
  } target;
  const auto send = [](uint16_t v) {
    Wire.supply(7); Wire.bytes = {1, 0, 1, 0, 1, uint8_t(v >> 8), uint8_t(v)};
    Wire.receive(7);
  };
  SetHostActive(true);
  send(123); send(456);
  assert(target.native == -1 && FollowerDiagnostics().queued == 2);
  ServiceFollower(target); assert(target.native == 123 && FollowerDiagnostics().queued == 1);
  ServiceFollower(target); assert(target.native == 456);
  send(999);
  { Transition outer; { Transition inner; inner.Complete(); } send(10); outer.Complete(); }
  ServiceFollower(target); assert(target.native == 456); // purged and ingress blocked
  { Transition failed; }
  { Transition ordinary; ordinary.Complete(); }
  assert(FollowerDiagnostics().fault);
  { Transition recovery(true); { Transition failed; } recovery.Complete(); }
  assert(FollowerDiagnostics().fault);
  { Transition recovery(true); recovery.Complete(); }
  assert(!FollowerDiagnostics().fault);
  test_mask = 1;
  send(42); ServiceFollower(target);
  assert(test_mask == 1 && target.native == 42);
  test_mask = 0;
  Wire.supply(300); Wire.receive(300);
  assert(Wire.reads == int(BUFFER_LENGTH) && !FollowerDiagnostics().queued);
  assert(FollowerDiagnostics().counts.applied == 3);
  puts("Actual Wire callback → dispatcher → setter and RAII lifecycle, IRQ preservation PASS");
}

int main() { TestFiles(); TestSave(); TestAddress(); TestContext(); TestReceive(); TestAddressSave(); TestRegistration(); TestNativeDispatch(); }
