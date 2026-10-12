#pragma once
#include <stddef.h>
#include <stdint.h>

// Wire-independent protocol module. Successful decoding is not application
// success. Host selection, descriptor lookup and IRQ ownership belong to the
// dispatcher; this module neither touches hardware nor mutates applet state.
namespace phz { namespace ii {
constexpr uint8_t kWriteParameter = 0x01;
constexpr uint8_t kWriteVirtualCV = 0x02;
constexpr uint8_t kWriteVirtualVoltage = 0x03;
constexpr uint8_t kWriteVirtualNormalized = 0x04;
constexpr size_t kFrameSize = 7;
constexpr uint8_t kDefaultAddress = 0x45;
constexpr uint16_t kSettingsKey = 0x0800;
enum class SlotBank : uint8_t { Standard, MonoLeft, MonoRight, Stereo };
struct Slot { SlotBank bank; uint8_t index; }; // Zero-based bank index.
struct Write { uint16_t slot; uint16_t parameter; uint16_t value; };
enum class AddressStatus : uint8_t { Ready, DisabledFault, DisabledUser };
struct AddressConfig { uint8_t address; AddressStatus status; };

// Every fallible decoder leaves its output argument unchanged on failure.
bool DecodeSlot(uint16_t word, Slot& result);
bool DecodeWrite(const uint8_t* bytes, size_t size, Write& result);
// 0x02 + BE16 channel (1..32) + BE16 zero + BE16 normalized CV.
bool DecodeVirtualCV(const uint8_t* bytes, size_t size, Write& result);
// Commands 2/3/4 share channel + reserved zero + value framing.
bool DecodeVirtualSignal(const uint8_t* bytes, size_t size, Write& result);
bool DecodeInteger(uint16_t code, int32_t minimum, int32_t maximum,
                   int32_t& result);
bool DecodeBoolean(uint16_t code);
bool DecodeCvSource(uint16_t code, uint8_t& result);
bool ValidAddress(uint8_t address);
bool EncodeAddress(uint8_t address, uint64_t& record);
bool EncodeSettings(uint8_t address, bool enabled, uint64_t& record);
// fileValid must come from independent whole-file validation. An absent key
// in a valid legacy file is different from corrupt or missing configuration.
AddressConfig DecodeAddress(bool fileValid, bool present, uint64_t record);
}} // namespace phz::ii
