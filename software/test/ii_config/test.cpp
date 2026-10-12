#include "config/Store.h"
#include "ii/AddressState.h"
#include "ii/ReceiveQueue.h"
#include "ii/Follower.h"
#include "ii/Lifecycle.h"
#include "ii/Configuration.h"
#include "txi2c2midi/Configuration.h"
#include <Wire.h>
#include "ii/VirtualInputState.h"
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
  assert(s.PendingRecord(record) && record == 0x10246);
  s.Boot(true, true, record);
  assert(s.enabled() && s.active() == 0x46 && !s.reboot_required());
  for (const auto record_bad : {uint64_t(0), uint64_t(0x1450000), uint64_t(0x345)}) {
    s.Boot(true, true, record_bad);
    assert(!s.enabled() && s.active() == 0 && s.dirty());
    assert(s.Edit(0x44)); s.Saved(true);
    assert(!s.enabled() && s.reboot_required()); // never hot rebind
  }
  s.Boot(false, false, 0);
  assert(!s.enabled());
  s.FactoryDefaults();
  assert(!s.enabled() && !s.pending_enabled() && !s.faulted());
  assert(s.active() == 0x45 && !s.dirty() && !s.reboot_required());
  s.EditEnabled(true);
  assert(s.dirty() && !s.enabled() && !s.reboot_required());
  s.Saved(false);
  assert(s.dirty() && !s.reboot_required());
  s.Saved(true);
  assert(!s.dirty() && !s.enabled() && s.reboot_required());
  assert(s.PendingRecord(record) && record == 0x10245);
  s.Boot(true, true, record);
  assert(s.enabled() && !s.reboot_required());
  s.EditEnabled(false);
  assert(s.enabled() && s.dirty() && !s.reboot_required());
  s.Saved(true);
  assert(s.enabled() && s.reboot_required());
  assert(s.PendingRecord(record) && record == 0x245);
  s.Boot(true, true, record);
  assert(!s.enabled() && !s.faulted() && !s.dirty() && !s.reboot_required());
  for (unsigned bit = 17; bit < 64; ++bit) {
    s.Boot(true, true, record | (uint64_t{1} << bit));
    assert(s.faulted() && !s.enabled() && s.dirty());
  }
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
  auto expected = New(); expected.values[0x800] = 0x10246;
  assert(result == expected); // all other config and data records preserved
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
  fs.fail_rename = 0;
  state.EditEnabled(false);
  assert(SaveAddress(fs, path, state));
  assert(state.enabled() && state.reboot_required());
  assert(phz::config::Load(fs, path, result) == LoadResult::Primary);
  expected.values[0x800] = 0x247;
  assert(result == expected); // opt-out preserves unrelated globals too
  state.Boot(true, true, result.values[0x800]);
  assert(!state.enabled() && !state.faulted() && state.active() == 0x47);
  puts("Actual ii settings save: unrelated records preserved, invalid globals rejected, failed save stays dirty, persisted opt-out PASS");
}
void TestRegistration() {
  using namespace phz::ii;
  AddressState state;
  state.FactoryDefaults();
  StartFollower(state, true);
  assert(!FollowerRegistered() && Wire.starts == 0 && !Wire.receive && !Wire.request);
  state.EditEnabled(true); state.Saved(true);
  StartFollower(state, true);
  assert(Wire.starts == 0); // opt-in is not active until reboot
  state.Boot(true, true, 0x246); // explicitly disabled v2 record
  StartFollower(state, true);
  assert(!state.faulted() && Wire.starts == 0);
  state.Boot(false, false, 0);
  StartFollower(state, true);
  assert(!FollowerRegistered() && Wire.starts == 0);
  assert(!Wire.receive && !Wire.request);
  state.Edit(0x45); state.Saved(true);
  StartFollower(state, true);
  assert(Wire.starts == 0); // save cannot enable a faulted boot
  state.Boot(true, true, 0x145);
  assert(state.enabled());
  StartFollower(state, false);
  StartFollower(state, false);
  assert(!FollowerRegistered() && Wire.starts == 0);
  assert(!Wire.receive && !Wire.request); // no peripheral side effects
  assert(state.enabled() && state.active() == 0x45); // no config corruption
  StartFollower(state, true);
  assert(FollowerRegistered() && Wire.starts == 1 && Wire.address == 0x45);
  Wire.read_check = [] { assert(test_mask == 1); };
  Wire.supply(7); Wire.receive(7); assert(Wire.reads == 7 && test_mask == 0);
  Wire.supply(300); Wire.receive(300); assert(Wire.reads == int(BUFFER_LENGTH));
  Wire.request(); assert(Wire.transmitted == 2 && Wire.reply == std::vector<uint8_t>({0,0}));
  state.Edit(0x46); state.Saved(true); StartFollower(state, true);
  assert(Wire.starts == 1 && Wire.address == 0x45);
  puts("Actual follower: unsupported hardware/config fault disabled, callbacks before begin, no hot rebind, unsolicited zero reply PASS");
}

