#pragma once

#include <stdint.h>

namespace OC {

// Optional absolute octave window for an already-quantized pitch. Folding by
// whole octaves preserves the selected scale degree and any microtonal offset.
class OctaveWindow {
 public:
  static constexpr int8_t kMinOctave = -8;
  static constexpr int8_t kMaxOctave = 8;

  bool enabled() const { return low_ <= high_; }
  int8_t low() const { return low_; }
  int8_t high() const { return high_; }

  void Disable() {
    low_ = 1;
    high_ = 0;
  }

  bool Set(int low, int high) {
    if (low < kMinOctave || low > kMaxOctave || high < kMinOctave ||
        high > kMaxOctave || low > high)
      return false;
    low_ = int8_t(low);
    high_ = int8_t(high);
    return true;
  }

  void EnableDefault() { Set(0, 4); }

  void AdjustLow(int delta) {
    if (!enabled()) EnableDefault();
    int value = low_ + delta;
    if (value < kMinOctave) value = kMinOctave;
    if (value > high_) value = high_;
    low_ = int8_t(value);
  }

  void AdjustHigh(int delta) {
    if (!enabled()) EnableDefault();
    int value = high_ + delta;
    if (value < low_) value = low_;
    if (value > kMaxOctave) value = kMaxOctave;
    high_ = int8_t(value);
  }

  int32_t Apply(int32_t pitch, int32_t octave_size) const;

 private:
  // low > high is the compact persisted representation of Range: Off.
  int8_t low_ = 1;
  int8_t high_ = 0;
};

static_assert(sizeof(OctaveWindow) == 2,
              "Octave window must remain compact for Q1-Q8 snapshots");

}  // namespace OC
