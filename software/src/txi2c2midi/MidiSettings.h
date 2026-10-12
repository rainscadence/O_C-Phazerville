#pragma once
#include <stddef.h>
#include <stdint.h>
#include "NoteScheduler.h"

namespace txi2c2midi {
// Upstream ops.ino TIME/SHIFT semantics. Qwen draft, corrected by Codex.
// Main-loop owned; callers synchronize reads with Apply.
class MidiSettings {
 public:
  uint16_t duration[32];
  int8_t shift[32];
  MidiSettings() { Reset(); }
  void Reset() {
    for (unsigned i = 0; i < 32; ++i) { duration[i] = 100; shift[i] = 0; }
  }
  inline __attribute__((always_inline)) bool Apply(
      const uint8_t* data, size_t size) {
    if (!data || !size) return false;
    if (!((data[0] == 2 && size == 4) || (data[0] == 4 && size == 3))) return false;
    if (data[1] > 32) return false;
    const unsigned first = data[1] ? data[1] - 1 : 0;
    const unsigned end = data[1] ? data[1] : 32;
    if (data[0] == 2) {
      const uint16_t value = (uint16_t(data[2]) << 8) | data[3];
      if (value > 32767) return false;
      for (unsigned i = first; i < end; ++i) duration[i] = value;
    } else {
      int value = data[2] < 128 ? int(data[2]) : int(data[2]) - 256;
      if (value < -127) value = -127;
      for (unsigned i = first; i < end; ++i) shift[i] = int8_t(value);
    }
    return true;
  }
};

// Experimental single-address envelope: [1,2,1,native MIDI payload...].
// Callbacks only copy/select. One bounded mutation per main-loop Service call.
template<class Lock>
class MidiControl {
 public:
  void Clear() { Lock lock; head_ = count_ = selected_ = 0; }
  void InvalidateSelection() { Lock lock; selected_ = 0; }
  bool Select(const uint8_t* frame, size_t size) {
    Lock lock;
    selected_ = 0;
    if (!frame || size != 5 || frame[0] != 1 || frame[1] != 2 ||
        frame[2] != 1 || (frame[3] != 1 && frame[3] != 3) ||
        frame[4] < 1 || frame[4] > 32)
      return false;
    selected_ = frame[3];
    channel_ = frame[4] - 1;
    return true;
  }
#if defined(ARDUINO_TEENSY41)
  __attribute__((noinline,noclone,used,section(".flashmem")))
#endif
  static bool Accepts(const uint8_t* frame, size_t size) {
    if (!frame || size < 4 || size > 9 || frame[0] != 1 ||
        frame[1] != 2 || frame[2] != 1)
      return false;
    const uint8_t op = frame[3], ch = size >= 5 ? frame[4] : 255;
    const bool setting = ch <= 32 &&
        ((op == 2 && size == 7 && frame[5] < 128) ||
         (op == 4 && size == 6));
    // First output adapter supports USB-device channels 0..15 only.
    const bool note = ch < 16 &&
        ((op == 20 && size == 7 && frame[5] < 128 && frame[6] < 128) ||
         (op == 21 && size == 6 && frame[5] < 128) ||
         (op == 23 && size == 9 && frame[5] < 128 && frame[6] < 128 &&
          frame[7] < 128));
    return setting || note || (op == 22 && size == 4);
  }
  // Queue an already-classified mutation without disturbing a later read
  // selector that may have arrived before main-loop service.
  bool Capture(const uint8_t* frame, size_t size) {
    Lock lock;
    return CaptureUnlocked(frame,size);
  }
  bool Receive(const uint8_t* frame, size_t size) {
    Lock lock;
    selected_ = 0; // Every write invalidates the previous selection.
    if (!frame || size < 4 || size > 9 || frame[0] != 1 || frame[1] != 2 || frame[2] != 1)
      return false;
    const uint8_t op = frame[3], ch = size >= 5 ? frame[4] : 255;
    if (size == 5 && (op == 1 || op == 3) && ch >= 1 && ch <= 32) {
      selected_ = op; channel_ = ch - 1; return true;
    }
    return CaptureUnlocked(frame,size);
  }
  template<class Sink> void Service(Sink& sink, uint32_t now) {
    struct PassThrough {
      bool Apply(uint8_t, uint8_t note, uint8_t& output) {
        output = note;
        return true;
      }
    } pitch;
    Service(sink,pitch,now);
  }
  template<class Sink, class Pitch>
  inline __attribute__((always_inline)) void Service(
      Sink& sink, Pitch& final_pitch, uint32_t now) {
    Item item;
    int transpose = 0;
    uint16_t duration = 0;
    bool note = false;
    {
      Lock lock;
      if (count_) {
        item = queue_[head_]; head_ = (head_ + 1) % 8; --count_;
        if (item.bytes[0] == 2 || item.bytes[0] == 4) state_.Apply(item.bytes,item.size);
        else {
          note = true;
          if (item.bytes[0] != 22) {
            transpose = state_.shift[item.bytes[1]];
            duration = state_.duration[item.bytes[1]];
          }
        }
      }
    }
    // Transport callbacks cannot enter this main-loop-only musical state.
    // Never perform USB/serial work while holding the interrupt lock.
    if (note) {
      const auto* b = item.bytes;
      if (b[0] == 22) notes_.Panic(sink);
      else if (b[0] == 21) notes_.Off(sink,b[1],b[2]); // Native actual-note semantics.
      else {
        const int pitch = int(b[2]) + transpose;
        if (b[0] == 23) duration = (uint16_t(b[4]) << 8) | b[5];
        uint8_t emitted = 0;
        if (pitch >= 0 && pitch <= 127) {
          if (final_pitch.Apply(b[1],uint8_t(pitch),emitted))
            notes_.On(sink,b[1],emitted,b[3],duration,now);
        }
      }
    }
    notes_.Tick(sink,now);
  }
  template<class Sink> void ReleaseNotes(Sink& sink) { notes_.Panic(sink); }
  void Reply(uint8_t (&out)[2]) const {
    Lock lock;
    out[0] = out[1] = 0;
    if (selected_ == 1) {
      const auto value = state_.duration[channel_];
      out[0] = value >> 8; out[1] = value & 255;
    } else if (selected_ == 3) out[0] = uint8_t(state_.shift[channel_]);
  }
 private:
  struct Item { uint8_t bytes[6]{}, size = 0; } queue_[8];
  bool CaptureUnlocked(const uint8_t* frame, size_t size) {
    if (!Accepts(frame,size) || count_ == 8) return false;
    auto& item = queue_[(head_ + count_) % 8];
    item.size = size - 3;
    for (unsigned i = 0; i < item.size; ++i) item.bytes[i] = frame[i + 3];
    ++count_;
    return true;
  }
  NoteScheduler notes_;
  MidiSettings state_;
  uint8_t head_ = 0, count_ = 0, selected_ = 0, channel_ = 0;
};
}
