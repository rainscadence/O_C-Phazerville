#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace phz { namespace ii {

// Only ii assignment identities belong here. Physical destinations, MIDI,
// attenuation, slew and applet parameter blobs are deliberately absent.
// Field IDs must be stable within an applet, never pointer/list/registry order.
enum class RecallKind : uint8_t { Input = 1, Output = 2 };
struct RecallBinding {
  uint16_t field = 0;
  RecallKind kind = RecallKind::Input;
  uint8_t channel = 0; // 0 explicitly unmapped, otherwise I1..I32
};

template<size_t Fields>
struct RecallProfile {
  static_assert(Fields > 0 && Fields <= 255, "bounded field count");
  uint8_t count = 0;
  std::array<RecallBinding, Fields> bindings{};
  bool Valid() const {
    if (count > Fields) return false;
    for (size_t i = 0; i < count; ++i) {
      const auto& b = bindings[i];
      if (!b.field || b.channel > 32 ||
          (b.kind != RecallKind::Input && b.kind != RecallKind::Output)) return false;
      for (size_t j = 0; j < i; ++j) {
        const auto& prior = bindings[j];
        if (b.field == prior.field && b.kind == prior.kind) return false;
        if (b.channel && b.channel == prior.channel &&
            (b.kind == RecallKind::Output || prior.kind == RecallKind::Output)) return false;
      }
    }
    return true;
  }
};

inline uint64_t EncodeRecallBinding(const RecallBinding&);
inline bool DecodeRecallBinding(uint64_t, RecallBinding&);

// Main-loop policy, not a hardware adapter. Commit only after the real applet
// switch succeeds. Removing an owner does NOT promote an existing duplicate.
template<size_t Slots = 4>
class RecallOwnership {
 public:
  bool Select(size_t slot, uint64_t applet) {
    if (slot >= Slots || !applet) return false;
    if (ids_[slot] == applet) return true; // same-type reload is not reselection
    bool first = true;
    for (size_t i = 0; i < Slots; ++i)
      if (i != slot && ids_[i] == applet) first = false;
    ids_[slot] = applet;
    defaults_[slot] = first;
    return true;
  }
  bool Remove(size_t slot) {
    if (slot >= Slots) return false;
    ids_[slot] = 0; defaults_[slot] = false; return true;
  }
  uint64_t Applet(size_t slot) const { return slot < Slots ? ids_[slot] : 0; }
  bool UsesDefaults(size_t slot) const { return slot < Slots && defaults_[slot]; }
  // Whole-preset restore installs its own exact routes, not applet defaults.
  // Restoring must not rewrite defaults or invent first-load order from slot
  // number. Existing instances therefore remain unpromoted until reselected.
  void RestorePreset(const std::array<uint64_t, Slots>& ids) {
    ids_ = ids; defaults_.fill(false);
  }
 private:
  std::array<uint64_t, Slots> ids_{};
  std::array<bool, Slots> defaults_{};
};

