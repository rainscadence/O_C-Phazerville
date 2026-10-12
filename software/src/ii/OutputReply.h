#pragma once
#include "DirectionalMappings.h"

namespace phz { namespace ii {
// Caller holds the IRQ lock. A value reply is followed by a one-shot validity
// query for the same channel. No applet pointers, allocation or bus operations.
class OutputReply {
 public:
  static constexpr uint8_t kVoltage = 5, kValidity = 6;
  void Clear() { pending_ = false; last_valid_ = false; }
  bool Select(const uint8_t* bytes, size_t size, const DirectionalMappings& bank,
              uint32_t epoch, bool open) {
    pending_ = false;
    if (!bytes || size != 3 || (bytes[0] != kVoltage && bytes[0] != kValidity)) {
      Clear(); return false;
    }
    const uint16_t channel = (uint16_t(bytes[1]) << 8) | bytes[2];
    if (!channel || channel > bank.kCount) { Clear(); return false; }
    command_ = bytes[0]; channel_ = channel; epoch_ = epoch; word_ = 0;
    valid_ = open && bank.ReadVoltage(channel, word_);
    if (command_ == kValidity)
      word_ = open && last_valid_ && last_channel_ == channel && last_epoch_ == epoch;
    last_valid_ = false;
    pending_ = true;
    return true;
  }
  void Take(uint8_t (&bytes)[2], uint32_t epoch, bool open) {
    const bool current = pending_ && open && epoch_ == epoch;
    const uint16_t word = current ? word_ : 0;
    bytes[0] = uint8_t(word >> 8); bytes[1] = uint8_t(word);
    last_valid_ = current && command_ == kVoltage && valid_;
    last_channel_ = channel_; last_epoch_ = epoch_;
    pending_ = false;
  }
 private:
  uint32_t epoch_ = 0, last_epoch_ = 0;
  uint16_t channel_ = 0, last_channel_ = 0, word_ = 0;
  uint8_t command_ = 0;
  bool pending_ = false, valid_ = false, last_valid_ = false;
};
}}