void TestTxAddress() {
  using namespace txi2c2midi;
  DeviceAddressState state;
  assert(state.faulted() && state.dirty());
  state.Boot(true,false,0);
  assert(!state.faulted() && !state.dirty() && state.active()==0);
  for (int value=-256; value<=512; ++value) {
    const bool valid=value==0 || (value>=8 && value<=119);
    const auto old=state.pending();
    assert(state.Edit(value)==valid);
    if (!valid) assert(state.pending()==old);
  }
  for (uint64_t record=0; record<65536; ++record) {
    state.Boot(true,true,record);
    const bool valid=(record>>8)==1 && DeviceAddressState::ValidAddress(record&255);
    assert(state.faulted()==!valid);
    assert(state.active()==(valid ? (record&255) : 0));
  }
  for (int bit=16;bit<64;++bit) {
    state.Boot(true,true,0x146|(uint64_t{1}<<bit)); assert(state.faulted());
  }
  state.Boot(false,false,0); state.Edit(0); state.Saved(true);
  assert(state.faulted() && state.reboot_required() && !state.dirty());
  state.Boot(true,true,0x100); state.Edit(0x46);
  assert(state.dirty() && !state.reboot_required());
  state.Saved(false); assert(state.dirty() && state.active()==0);
  state.Cancel(); assert(state.pending()==0 && !state.dirty());
  auto before=Old(); before.values[kDeviceAddressKey]=0x100;
  auto after=before; after.values[kDeviceAddressKey]=0x146;
  FakeFs baseline; baseline.files[path]=Serialize(before);
  state.Edit(0x46);
  assert(SaveDeviceAddress(baseline,path,state));
  assert(state.active()==0 && state.reboot_required() && !state.dirty());
  Snapshot result;
  assert(phz::config::Load(baseline,path,result)!=LoadResult::Invalid && result==after);
  for(int cut=1;cut<=baseline.operations;++cut) {
    FakeFs fs; fs.files[path]=Serialize(before); fs.cut=cut;
    DeviceAddressState s; s.Boot(true,true,0x100); s.Edit(0x46);
    try { SaveDeviceAddress(fs,path,s); } catch(PowerLoss&) {}
    assert(phz::config::Load(fs,path,result)!=LoadResult::Invalid);
    assert(result==before || result==after);
  }
  state.Edit(0x47); baseline.fail_rename=baseline.renames+1;
  assert(!SaveDeviceAddress(baseline,path,state) && state.dirty());
  FakeFs empty; assert(!SaveDeviceAddress(empty,path,state));
  state.Boot(true,true,0x146); assert(state.active()==0x46 && !state.reboot_required());
  puts("TX address: exhaustive records, edit/cancel/save/reboot, isolated atomic save PASS");
}

