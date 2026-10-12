#pragma once
#include <stddef.h>
#include <stdint.h>

namespace phz { namespace routing {
// Bounded automatic-output ownership. Explicit MIDI applets are not owned here.
// Caller serializes Tick/Stop. Sink sends exact note/channel/port events.
template <size_t N> class AutoMidi {
  static_assert(N >= 2 && N <= 8 && N % 2 == 0, "paired outputs required");
 public:
  enum class Kind : uint8_t { None, Pitch, Gate, CC };
  struct Config {
    Kind kind = Kind::None;
    uint8_t channel = 0, controller = 0;
    bool enabled = false;
    bool operator==(const Config& b) const {
      return kind == b.kind && channel == b.channel &&
        controller == b.controller && enabled == b.enabled;
    }
  };
  struct Value { uint8_t code = 0; bool gate = false; };

  template<class Sink> void Stop(Sink& sink) {
    for (size_t p = 0; p < N/2; ++p) Release(p, sink);
    for (size_t p = 0; p < N/2; ++p) pairs_[p] = Pair{};
    for (size_t i = 0; i < N; ++i) seen_[i] = false;
  }
  template<class Sink> void StopPair(size_t p, Sink& sink) {
    if (p >= N/2) return;
    Release(p, sink);
    pairs_[p] = Pair{};
    seen_[2*p] = seen_[2*p+1] = false;
  }

  // Gate mode requires both enabled routes and matching MIDI channels. A
  // configured but disabled gate never becomes implicit pitch-only triggering.
  // Enabling/reconfiguring while high waits for a fresh low->high gate.
  template<class Sink> void Tick(const Config (&cfg)[N], const Value (&v)[N],
                                 uint8_t ports, uint32_t pulse_ticks, Sink& sink) {
    for (size_t p = 0; p < N/2; ++p) {
      const size_t a = 2*p, b = a+1;
      auto& state = pairs_[p];
      const bool changed = !seen_[a] || !seen_[b] ||
        !(previous_[a] == cfg[a]) || !(previous_[b] == cfg[b]) || state.ports != ports;
      if (changed) {
        Release(p, sink);
        state = Pair{};
        state.ports = ports;
      }
      const bool gated = cfg[b].kind == Kind::Gate;
      const bool eligible = ports && cfg[a].enabled && cfg[a].channel < 16 &&
        cfg[a].kind == Kind::Pitch && v[a].code < 128 &&
        (!gated || (cfg[b].enabled && cfg[b].channel == cfg[a].channel));
      if (!eligible) { Release(p, sink); state.armed = false; }
      else if (gated) {
        if (!v[b].gate) { Release(p, sink); state.armed = true; }
        else if (state.armed && (!state.active || state.note != v[a].code)) {
          Release(p, sink);
          Acquire(p, cfg[a].channel, v[a].code, ports, sink);
        }
      } else {
        if (state.last_pitch != v[a].code) {
          Release(p, sink);
          Acquire(p, cfg[a].channel, v[a].code, ports, sink);
          state.remaining = pulse_ticks ? pulse_ticks : 1;
          state.last_pitch = v[a].code;
        } else if (state.remaining && !--state.remaining) Release(p, sink);
      }
      for (size_t i = a; i <= b; ++i) {
        if (ports && cfg[i].enabled && cfg[i].channel < 16 &&
            cfg[i].kind == Kind::CC && cfg[i].controller < 128 && v[i].code < 128 &&
            (changed || !seen_[i] || last_cc_[i] != v[i].code)) {
          sink.CC(cfg[i].channel, cfg[i].controller, v[i].code, ports);
          last_cc_[i] = v[i].code;
        }
        previous_[i] = cfg[i];
        seen_[i] = true;
      }
    }
  }

 private:
  struct Pair {
    uint32_t remaining = 0;
    uint8_t channel = 0, note = 0, ports = 0, last_pitch = 255;
    bool active = false, armed = false;
  } pairs_[N/2];
  Config previous_[N]{};
  bool seen_[N]{};
  uint8_t last_cc_[N]{};

  uint8_t OtherPorts(size_t except, uint8_t channel, uint8_t note) const {
    uint8_t ports = 0;
    for (size_t p = 0; p < N/2; ++p)
      if (p != except && pairs_[p].active && pairs_[p].channel == channel && pairs_[p].note == note)
        ports |= pairs_[p].ports;
    return ports;
  }
  template<class Sink> void Release(size_t p, Sink& sink) {
    auto& s = pairs_[p];
    if (s.active) {
      const uint8_t ports = s.ports & ~OtherPorts(p, s.channel, s.note);
      if (ports) sink.Note(false, s.channel, s.note, ports);
      s.active = false;
    }
    s.remaining = 0;
  }
  template<class Sink> void Acquire(size_t p, uint8_t ch, uint8_t note, uint8_t ports, Sink& sink) {
    auto& s = pairs_[p];
    const uint8_t fresh = ports & ~OtherPorts(p, ch, note);
    s.channel = ch; s.note = note; s.ports = ports; s.active = true;
    if (fresh) sink.Note(true, ch, note, fresh);
  }
};
}} // namespace phz::routing
