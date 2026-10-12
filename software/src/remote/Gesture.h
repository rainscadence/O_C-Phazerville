#pragma once
#include <stdint.h>

namespace phz { namespace remote {
// Pure, fixed-storage scheduler. Call all main-loop methods under an IRQ lock;
// Tick runs only in the existing UI poll ISR. No USB or event-queue writes here.
struct Gesture {
  char kind = 0;
  uint8_t button = 0, target = 0;
  int8_t delta = 0;
  static bool Decode(const char* p, unsigned n, Gesture& out) {
    Gesture g;
    if (n == 1 && (p[0] == '?' || p[0] == 'A' || p[0] == 'X')) {
      g.kind = p[0];
    } else if (n == 2 && (p[0] == 't' || p[0] == 'h') && p[1] >= '0' && p[1] <= '6') {
      g.kind = p[0]; g.button = p[1] - '0';
    } else if (n == 2 && (p[0] == 'l' || p[0] == 'r') && (p[1] == '+' || p[1] == '-')) {
      g.kind = p[0]; g.delta = p[1] == '+' ? 1 : -1;
    } else if (n == 3 && p[0] == 'c' && p[1] >= '0' && p[1] <= '6' &&
               p[2] >= '0' && p[2] <= '6' && p[1] != p[2]) {
      g.kind = p[0]; g.button = p[1] - '0'; g.target = p[2] - '0';
    } else if (n == 4 && p[0] == 'e' && p[1] >= '0' && p[1] <= '6' &&
               (p[2] == 'l' || p[2] == 'r') && (p[3] == '+' || p[3] == '-')) {
      g.kind = p[0]; g.button = p[1] - '0'; g.target = p[2] == 'r';
      g.delta = p[3] == '+' ? 1 : -1;
    } else return false;
    out = g; return true;
  }
};

class GestureController {
 public:
  struct Sample { uint8_t buttons = 0; int8_t left = 0, right = 0; };
  struct State { bool armed, busy, conflict; uint32_t sequence; uint8_t held; };
  bool Arm(uint32_t now, bool quiet) {
    if (!quiet || busy_) return false;
    armed_ = true; conflict_ = false; lease_ = now; return true;
  }
  bool Disarm() {
    if (busy_) return false; // never truncate a press into a different gesture
    armed_ = false; return true;
  }
  bool Start(const Gesture& g, uint32_t now, bool quiet, unsigned buttons) {
    if (!Armed(now) || busy_ || !quiet || conflict_) return false;
    if (g.kind != 't' && g.kind != 'h' && g.kind != 'c' &&
        g.kind != 'e' && g.kind != 'l' && g.kind != 'r') return false;
    if ((g.kind == 't' || g.kind == 'h' || g.kind == 'c' || g.kind == 'e') &&
        g.button >= buttons) return false;
    if (g.kind == 'c' && g.target >= buttons) return false;
    gesture_ = g; start_ = now; lease_ = now;
    emitted_ = false; busy_ = true; ++sequence_; return true;
  }
  Sample Tick(uint32_t now, bool physical_activity) {
    Sample s;
    if (physical_activity && (Armed(now) || busy_)) {
      conflict_ = true; armed_ = false;
      // Finish this bounded gesture normally. Abrupt release could turn a
      // long press into a short press with a completely different action.
    }
    if (!busy_) { held_ = 0; return s; }
    const uint32_t age = now - start_;
    const char k = gesture_.kind;
    const uint32_t end = k == 'h' ? 650 : (k == 'c' || k == 'e') ? 160 : 80;
    if (age < end) {
      if (k == 't' || k == 'h' || k == 'c' || k == 'e')
        s.buttons = uint8_t(1u << gesture_.button);
      if (k == 'c' && age >= 40 && age < 120)
        s.buttons |= uint8_t(1u << gesture_.target);
      if (!emitted_ && ((k == 'l' || k == 'r') || (k == 'e' && age >= 40))) {
        const bool right = k == 'r' || (k == 'e' && gesture_.target);
        if (right) s.right = gesture_.delta; else s.left = gesture_.delta;
        emitted_ = true;
      }
    }
    // Release traverses the real 7-tick debouncer before reporting idle.
    if (age >= end + 32) busy_ = false;
    held_ = s.buttons;
    return s;
  }
  State Snapshot(uint32_t now) const { return {Armed(now), busy_, conflict_, sequence_, held_}; }
 private:
  bool Armed(uint32_t now) const { return armed_ && uint32_t(now - lease_) < 30000; }
  Gesture gesture_;
  uint32_t start_ = 0, lease_ = 0, sequence_ = 0;
  bool armed_ = false, busy_ = false, conflict_ = false, emitted_ = false;
  uint8_t held_ = 0;
};
}}