void TestCombinedModeSave() {
  using namespace phz::ii;
  auto before=Old(); before.values[txi2c2midi::kModeSettingsKey]=0x100;
  auto after=before; after.values[kSettingsKey]=0x10246;
  after.values[txi2c2midi::kModeSettingsKey]=0x101;
  FakeFs baseline; baseline.files[path]=Serialize(before);
  AddressState address; address.Boot(true,true,0x145); address.Edit(0x46);
  txi2c2midi::ModeState mode; mode.Boot(true,true,0x100);
  mode.Edit(txi2c2midi::Mode::Legacy);
  assert(SaveAddress(baseline,path,address,&mode));
  Snapshot result; assert(phz::config::Load(baseline,path,result)!=LoadResult::Invalid);
  assert(result==after && !mode.dirty() && mode.reboot_required());
  assert(mode.active()==txi2c2midi::Mode::Single && address.active()==0x45);
  for(int cut=1;cut<=baseline.operations;++cut) {
    FakeFs fs; fs.files[path]=Serialize(before); fs.cut=cut;
    AddressState a; a.Boot(true,true,0x145); a.Edit(0x46);
    txi2c2midi::ModeState m; m.Boot(true,true,0x100); m.Edit(txi2c2midi::Mode::Legacy);
    try { SaveAddress(fs,path,a,&m); } catch(PowerLoss&) {}
    assert(phz::config::Load(fs,path,result)!=LoadResult::Invalid);
    assert(result==before || result==after); // Never a split address/mode save.
  }
  mode.Edit(txi2c2midi::Mode::Single); address.Edit(0x47);
  baseline.fail_rename=baseline.renames+1;
  assert(!SaveAddress(baseline,path,address,&mode) && mode.dirty() && address.dirty());
  assert(mode.active()==txi2c2midi::Mode::Single && address.active()==0x45);
  mode.Boot(true,true,0x101); assert(mode.active()==txi2c2midi::Mode::Legacy);
  mode.Boot(true,false,0); assert(mode.active()==txi2c2midi::Mode::Single && !mode.faulted());
  mode.Boot(true,true,0x200); assert(mode.faulted());
  puts("Atomic ii address + TX mode save: power cuts, unrelated records, failure and reboot PASS");
}

