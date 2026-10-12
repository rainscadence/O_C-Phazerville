#pragma once

#include <stddef.h>
#include <stdint.h>

#include "QuantizerBank.h"

namespace OC {

enum class QuantizerProperty : uint8_t {
  Scale,
  Root,
  Octave,
  Mask,
};

enum class QuantizerCommandKind : uint8_t {
  SetProperty,
  SetRange,
  DisableRange,
  Replace,
  CaptureMask,
};

enum class QuantizerCommandResult : uint8_t {
  Applied,
  Invalid,
  Unavailable,
  Unsupported,
};

// A decoded, transport-independent edit to one Q1-Q8 slot. CaptureMask is the
// atomic boundary for chord capture: the transport resolves a source chord to
// distinct pitch classes, while this service owns the quantizer semantics.
struct QuantizerCommand {
  // Native I2C2MIDI chords contain at most eight notes. Keeping conversion
  // here gives every transport the same duplicate/octave/negative-note rules.
  static constexpr size_t kMaxCaptureNotes = 8;

  QuantizerCommandKind kind = QuantizerCommandKind::SetProperty;
  QuantizerProperty property = QuantizerProperty::Scale;
  unsigned slot = 0;
  int32_t first = 0;
  int32_t second = 0;
  QuantizerSettings replacement;

  static QuantizerCommand Property(unsigned slot, QuantizerProperty property,
                                   int value);
  static QuantizerCommand Range(unsigned slot, int low, int high);
  static QuantizerCommand RangeOff(unsigned slot);
  static QuantizerCommand Replace(unsigned slot,
                                  const QuantizerSettings& settings);
  static QuantizerCommand Capture(unsigned slot, uint16_t pitch_class_mask);
  static QuantizerCommand CaptureNotes(unsigned slot, const int16_t* notes,
                                       size_t count);
};

// The only storage seam required by the command service. Implementations stage
// and validate a complete slot before Replace is called, so invalid commands
// cannot leave a partially edited bank behind.
class QuantizerCommandTarget {
 public:
  virtual ~QuantizerCommandTarget() = default;
  virtual bool Read(unsigned slot, QuantizerSettings& settings) const = 0;
  virtual bool Replace(unsigned slot,
                       const QuantizerSettings& settings) = 0;
  virtual void Changed(unsigned slot) = 0;
};

QuantizerCommandResult ApplyQuantizerCommand(
    QuantizerCommandTarget& target, const QuantizerCommand& command,
    int scale_count, int chromatic_scale);

}  // namespace OC
