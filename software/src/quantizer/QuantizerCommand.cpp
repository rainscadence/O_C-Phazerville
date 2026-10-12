#include "QuantizerCommand.h"

namespace OC {

QuantizerCommand QuantizerCommand::Property(unsigned slot,
                                             QuantizerProperty property,
                                             int value) {
  QuantizerCommand command;
  command.kind = QuantizerCommandKind::SetProperty;
  command.property = property;
  command.slot = slot;
  command.first = value;
  return command;
}

QuantizerCommand QuantizerCommand::Range(unsigned slot, int low, int high) {
  QuantizerCommand command;
  command.kind = QuantizerCommandKind::SetRange;
  command.slot = slot;
  command.first = low;
  command.second = high;
  return command;
}

QuantizerCommand QuantizerCommand::RangeOff(unsigned slot) {
  QuantizerCommand command;
  command.kind = QuantizerCommandKind::DisableRange;
  command.slot = slot;
  return command;
}

QuantizerCommand QuantizerCommand::Replace(
    unsigned slot, const QuantizerSettings& settings) {
  QuantizerCommand command;
  command.kind = QuantizerCommandKind::Replace;
  command.slot = slot;
  command.replacement = settings;
  return command;
}

QuantizerCommand QuantizerCommand::Capture(unsigned slot,
                                           uint16_t pitch_class_mask) {
  QuantizerCommand command;
  command.kind = QuantizerCommandKind::CaptureMask;
  command.slot = slot;
  command.first = pitch_class_mask;
  return command;
}

QuantizerCommand QuantizerCommand::CaptureNotes(unsigned slot,
                                                const int16_t* notes,
                                                size_t count) {
  uint16_t mask = 0;
  if (notes && count && count <= kMaxCaptureNotes) {
    for (size_t i = 0; i < count; ++i) {
      int pitch_class = notes[i] % 12;
      if (pitch_class < 0) pitch_class += 12;
      mask |= uint16_t(1) << pitch_class;
    }
  }
  return Capture(slot, mask);
}

QuantizerCommandResult ApplyQuantizerCommand(
    QuantizerCommandTarget& target, const QuantizerCommand& command,
    int scale_count, int chromatic_scale) {
  if (command.slot >= QuantizerBank::kCount)
    return QuantizerCommandResult::Invalid;

  QuantizerSettings next;
  if (!target.Read(command.slot, next))
    return QuantizerCommandResult::Unavailable;

  switch (command.kind) {
    case QuantizerCommandKind::SetProperty:
      switch (command.property) {
        case QuantizerProperty::Scale:
          if (command.first < INT16_MIN || command.first > INT16_MAX)
            return QuantizerCommandResult::Invalid;
          next.scale = int16_t(command.first);
          break;
        case QuantizerProperty::Root:
          if (command.first < 0 || command.first > 11)
            return QuantizerCommandResult::Invalid;
          next.root = int8_t(command.first);
          break;
        case QuantizerProperty::Octave:
          if (command.first < -8 || command.first > 8)
            return QuantizerCommandResult::Invalid;
          next.octave = int8_t(command.first);
          break;
        case QuantizerProperty::Mask:
          if (command.first < 1 || command.first > UINT16_MAX)
            return QuantizerCommandResult::Invalid;
          next.mask = uint16_t(command.first);
          break;
        default:
          return QuantizerCommandResult::Invalid;
      }
      break;

    case QuantizerCommandKind::SetRange:
      if (!next.range.Set(command.first, command.second))
        return QuantizerCommandResult::Invalid;
      break;

    case QuantizerCommandKind::DisableRange:
      next.range.Disable();
      break;

    case QuantizerCommandKind::Replace:
      next = command.replacement;
      break;

    case QuantizerCommandKind::CaptureMask:
      next.scale = chromatic_scale;
      next.mask = uint16_t(command.first);
      break;

    default:
      return QuantizerCommandResult::Invalid;
  }

  if (!QuantizerBank::Valid(command.slot, next, scale_count))
    return QuantizerCommandResult::Invalid;
  if (!target.Replace(command.slot, next))
    return QuantizerCommandResult::Unavailable;

  target.Changed(command.slot);
  return QuantizerCommandResult::Applied;
}

}  // namespace OC
