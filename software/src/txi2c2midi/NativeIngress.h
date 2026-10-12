#pragma once

#include <stddef.h>
#include <stdint.h>
#include "../ii/compact.h"

namespace txi2c2midi {

// One bounded admission queue for every mutating command accepted by the
// single-address listener. Read selectors remain immediate, but MIDI, TXO and
// PHZ quantizer writes cross one FIFO before any application state is touched.
template<class Lock>
class NativeIngress {
 public:
  static constexpr size_t kCapacity = 8;
  static constexpr size_t kNativeFrameSize = 9;
  enum class Kind : uint8_t { Midi, Txo, Quantizer };
  struct Item {
    Kind kind = Kind::Midi;
    uint8_t bytes[kNativeFrameSize]{};
    uint8_t size = 0;
    pc_qtz_request quantizer{};
  };

  void Clear() {
    Lock lock;
    head_ = count_ = 0;
  }

#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  bool CaptureNative(Kind kind, const uint8_t* bytes, size_t size) {
    if (kind == Kind::Quantizer || !bytes || !size ||
        size > kNativeFrameSize)
      return false;
    Lock lock;
    if (count_ == kCapacity) return false;
    auto& item = items_[(head_ + count_) % kCapacity];
    item = {};
    item.kind = kind;
    item.size = uint8_t(size);
    for (size_t i = 0; i < size; ++i) item.bytes[i] = bytes[i];
    ++count_;
    return true;
  }

  bool CaptureQuantizer(const pc_qtz_request& request) {
    Lock lock;
    if (count_ == kCapacity) return false;
    auto& item = items_[(head_ + count_) % kCapacity];
    item = {};
    item.kind = Kind::Quantizer;
    item.quantizer = request;
    ++count_;
    return true;
  }

  bool Pop(Item& item) {
    Lock lock;
    if (!count_) return false;
    item = items_[head_];
    head_ = uint8_t((head_ + 1) % kCapacity);
    --count_;
    return true;
  }

  size_t Count() const {
    Lock lock;
    return count_;
  }

 private:
  Item items_[kCapacity]{};
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

}  // namespace txi2c2midi
