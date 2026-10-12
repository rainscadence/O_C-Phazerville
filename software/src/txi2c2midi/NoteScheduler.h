#pragma once
#include <stdint.h>
namespace txi2c2midi {
// Qwen draft simplified/reviewed by Codex. Main-loop only; 32 total voices.
class NoteScheduler {
 public:
  template<class Sink>
  inline __attribute__((always_inline)) bool On(
      Sink& sink, uint8_t ch, uint8_t note, uint8_t velocity,
      uint16_t duration, uint32_t now) {
    if (ch >= 32 || note > 127 || velocity > 127 || duration > 32767) return false;
    if (!velocity) { Off(sink,ch,note); return true; }
    Off(sink,ch,note);
    for (auto& v : voices_) if (!v.active) {
      v = {now,duration,ch,note,true};
      sink.On(ch,note,velocity); return true;
    }
    return false; // Bounded pool: reject newest, do not steal another note.
  }
  template<class Sink>
  inline __attribute__((always_inline)) void Off(
      Sink& sink, uint8_t ch, uint8_t note) {
    for (auto& v : voices_) if (v.active && v.ch == ch && v.note == note) Release(sink,v);
  }
  template<class Sink>
  inline __attribute__((always_inline)) void Tick(
      Sink& sink, uint32_t now) {
    for (auto& v : voices_)
      if (v.active && v.duration && uint32_t(now-v.start) >= v.duration) Release(sink,v);
  }
  template<class Sink>
  inline __attribute__((always_inline)) void Panic(Sink& sink) {
    for (auto& v : voices_) if (v.active) Release(sink,v);
  }
 private:
  struct Voice { uint32_t start=0; uint16_t duration=0; uint8_t ch=0,note=0; bool active=false; } voices_[32];
  template<class Sink>
  inline __attribute__((always_inline)) void Release(
      Sink& sink, Voice& v) {
    v.active=false; sink.Off(v.ch,v.note);
  }
};
}
