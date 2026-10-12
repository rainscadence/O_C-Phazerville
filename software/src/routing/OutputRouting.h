#pragma once
#include <stddef.h>
#include <stdint.h>

namespace phz { namespace routing {

// Logical sources never change identity. Destinations are 0=None, 1..N=jacks.
// Caller owns synchronization (including proposal/commit) and host lifecycle.
// No allocation, hardware operations, UI, or automatic persistence here.
template <size_t N>
class OutputRouting {
  static_assert(N >= 1 && N <= 8, "unsupported output count");
 public:
  class Proposal {
   public:
    bool Valid() const { return origin_ != nullptr; }
    bool Conflict() const { return owner_ >= 0 && size_t(owner_) != source_; }
    int Owner() const { return owner_; } // zero-based source, -1=unowned
   private:
    friend class OutputRouting;
    const OutputRouting* origin_ = nullptr;
    uint64_t revision_ = 0;
    size_t source_ = 0;
    uint8_t destination_ = 0;
    int owner_ = -1;
  };

  OutputRouting() { Identity(); }
  OutputRouting(const OutputRouting&) = delete;
  OutputRouting& operator=(const OutputRouting&) = delete;

  uint8_t Destination(size_t source) const {
    return source < N ? destinations_[source] : 0;
  }
  int Owner(uint8_t destination) const {
    if (!destination || destination > N) return -1;
    for (size_t i = 0; i < N; ++i)
      if (destinations_[i] == destination) return int(i);
    return -1;
  }
  Proposal Propose(size_t source, uint8_t destination) const {
    Proposal p;
    if (source >= N || destination > N) return p;
    p.origin_ = this;
    p.revision_ = revision_;
    p.source_ = source;
    p.destination_ = destination;
    p.owner_ = Owner(destination);
    return p;
  }
  // Destructive reassignment requires explicit approval of this exact proposal.
  // Any intervening configuration change (even away and back) invalidates it.
  bool Commit(const Proposal& p, bool approve_reassignment = false) {
    if (p.origin_ != this || p.revision_ != revision_ ||
        (p.Conflict() && !approve_reassignment)) return false;
    if (destinations_[p.source_] == p.destination_) return true;
    if (!Advance()) return false;
    if (p.Conflict()) destinations_[p.owner_] = 0;
    destinations_[p.source_] = p.destination_;
    return true;
  }
  bool MidiEnabled(size_t source) const {
    return source < N && (midi_mask_ & (1u << source));
  }
  bool SetMidiEnabled(size_t source, bool enabled) {
    if (source >= N) return false;
    if (MidiEnabled(source) == enabled) return true;
    if (!Advance()) return false;
    const uint8_t bit = uint8_t(1u << source);
    midi_mask_ = enabled ? (midi_mask_ | bit) : (midi_mask_ & ~bit);
    return true;
  }
  bool Reset() {
    if (!Advance()) return false;
    Identity();
    return true;
  }
  bool InvalidateProposals() { return Advance(); }

  // Processed logical values are read completely before any output write.
  // A disconnected jack is explicitly zero every frame, never a stale sample.
  // With distinct arrays, logical values are unchanged for ii/self-patching.
  void Render(const int32_t (&logical)[N], int32_t (&physical)[N]) const {
    int32_t scratch[N]{};
    for (size_t i = 0; i < N; ++i)
      if (destinations_[i]) scratch[destinations_[i] - 1] = logical[i];
    for (size_t i = 0; i < N; ++i) physical[i] = scratch[i];
  }

  // In-memory codec only: no preset key is allocated or written by this class.
  // 63..48 magic OR, 47..40 version 1, 39..32 MIDI, 31..0 eight jack nibbles.
  uint64_t Encode() const {
    uint64_t record = (uint64_t(0x4f52) << 48) | (uint64_t(1) << 40) |
                      (uint64_t(midi_mask_) << 32);
    for (size_t i = 0; i < N; ++i) record |= uint64_t(destinations_[i]) << (4*i);
    return record;
  }
  // Decode validates the entire record before mutation. Missing legacy records
  // must be handled explicitly by the caller via Reset, not as corrupt data.
  bool Restore(uint64_t record) {
    if ((record >> 40) != ((uint64_t(0x4f52) << 8) | 1)) return false;
    const uint8_t midi = uint8_t(record >> 32);
    if (midi & ~MidiMask()) return false;
    uint8_t staged[N]{};
    uint16_t seen = 0;
    for (size_t i = 0; i < 8; ++i) {
      const uint8_t d = (record >> (4*i)) & 0xf;
      if (d > N || (i >= N && d) || (d && (seen & (1u << d)))) return false;
      if (i < N) staged[i] = d;
      if (d) seen |= 1u << d;
    }
    if (!Advance()) return false;
    for (size_t i = 0; i < N; ++i) destinations_[i] = staged[i];
    midi_mask_ = midi;
    return true;
  }

 private:
  static uint8_t MidiMask() { return uint8_t((1u << N) - 1); }
  void Identity() {
    for (size_t i = 0; i < N; ++i) destinations_[i] = uint8_t(i + 1);
    midi_mask_ = MidiMask(); // legacy behavior, still gated by Auto MIDI globally
  }
  bool Advance() {
    if (revision_ == UINT64_MAX) return false; // never alias an old approval
    ++revision_;
    return true;
  }
  uint8_t destinations_[N]{};
  uint8_t midi_mask_ = 0;
  uint64_t revision_ = 0;
};

}} // namespace phz::routing
