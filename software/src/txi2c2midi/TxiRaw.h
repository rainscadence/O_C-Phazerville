#pragma once

#include <stddef.h>
#include <stdint.h>

namespace txi2c2midi {

// Original TXI raw CV range is signed 15-bit (-16384..16383). Ornate's
// calibrated IOFrame uses 4096 units for 10V, so CVs have an exact x4 mapping.
inline int16_t TxiRawValue(int32_t value) {
  if (value <= -4096) return -16384;
  if (value >= 4096) return 16383;
  return int16_t(value * 4);
}

// Immediate queries for two logical TXI units in the experimental shared
// envelope [version=1,module=0,unit,selector]. In original TXI numbering,
// ports0..3 are PARAM knobs and ports4..7 are CV inputs. Selectors0..7 return
// raw values,8..15 quantized values and16..23 scale-degree note numbers.
template<class Lock>
class TxiRawQueries {
 public:
  template<class Values>
  void Publish(const Values& values, unsigned count) {
    Lock lock;
    if (count > 8) count = 8;
    for (unsigned i = 0; i < count; ++i)
      values_[Source(i,4)] = TxiRawValue(values[i]);
    if (count == 8) available_[0] |= 0xf0f0;
  }

  template<class Values>
  void PublishKnobs(const Values& values, unsigned count) {
    Lock lock;
    if (count > 8) count = 8;
    for (unsigned i = 0; i < count; ++i)
      values_[Source(i,0)] = values[i];
    if (count == 8) available_[0] |= 0x0f0f;
  }

  template<class KnobValues, class InputValues>
  void PublishQuantized(const KnobValues& knobs, const InputValues& inputs,
                        unsigned count) {
    Lock lock;
    if (count > 8) count = 8;
    for (unsigned i = 0; i < count; ++i) {
      const unsigned knob = Source(i,0);
      const unsigned input = Source(i,4);
      values_[16 + knob] = knobs[i].value;
      values_[32 + knob] = knobs[i].note;
      values_[16 + input] = inputs[i].value;
      values_[32 + input] = inputs[i].note;
    }
    if (count == 8) available_[1] = available_[2] = 0xffff;
  }

#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  bool Select(const uint8_t* frame, size_t size) {
    Lock lock;
    selected_ = false;
    if (!frame || size != 4 || frame[0] != 1 || frame[1] != 0 ||
        frame[2] < 1 || frame[2] > 2 || frame[3] > 23)
      return false;
    mode_ = frame[3] >> 3;
    input_ = uint8_t((frame[2] - 1) * 8 + (frame[3] & 7));
    selected_ = true;
    return true;
  }

#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  void InvalidateSelection() { Lock lock; selected_ = false; }
#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  bool Selected() const { Lock lock; return selected_; }
#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  void Reply(uint8_t (&out)[2]) const {
    Lock lock;
    out[0] = out[1] = 0;
    if (!selected_ || !(available_[mode_] & (uint16_t(1) << input_)))
      return;
    const uint16_t value = uint16_t(values_[unsigned(mode_) * 16 + input_]);
    out[0] = value >> 8;
    out[1] = value & 255;
  }
#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  void Clear() {
    Lock lock;
    for (auto& value : values_) value = 0;
    available_[0] = available_[1] = available_[2] = 0;
    selected_ = false;
    input_ = mode_ = 0;
  }

 private:
  static unsigned Source(unsigned channel, unsigned port_offset) {
    return (channel / 4) * 8 + port_offset + channel % 4;
  }

  int16_t values_[3 * 16]{};
  uint16_t available_[3]{};
  uint8_t input_ = 0;
  uint8_t mode_ = 0;
  bool selected_ = false;
};

} // namespace txi2c2midi
