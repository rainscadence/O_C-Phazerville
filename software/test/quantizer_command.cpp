#include <assert.h>

#include "quantizer/QuantizerCommand.h"

namespace {

class Target : public OC::QuantizerCommandTarget {
 public:
  bool Read(unsigned slot, OC::QuantizerSettings& out) const override {
    if (!readable || slot >= OC::QuantizerBank::kCount) return false;
    out = slots[slot];
    return true;
  }

  bool Replace(unsigned slot,
               const OC::QuantizerSettings& settings) override {
    if (!writable || slot >= OC::QuantizerBank::kCount) return false;
    slots[slot] = settings;
    ++replace_count;
    return true;
  }

  void Changed(unsigned slot) override {
    changed_slot = int(slot);
    ++change_count;
  }

  OC::QuantizerSettings slots[OC::QuantizerBank::kCount];
  bool readable = true;
  bool writable = true;
  int replace_count = 0;
  int change_count = 0;
  int changed_slot = -1;
};

constexpr int kScaleCount = 16;
constexpr int kChromaticScale = 3;

void ExpectApplied(Target& target, const OC::QuantizerCommand& command) {
  assert(OC::QuantizerCommandResult::Applied == OC::ApplyQuantizerCommand(
      target, command, kScaleCount, kChromaticScale));
}

}  // namespace

int main() {
  Target target;
  for (auto& slot : target.slots) {
    slot.scale = 1;
    slot.root = 2;
    slot.octave = -1;
    slot.mask = 0x0ab5;
  }

  ExpectApplied(target, OC::QuantizerCommand::Property(
      2, OC::QuantizerProperty::Scale, 7));
  assert(target.slots[2].scale == 7);
  assert(target.slots[2].root == 2);
  ExpectApplied(target, OC::QuantizerCommand::Property(
      2, OC::QuantizerProperty::Root, 11));
  ExpectApplied(target, OC::QuantizerCommand::Property(
      2, OC::QuantizerProperty::Octave, 4));
  ExpectApplied(target, OC::QuantizerCommand::Property(
      2, OC::QuantizerProperty::Mask, 0x0555));
  assert(target.slots[2].root == 11);
  assert(target.slots[2].octave == 4);
  assert(target.slots[2].mask == 0x0555);

  ExpectApplied(target, OC::QuantizerCommand::Range(2, -2, 3));
  assert(target.slots[2].range.enabled());
  assert(target.slots[2].range.low() == -2);
  assert(target.slots[2].range.high() == 3);
  ExpectApplied(target, OC::QuantizerCommand::RangeOff(2));
  assert(!target.slots[2].range.enabled());

  OC::QuantizerSettings replacement;
  replacement.scale = 9;
  replacement.root = 6;
  replacement.octave = 1;
  replacement.mask = 0x0123;
  assert(replacement.range.Set(-1, 1));
  ExpectApplied(target, OC::QuantizerCommand::Replace(5, replacement));
  assert(target.slots[5].scale == 9);
  assert(target.slots[5].root == 6);
  assert(target.slots[5].range.high() == 1);

  target.slots[4] = replacement;
  const int16_t chord[] = {0, 4, 7, 8, 12, -4, 20, 7};
  ExpectApplied(target, OC::QuantizerCommand::CaptureNotes(4, chord, 8));
  assert(target.slots[4].scale == kChromaticScale);
  assert(target.slots[4].mask ==
         ((1u << 0) | (1u << 4) | (1u << 7) | (1u << 8)));
  assert(target.slots[4].root == replacement.root);
  assert(target.slots[4].octave == replacement.octave);
  assert(target.slots[4].range.low() == replacement.range.low());
  assert(target.slots[4].range.high() == replacement.range.high());

  const int committed = target.replace_count;
  const int notified = target.change_count;
  const auto before = target.slots[2];
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::CaptureNotes(2, nullptr, 0),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::CaptureNotes(2, chord, 9),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::Property(
          2, OC::QuantizerProperty::Mask, 0),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::Range(2, 3, -2),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::Property(
          8, OC::QuantizerProperty::Scale, 1),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::Property(
          256, OC::QuantizerProperty::Scale, 1),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::Property(
          2, OC::QuantizerProperty::Root, 256),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::Property(
          2, OC::QuantizerProperty::Octave, 256),
      kScaleCount, kChromaticScale));
  assert(OC::QuantizerCommandResult::Invalid == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::Property(
          2, OC::QuantizerProperty::Mask, 0x10000),
      kScaleCount, kChromaticScale));
  assert(target.replace_count == committed);
  assert(target.change_count == notified);
  assert(target.slots[2].scale == before.scale);
  assert(target.slots[2].mask == before.mask);

  target.readable = false;
  assert(OC::QuantizerCommandResult::Unavailable == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::RangeOff(0),
      kScaleCount, kChromaticScale));
  target.readable = true;
  target.writable = false;
  assert(OC::QuantizerCommandResult::Unavailable == OC::ApplyQuantizerCommand(
      target, OC::QuantizerCommand::RangeOff(0),
      kScaleCount, kChromaticScale));
  assert(target.change_count == notified);

  assert(target.changed_slot == 4);
  return 0;
}