void TestNativeDispatch() {
  using namespace phz::ii;
  struct Target final : Backend {
    int32_t native = -1;
    int host_calls = 0;
    std::vector<pc_qtz_request> quantizers;
    HostActionResult host_result = HostActionResult::Applied;
    HostActionResult ApplyHostAction(const HostAction& action) override {
      assert(test_mask == 0); // slow host work must not run with IRQs masked
      assert(action.kind == HostActionKind::SelectApplet && action.target == 2 && action.applet == 56);
      ++host_calls;
      assert(FollowerDiagnostics().depth > 0 && FollowerDiagnostics().queued == 0);
      Wire.supply(7);Wire.bytes={1,0,1,0,1,0,99};Wire.receive(7);
      assert(FollowerDiagnostics().queued == 0); // ingress closed while executing
      return host_result;
    }
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
    bool ApplyQuantizer(const pc_qtz_request& request) override {
      assert(test_mask == 1);
      quantizers.push_back(request);
      return true;
    }
  } target;
  const auto send = [](uint16_t v) {
    Wire.supply(7); Wire.bytes = {1, 0, 1, 0, 1, uint8_t(v >> 8), uint8_t(v)};
    Wire.receive(7);
  };
  SetHostActive(true);
  const auto host = [&] {
    Wire.supply(7);Wire.bytes={7,0,2,0,56,0,0};Wire.receive(7);
  };
  host();send(777);assert(target.host_calls==0);
  ServiceFollower(target);assert(target.host_calls==1&&target.native==-1&&FollowerDiagnostics().queued==0);
  assert(!FollowerDiagnostics().fault&&FollowerDiagnostics().depth==0);
  target.host_result=HostActionResult::Rejected;host();ServiceFollower(target);
  assert(!FollowerDiagnostics().fault&&target.native==-1);
  target.host_result=HostActionResult::Failed;host();ServiceFollower(target);
  assert(FollowerDiagnostics().fault);host();assert(FollowerDiagnostics().queued==0);
  {Transition recovery(true);recovery.Complete();}
  target.host_result=HostActionResult::Applied;
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
  assert(FollowerDiagnostics().counts.applied == 4); // three writes plus host action
  const auto quantizer = [&](const pc_qtz_request& request, uint8_t expected) {
    uint8_t bytes[16]{}; size_t size=0;
    assert(pc_qtz_encode(&request,bytes,sizeof bytes,&size));
    Wire.supply(size);
    for (size_t i=0;i<size;++i) Wire.bytes[i]=bytes[i];
    Wire.receive(int(size));
    Wire.write_limit=9; Wire.request(); Wire.write_limit=2;
    uint8_t status=255;
    assert(Wire.reply.size()==9 &&
           pc_qtz_match(&request,Wire.reply.data(),Wire.reply.size(),&status));
    assert(status==expected);
  };
  SetHostActive(true,PC_FAMILY_QUADRANTS,16);
  pc_qtz_request q{};q.op=PC_QTZ_ROOT;q.slot=1;q.tag=100;q.root=11;
  quantizer(q,PC_ADMITTED);send(314);
  assert(target.quantizers.empty() && target.native==42 &&
         FollowerDiagnostics().queued==2);
  ServiceFollower(target);
  assert(target.quantizers.size()==1 && target.quantizers.back().root==11 &&
         target.native==42);
  ServiceFollower(target);assert(target.native==314);
  q.op=PC_QTZ_SCALE;q.tag++;q.scale=16;
  quantizer(q,PC_ARGUMENT);assert(!FollowerDiagnostics().queued);
  q.op=PC_QTZ_CAPTURE_CHORD;q.tag++;q.source_chord=1;
  quantizer(q,PC_UNSUPPORTED);assert(!FollowerDiagnostics().queued);
  q={};q.op=PC_QTZ_REPLACE;q.slot=8;q.tag=103;q.scale=15;q.root=5;
  q.octave=-2;q.mask=0x0a55;q.range_enabled=1;q.low=-1;q.high=3;
  quantizer(q,PC_ADMITTED);ServiceFollower(target);
  assert(target.quantizers.size()==2 && target.quantizers.back().op==PC_QTZ_REPLACE &&
         target.quantizers.back().mask==0x0a55);
  q.op=PC_QTZ_ROOT;q.tag++;q.root=2;
  quantizer(q,PC_ADMITTED);BeginTransition(false);EndTransition(true);
  ServiceFollower(target);assert(target.quantizers.size()==2);
  SetHostActive(true,PC_FAMILY_MIDI,16);
  q.tag++;quantizer(q,PC_ADMITTED);ServiceFollower(target);
  assert(target.quantizers.size()==3);
  SetHostActive(true,PC_FAMILY_QUADRANTS,16);
  puts("Actual compact quantizer: family/capability, semantic scale validation, shared FIFO order, 16-byte replacement and lifecycle purge PASS");
  const auto signal = [](uint16_t channel, uint16_t code, uint8_t command=2) {
    Wire.supply(7);
    Wire.bytes = {command, uint8_t(channel >> 8), uint8_t(channel), 0, 0,
                  uint8_t(code >> 8), uint8_t(code)};
    Wire.receive(7);
  };
  signal(1, 65535); assert(virtual_inputs.Value(0) == 0);
  ServiceFollower(target); assert(virtual_inputs.Value(0) == 7680);
  assert(target.native == 314); // does not invoke the absolute parameter setter
  signal(32, 32768); ServiceFollower(target); assert(virtual_inputs.Value(31) == 3840);
  virtual_inputs.SetRange(0, VirtualInputs::Range::Bipolar5);
  signal(1, 0); ServiceFollower(target); assert(virtual_inputs.Value(0) == -7680);
  signal(1,4096,3); ServiceFollower(target); assert(virtual_inputs.Value(0)==3840);
  signal(1,uint16_t(-4096),3); ServiceFollower(target); assert(virtual_inputs.Value(0)==-3840);
  signal(1,0,3); ServiceFollower(target); assert(virtual_inputs.Value(0)==0);
  signal(1,8192,4); ServiceFollower(target); assert(virtual_inputs.Value(0)==0);
  signal(1,16384,4); ServiceFollower(target); assert(virtual_inputs.Value(0)==7680);
  signal(1,16385,4); ServiceFollower(target); assert(virtual_inputs.Value(0)==7680);
  signal(1,0,4); ServiceFollower(target); assert(virtual_inputs.Value(0)==-7680);
  signal(1,0,3); BeginTransition(false); EndTransition(true); ServiceFollower(target);
  assert(!virtual_inputs.Get(0).received); // new formats obey invalidation too
  signal(1, 65535);
  { Transition outer; { Transition inner; inner.Complete(); } outer.Complete(); }
  ServiceFollower(target);
  assert(virtual_inputs.Value(0) == 0 && virtual_inputs.Value(31) == 0);
  assert(!virtual_inputs.Get(0).received);
  assert(virtual_inputs.Get(0).range == VirtualInputs::Range::Bipolar5);
  signal(1, 65535); test_mask = 1; ServiceFollower(target);
  assert(test_mask == 1 && virtual_inputs.Value(0) == 7680); test_mask = 0;
  SetHostActive(false); assert(virtual_inputs.Value(0) == 0);
  signal(1, 65535); SetHostActive(true); ServiceFollower(target);
  assert(virtual_inputs.Value(0) == 0);
  puts("Actual Wire callback -> virtual CV bank, deferred effect, lifecycle clearing, IRQ preservation PASS");
  puts("Actual Wire callback → dispatcher → setter and RAII lifecycle, IRQ preservation PASS");
}

