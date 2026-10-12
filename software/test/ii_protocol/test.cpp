#include "ii/Protocol.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
using namespace phz::ii;
int main() {
  // Exhaust all 16-bit destinations and verify failed decodes preserve output.
  unsigned slots = 0;
  for (unsigned word = 0; word <= 65535; ++word) {
    Slot s{SlotBank::Stereo, 99};
    const bool expected = (word >= 1 && word <= 4) ||
      ((word >> 8) >= 1 && (word >> 8) <= 3 &&
       (word & 255) >= 1 && (word & 255) <= 5);
    assert(DecodeSlot(static_cast<uint16_t>(word), s) == expected);
    if (expected) {
      ++slots;
      assert(s.index == (word <= 4 ? word - 1 : (word & 255) - 1));
      assert(static_cast<unsigned>(s.bank) == (word <= 4 ? 0 : word >> 8));
    } else assert(s.bank == SlotBank::Stereo && s.index == 99);
  }
  assert(slots == 19);
  uint8_t frame[137] = {1, 0x02, 0x05, 0x12, 0x34, 0xff, 0xff};
  for (size_t n = 0; n <= sizeof(frame); ++n) {
    Write w{9, 8, 7};
    assert(DecodeWrite(frame, n, w) == (n == 7));
    if (n == 7) assert(w.slot == 0x205 && w.parameter == 0x1234 && w.value == 65535);
    else assert(w.slot == 9 && w.parameter == 8 && w.value == 7);
  }
  Write w{9, 8, 7};
  assert(!DecodeWrite(nullptr, 7, w));
  for (unsigned command = 0; command <= 255; ++command) {
    frame[0] = static_cast<uint8_t>(command);
    assert(DecodeWrite(frame, 7, w) == (command == 1));
  }
  frame[0] = 1; frame[3] = frame[4] = 0;
  assert(!DecodeWrite(frame, 7, w));
  frame[4] = 1; frame[1] = 0x01; frame[2] = 0x06;
  assert(!DecodeWrite(frame, 7, w));

  const int32_t domains[][2] = {{-126,126},{0,1},{-32768,32767},
    {INT32_MIN,INT32_MAX},{INT32_MIN,INT32_MIN},{INT32_MAX,INT32_MAX},
    {-91,90},{0,200},{-127,127}};
  unsigned cases = 0;
  for (const auto& domain : domains) {
    int32_t prior = domain[0];
    for (unsigned code = 0; code <= 65535; ++code) {
      int32_t value = 42;
      assert(DecodeInteger(static_cast<uint16_t>(code), domain[0], domain[1], value));
      assert(value >= domain[0] && value <= domain[1] && value >= prior);
      // Independently use unsigned span arithmetic for the expected offset.
      const uint64_t span = static_cast<uint64_t>(
        static_cast<int64_t>(domain[1]) - domain[0]);
      const int64_t expected = static_cast<int64_t>(domain[0]) +
        static_cast<int64_t>((span * code + 32767u) / 65535u);
      assert(value == expected);
      if (code == 0) assert(value == domain[0]);
      if (code == 65535) assert(value == domain[1]);
      prior = value; ++cases;
    }
  }
  int32_t unchanged = 42;
  assert(!DecodeInteger(0, 5, -5, unchanged) && unchanged == 42);
  for (unsigned code = 0; code <= 65535; ++code) {
    assert(DecodeBoolean(static_cast<uint16_t>(code)) == (code >= 32768));
    uint8_t native = 0xfe;
    assert(DecodeCvSource(static_cast<uint16_t>(code), native) == (code <= 89));
    const uint8_t expected = code == 0 ? 0 : code <= 8 ? 0x40 + code - 1 :
      code <= 24 ? 0x60 + code - 9 : code <= 56 ? 0x80 + code - 25 : code == 57 ? 0xe0 : 0xc0 + code - 58;
    assert(native == (code <= 89 ? expected : 0xfe));
  }
  for (unsigned address = 0; address < 256; ++address) {
    uint64_t record = 0xdead;
    bool valid = address >= 8 && address <= 0x77;
    assert(EncodeAddress(static_cast<uint8_t>(address), record) == valid);
    if (valid) {
      auto c = DecodeAddress(true, true, record);
      assert(c.status == AddressStatus::Ready && c.address == address);
    } else assert(record == 0xdead);
    assert(DecodeAddress(false, false, record).status == AddressStatus::DisabledFault);
  }
  assert(DecodeAddress(true, false, 0).address == 0x45);
  for (unsigned address = 0; address < 256; ++address) for (int enabled = 0; enabled < 2; ++enabled) {
    uint64_t record = 0xdead;
    const bool valid = address >= 8 && address <= 0x77;
    assert(EncodeSettings(uint8_t(address), enabled, record) == valid);
    if (!valid) { assert(record == 0xdead); continue; }
    const auto decoded = DecodeAddress(true, true, record);
    assert(decoded.address == address);
    assert(decoded.status == (enabled ? AddressStatus::Ready : AddressStatus::DisabledUser));
    assert(DecodeAddress(false, true, record).status == AddressStatus::DisabledFault);
    for (unsigned bit = 17; bit < 64; ++bit)
      assert(DecodeAddress(true, true, record | (uint64_t{1} << bit)).status == AddressStatus::DisabledFault);
  }
  for (unsigned version = 0; version < 256; ++version)
    assert((DecodeAddress(true, true, (uint64_t{version} << 8) | 0x45).status ==
            AddressStatus::Ready) == (version == 1));
  for (unsigned bit = 16; bit < 64; ++bit)
    assert(DecodeAddress(true, true, (uint64_t{1} << bit) | 0x145).status ==
           AddressStatus::DisabledFault);
  printf("PASS: %u scalar cases; all slot/enum/boolean words; frame rejection; address schema.\n", cases);
}
