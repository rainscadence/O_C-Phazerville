#include "QuantizerAdapter.h"

namespace phz { namespace ii {

bool DecodeQuantizerRequest(const pc_qtz_request& request,
                            OC::QuantizerCommand& command) {
  if (request.slot < 1 || request.slot > OC::QuantizerBank::kCount)
    return false;
  const unsigned slot = request.slot - 1;
  switch (request.op) {
    case PC_QTZ_SCALE:
      command = OC::QuantizerCommand::Property(
          slot, OC::QuantizerProperty::Scale, request.scale);
      return true;
    case PC_QTZ_ROOT:
      command = OC::QuantizerCommand::Property(
          slot, OC::QuantizerProperty::Root, request.root);
      return true;
    case PC_QTZ_OCTAVE:
      command = OC::QuantizerCommand::Property(
          slot, OC::QuantizerProperty::Octave, request.octave);
      return true;
    case PC_QTZ_MASK:
      command = OC::QuantizerCommand::Property(
          slot, OC::QuantizerProperty::Mask, request.mask);
      return true;
    case PC_QTZ_RANGE:
      command = OC::QuantizerCommand::Range(
          slot, request.low, request.high);
      return true;
    case PC_QTZ_RANGE_OFF:
      command = OC::QuantizerCommand::RangeOff(slot);
      return true;
    case PC_QTZ_REPLACE: {
      OC::QuantizerSettings settings;
      settings.scale = int16_t(request.scale);
      settings.root = int8_t(request.root);
      settings.octave = request.octave;
      settings.mask = request.mask;
      if (request.range_enabled) {
        if (!settings.range.Set(request.low, request.high)) return false;
      } else {
        if (request.low != 1 || request.high != 0) return false;
        settings.range.Disable();
      }
      command = OC::QuantizerCommand::Replace(slot, settings);
      return true;
    }
    default:
      return false;
  }
}

}}  // namespace phz::ii
