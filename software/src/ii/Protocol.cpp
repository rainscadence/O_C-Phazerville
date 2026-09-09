#include "Protocol.h"

namespace phz { namespace ii {
bool DecodeSlot(uint16_t word, Slot& result) {
  Slot decoded{};
  if (word >= 1 && word <= 4) {
    decoded = {SlotBank::Standard, static_cast<uint8_t>(word - 1)};
  } else {
    const uint16_t bank = word >> 8;
    const uint16_t stage = word & 0xff;
    if (bank < 1 || bank > 3 || stage < 1 || stage > 5) return false;
    decoded = {static_cast<SlotBank>(bank),
               static_cast<uint8_t>(stage - 1)};
  }
  result = decoded;
  return true;
}

static uint16_t ReadWord(const uint8_t* bytes) {
  return (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
}

// Reject the entire transaction; never accept a seven-byte prefix of a longer
// transaction. Output stays untouched on failure.
bool DecodeWrite(const uint8_t* bytes, size_t size, Write& result) {
  if (!bytes || size != kFrameSize || bytes[0] != kWriteParameter) return false;
  const Write decoded{ReadWord(bytes + 1), ReadWord(bytes + 3),
                      ReadWord(bytes + 5)};
  Slot destination{};
  if (!DecodeSlot(decoded.slot, destination) || decoded.parameter == 0)
    return false;
  result = decoded;
  return true;
}

bool DecodeInteger(uint16_t code, int32_t minimum, int32_t maximum,
                          int32_t& result) {
  if (minimum > maximum) return false;
  const int64_t span = static_cast<int64_t>(maximum) - minimum;
  // Full int32 endpoint span times a uint16 code fits comfortably in int64.
  result = static_cast<int32_t>(static_cast<int64_t>(minimum) +
                               (span * code + 32767) / 65535);
  return true;
}

bool DecodeBoolean(uint16_t code) { return code >= 32768; }

// Approved T41 option codes, not the sparse native CVInputMap source bytes.
bool DecodeCvSource(uint16_t code, uint8_t& result) {
  uint8_t native;
  if (code == 0) native = 0;
  else if (code <= 8) native = static_cast<uint8_t>(0x40 + code - 1);
  else if (code <= 24) native = static_cast<uint8_t>(0x60 + code - 9);
  else if (code <= 56) native = static_cast<uint8_t>(0x80 + code - 25);
  else if (code == 57) native = 0xe0;
  else return false;
  result = native;
  return true;
}

// Conservative configurable subset excluding reserved 7-bit addresses.
// Bus-specific collisions must still be checked by the operator.
bool ValidAddress(uint8_t address) {
  return address >= 0x08 && address <= 0x77;
}

// Global key 0x0800: bits0..7 address, bits8..15 schema version1,
// bits16..63 zero. Legacy absence is valid only after the containing file
// has independently passed validation. This is not a filesystem validator.
bool EncodeAddress(uint8_t address, uint64_t& record) {
  if (!ValidAddress(address)) return false;
  record = (uint64_t{1} << 8) | address;
  return true;
}
AddressConfig DecodeAddress(bool fileValid, bool present,
                                   uint64_t record) {
  if (!fileValid) return {0, AddressStatus::DisabledFault};
  if (!present) return {kDefaultAddress, AddressStatus::Ready};
  const uint8_t address = static_cast<uint8_t>(record);
  if ((record >> 8) != 1 || !ValidAddress(address))
    return {0, AddressStatus::DisabledFault};
  return {address, AddressStatus::Ready};
}
}} // namespace phz::ii