void TestOutputReadback() {
  using namespace phz::ii;
  SetHostActive(true);
  virtual_inputs.Reset();
  auto select=[](uint8_t cmd, uint16_t channel) {
    Wire.supply(3); Wire.bytes={cmd,uint8_t(channel>>8),uint8_t(channel)}; Wire.receive(3);
  };
  auto take=[]() -> uint16_t {
    const auto previous=Wire.transmitted; Wire.request();
    assert(Wire.transmitted==previous+2 && Wire.reply.size()==2);
    return (uint16_t(Wire.reply[0])<<8)|Wire.reply[1];
  };
  auto query=[&](uint8_t cmd,uint16_t channel){select(cmd,channel);return take();};
  int32_t values[8]={3840,-3840,0,7680,-7680,15360,2147483647,(-2147483647-1)};
  const uint16_t expected[8]={4096,uint16_t(-4096),0,8192,uint16_t(-8192),16384,32767,32768};
  for(size_t i=0;i<8;++i) {
    assert(virtual_inputs.SetDirection(i,DirectionalMappings::Direction::Output,0));
    assert(virtual_inputs.AssignOutput(i,uint16_t(i+1),8));
  }
  assert(query(5,1)==0 && query(6,1)==0); // unpublished
  auto epoch=CaptureOutputEpoch(); assert(epoch);
  PublishOutputs(epoch,values,8);
  for(size_t i=0;i<8;++i) {
    assert(query(5,i+1)==expected[i]); assert(query(6,i+1)==1);
    assert(query(6,i+1)==0); // one shot; valid zero distinct from missing
  }
  assert(query(5,9)==0 && query(6,9)==0); // Input
  assert(query(5,0)==0 && query(6,0)==0);
  assert(query(5,33)==0 && query(6,33)==0);
  assert(query(5,1)==4096 && query(6,2)==0); // wrong channel
  select(5,1); values[0]=7680; PublishOutputs(epoch,values,8);
  assert(take()==4096 && query(6,1)==1); // receive-time snapshot
  select(5,1); BeginTransition(false); EndTransition(true);
  assert(take()==0 && query(6,1)==0);
  PublishOutputs(epoch,values,8); assert(query(5,1)==0 && query(6,1)==0); // stale frame
  epoch=CaptureOutputEpoch(); PublishOutputs(epoch,values,8);
  assert(query(5,1)==8192);
  BeginTransition(false); EndTransition(true);
  assert(query(6,1)==0); // lifecycle changed after value reply
  epoch=CaptureOutputEpoch(); PublishOutputs(epoch,values,8);
  select(5,1); Wire.supply(4); Wire.receive(4); assert(take()==0);
  assert(query(5,1)==8192); Wire.supply(7); Wire.receive(7); assert(query(6,1)==0);
  select(5,1); Wire.supply(300); Wire.receive(300);
  assert(Wire.reads==int(BUFFER_LENGTH) && take()==0 && query(6,1)==0);
  select(5,1); assert(take()==8192); assert(take()==0 && query(6,1)==0);
  select(5,1); Wire.write_limit=1; Wire.request(); Wire.write_limit=2;
  assert(query(6,1)==0); // incomplete TX-buffer copy cannot validate
  SetHostActive(false); PublishOutputs(epoch,values,8);
  assert(query(5,1)==0 && query(6,1)==0 && CaptureOutputEpoch()==0);
  SetHostActive(true); epoch=CaptureOutputEpoch();
  PublishOutputs(epoch,values,7); assert(query(5,1)==0 && query(6,1)==0);
  test_mask=1; PublishOutputs(epoch,values,8); assert(query(5,1)==8192 && query(6,1)==1);
  assert(test_mask==1); test_mask=0;
  BeginTransition(false); PublishOutputs(epoch,values,8); EndTransition(false);
  assert(CaptureOutputEpoch()==0 && query(5,1)==0 && query(6,1)==0);
  BeginTransition(true); EndTransition(true);
  puts("Actual follower readback: snapshot/validity pairing, eight outputs, signed conversion, stale-frame rejection, malformed drains, lifecycle and IRQ masks PASS");
}