// Single-bank cache. Construct a separate staged cache when loading another
// bank, then publish it on validated load. No silent eviction/truncation.
// Full applet IDs are independent of quadrant and registry ordering.
template<size_t Applets, size_t Fields>
class RecallBank {
  static_assert(Applets > 0 && Applets <= 65534 / (Fields + 2), "bounded storage keys");
 public:
  using Profile = RecallProfile<Fields>;
  explicit RecallBank(uint16_t bank = 0) : bank_(bank) {}
  uint16_t Bank() const { return bank_; }
  const Profile* Find(uint64_t id) const {
    for (size_t i = 0; i < count_; ++i) if (entries_[i].id == id) return &entries_[i].profile;
    return nullptr;
  }
  bool Remember(uint64_t id, const Profile& profile) {
    if (!id || !profile.Valid()) return false;
    size_t i = 0;
    while (i < count_ && entries_[i].id != id) ++i;
    if (i == Applets) return false;
    if (i < count_ && Same(entries_[i].profile, profile)) return true;
    if (revision_ == UINT64_MAX) return false;
    entries_[i] = {id, profile};
    if (i == count_) ++count_;
    ++revision_;
    return true;
  }
  bool Dirty() const { return revision_ != saved_; }
  uint64_t Revision() const { return revision_; }
  // Failed saves do not clean the cache. A stale successful save must not
  // acknowledge newer edits made after that snapshot was captured.
  bool Saved(uint64_t revision, bool success) {
    if (!success || revision != revision_) return false;
    saved_ = revision; return true;
  }
  // Write to a separate storage snapshot. The caller's atomic save publishes
  // it only after every record succeeds. Do not mark Saved on export alone.
  template<class Write>
  bool Export(Write write) const {
    for (size_t i=0;i<count_;++i) {
      const uint16_t base = uint16_t(1+i*(Fields+2));
      const auto& entry = entries_[i];
      if (!write(base,entry.id) || !write(base+1,uint64_t(entry.profile.count))) return false;
      for(size_t j=0;j<entry.profile.count;++j)
        if(!write(uint16_t(base+2+j),EncodeRecallBinding(entry.profile.bindings[j]))) return false;
    }
    // Magic/version 'IR', 1; bank identity, count, and record stride identity.
    return write(0,UINT64_C(0x495201) | uint64_t(bank_)<<24 | uint64_t(count_)<<40 | uint64_t(Fields)<<56);
  }
  template<class Read>
  bool Import(Read read) {
    RecallBank staged(bank_);
    return Import(read, staged);
  }
  // Firmware provides heap-backed scratch so a large bank never consumes the
  // real-time stack. Scratch must be a separate object.
  template<class Read>
  bool Import(Read read, RecallBank& staged) {
    if (&staged == this) return false;
    uint64_t header;
    if(!read(0,header) || (header & 0xffffff)!=0x495201 || (header>>56)!=Fields ||
       uint16_t(header>>24)!=bank_) return false;
    const auto count=uint16_t(header>>40);
    if(count>Applets) return false;
    staged.bank_=bank_; staged.count_=0; staged.revision_=staged.saved_=0;
    for(size_t i=0;i<count;++i) {
      const uint16_t base=uint16_t(1+i*(Fields+2));
      uint64_t id,n;
      if(!read(base,id) || !id || staged.Find(id) || !read(base+1,n) || n>Fields) return false;
      Profile profile;profile.count=uint8_t(n);
      for(size_t j=0;j<n;++j) {
        uint64_t word;
        if(!read(uint16_t(base+2+j),word) || !DecodeRecallBinding(word,profile.bindings[j])) return false;
      }
      if(!staged.Remember(id,profile)) return false;
    }
    staged.saved_=staged.revision_;
    *this=staged;
    return true;
  }
 private:
  struct Entry { uint64_t id = 0; Profile profile{}; };
  static bool Same(const Profile& a, const Profile& b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; ++i)
      if (a.bindings[i].field != b.bindings[i].field ||
          a.bindings[i].kind != b.bindings[i].kind ||
          a.bindings[i].channel != b.bindings[i].channel) return false;
    return true;
  }
  uint16_t bank_;
  size_t count_ = 0;
  std::array<Entry, Applets> entries_{};
  uint64_t revision_ = 0, saved_ = 0;
};

// Unambiguous versioned binding record for a future bank-storage adapter.
// bits 0..15 field, 16..21 channel, 22..23 kind, 24..31 version=1;
// upper 32 reserved. No partial output mutation on malformed data.
inline uint64_t EncodeRecallBinding(const RecallBinding& b) {
  return uint64_t(b.field) | (uint64_t(b.channel) << 16) |
         (uint64_t(b.kind) << 22) | (UINT64_C(1) << 24);
}
inline bool DecodeRecallBinding(uint64_t value, RecallBinding& out) {
  RecallBinding candidate{uint16_t(value), RecallKind((value >> 22) & 3),
                          uint8_t((value >> 16) & 63)};
  if ((value >> 24) != 1 || !candidate.field || candidate.channel > 32 ||
      (candidate.kind != RecallKind::Input && candidate.kind != RecallKind::Output)) return false;
  out = candidate; return true;
}

}}
