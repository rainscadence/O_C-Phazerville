#pragma once
#include <stdint.h>
#include "Gesture.h"

namespace phz { namespace remote {
// Local Qwen draft, corrected: empty frames never replay the previous selector;
// expired/invalid frames drain through LF, never escape into legacy debug input.
class Parser {
 public:
  enum class Result { Pass, Pending, Invalid, Action };
  Result Feed(uint8_t byte, uint32_t now) {
    if (state_ == Idle) {
      if (byte != '~') return Result::Pass;
      state_ = Selector; selector_ = 0; length_ = 0; started_ = now;
      return Result::Pending;
    }
    if (uint32_t(now - started_) >= 250) state_ = Discard;
    if (byte == '\n') {
      Gesture ignored;
      const bool valid = state_ == End ||
        (state_ == Extended && Gesture::Decode(payload_, length_, ignored));
      state_ = Idle;
      return valid ? Result::Action : Result::Invalid;
    }
    if (state_ == Selector && Allowed(byte)) {
      selector_ = char(byte); state_ = byte == 'G' ? Extended : End;
    } else if (state_ == Extended && length_ < sizeof(payload_)) {
      payload_[length_++] = char(byte);
    } else state_ = Discard;
    return Result::Pending;
  }
  char selector() const { return selector_; }
  Gesture gesture() const { Gesture g; Gesture::Decode(payload_, length_, g); return g; }
 private:
  enum State { Idle, Selector, End, Extended, Discard };
  State state_ = Idle;
  char selector_ = 0;
  uint32_t started_ = 0;
  char payload_[4] = {};
  uint8_t length_ = 0;
  static bool Allowed(uint8_t byte) {
    for (const char* p = "?ABlr[]SJKNPCEG"; *p; ++p)
      if (byte == uint8_t(*p)) return true;
    return false;
  }
};
}}