void TestHostIdentity() {
  using namespace phz::ii;
  struct Target:Backend {
    HostIdentity state{{56,8,99,100},0,1};int snapshots=0;bool supported=true;
    bool Describe(const Write&,Descriptor&)override{return false;}
    bool Apply(const Prepared&)override{return false;}
    bool SnapshotHostIdentity(HostIdentity& out)override{assert(test_mask==1);++snapshots;out=state;return supported;}
  } target;
  auto select=[](uint8_t cmd,uint16_t arg){Wire.supply(3);Wire.bytes={cmd,uint8_t(arg>>8),uint8_t(arg)};Wire.receive(3);};
  auto take=[](){Wire.request();assert(Wire.reply.size()==2);return uint16_t(uint16_t(Wire.reply[0])<<8|Wire.reply[1]);};
  auto query=[&](uint8_t cmd,uint16_t arg){select(cmd,arg);return take();};
  SetHostActive(true);
  assert(query(9,1)==65535); // no published snapshot, not applet zero
  ServiceFollower(target);int count=target.snapshots;
  for(uint16_t slot=1;slot<=4;++slot)assert(query(9,slot)==target.state.applets[slot-1]);
  assert(query(10,0)==0 && query(11,0)==1 && target.snapshots==count);
  for(uint32_t arg=0;arg<=65535;++arg){
    if(arg<1 || arg>4)assert(query(9,arg)==65535);
    if(arg){assert(query(10,arg)==65535);assert(query(11,arg)==65535);}
  }
  select(9,2);target.state.applets[1]=56;ServiceFollower(target);
  assert(take()==8 && query(9,2)==56); // select-time snapshot, not late live lookup
  assert(take()==65535); // consumed reply cannot be replayed
  select(9,1);BeginTransition(false);EndTransition(true);
  assert(take()==65535 && query(10,0)==65535);
  ServiceFollower(target);assert(query(10,0)==0);
  target.state.bank=255;target.state.preset=31;target.state.applets[0]=UINT64_C(0x100000038);
  ServiceFollower(target);assert(query(9,1)==65535 && query(10,0)==255 && query(11,0)==31);
  target.state.preset=-1;ServiceFollower(target);assert(query(11,0)==65535);
  target.state.bank=-1;target.state.preset=0;ServiceFollower(target);assert(query(10,0)==65535 && query(11,0)==65535);
  target.state.bank=0;target.state.preset=0;ServiceFollower(target);assert(query(11,0)==0);
  select(10,0);Wire.write_limit=1;Wire.request();Wire.write_limit=2;assert(query(10,0)==65535);
  ServiceFollower(target);target.supported=false;ServiceFollower(target);assert(query(10,0)==65535);
  target.supported=true;ServiceFollower(target);SetHostActive(false);assert(query(10,0)==65535);
  SetHostActive(true);ServiceFollower(target);BeginTransition(false);EndTransition(false);
  assert(query(10,0)==65535);BeginTransition(true);EndTransition(true);ServiceFollower(target);
  test_mask=1;assert(query(10,0)==0 && test_mask==1);test_mask=0;
  select(10,0);Wire.supply(300);Wire.receive(300);assert(Wire.reads==int(BUFFER_LENGTH));
  assert(query(10,0)==65535);ServiceFollower(target);
  // Identity queries must break a voltage-validity pair, not validate another reply.
  virtual_inputs.Reset();virtual_inputs.SetDirection(0,DirectionalMappings::Direction::Output,0);
  virtual_inputs.AssignOutput(0,1,8);int32_t values[8]={3840};PublishOutputs(CaptureOutputEpoch(),values,8);
  assert(query(5,1)==4096 && query(10,0)==0 && query(6,1)==0);

  LoadedSetupIdentity setup;HostIdentity state;setup.CopyTo(state);
  assert(state.bank==-1 && state.preset==-1);
  setup.PresetLoaded(1);setup.CopyTo(state);assert(state.preset==-1);
  setup.BankLoaded(0);setup.PresetLoaded(1);setup.CopyTo(state);assert(state.bank==0 && state.preset==1);
  setup.BankLoaded(0);setup.CopyTo(state);assert(state.preset==1);
  setup.BankLoaded(2);setup.CopyTo(state);assert(state.bank==2 && state.preset==-1);
  setup.PresetLoaded(32);setup.CopyTo(state);assert(state.preset==-1);
  puts("Actual follower identity queries: four applets, bank/preset zero, unavailable sentinel, all arguments, epoch invalidation, no IRQ lookup, short reply and loaded-state tracking PASS");
}

void TestFollowerStopRestart() {
  using namespace phz::ii;
  SetHostActive(true);
  Wire.supply(7); Wire.bytes = {1,0,1,0,1,0,99}; Wire.receive(7);
  assert(FollowerDiagnostics().queued == 1);
  auto old_receive = Wire.receive;
  auto old_request = Wire.request;
  const auto epoch = FollowerDiagnostics().epoch;
  StopFollower();
  assert(!FollowerRegistered() && !test_irq_enabled);
  assert(IMXRT_LPI2C1.SIER == 0 && IMXRT_LPI2C1.SCR == 0);
  assert(!Wire.receive && !Wire.request);
  assert(FollowerDiagnostics().queued == 0 && !FollowerDiagnostics().active);
  assert(FollowerDiagnostics().epoch != epoch);
  const int reads = Wire.reads, writes = Wire.transmitted;
  old_receive(7); old_request(); // Stale callback references cannot mutate state.
  assert(Wire.reads == reads && Wire.transmitted == writes);
  const int clears = test_pending_clears;
  StopFollower(); assert(test_pending_clears == clears);
  uint64_t record = 0;
  assert(EncodeSettings(0x46, true, record));
  AddressState state; state.Boot(true, true, record);
  assert(state.enabled());
  const int starts = Wire.starts;
  StartFollower(state, true);
  assert(FollowerRegistered() && Wire.starts == starts + 1 && Wire.address == 0x46);
  assert(test_irq_enabled && !FollowerDiagnostics().active);
  Wire.request(); assert(Wire.reply == std::vector<uint8_t>({0,0}));
  StopFollower();
  puts("Actual follower stop/restart: queues, callbacks, register shutdown and inactive restart PASS");
}
int main() { TestFiles(); TestSave(); TestAddress(); TestContext(); TestReceive(); TestAddressSave(); TestCombinedModeSave(); TestTxAddress(); TestRegistration(); TestNativeDispatch(); TestOutputReadback(); TestHostIdentity(); TestFollowerStopRestart(); }
