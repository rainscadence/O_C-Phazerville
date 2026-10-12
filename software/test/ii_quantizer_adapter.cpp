#include <assert.h>

#include "ii/QuantizerAdapter.h"

int main() {
  pc_qtz_request request{};
  OC::QuantizerCommand command;
  request.slot = 8;

  request.op = PC_QTZ_SCALE;
  request.scale = 15;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(command.kind == OC::QuantizerCommandKind::SetProperty &&
         command.property == OC::QuantizerProperty::Scale &&
         command.slot == 7 && command.first == 15);

  request.op = PC_QTZ_ROOT;
  request.root = 11;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(command.property == OC::QuantizerProperty::Root &&
         command.first == 11);

  request.op = PC_QTZ_OCTAVE;
  request.octave = -8;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(command.property == OC::QuantizerProperty::Octave &&
         command.first == -8);

  request.op = PC_QTZ_MASK;
  request.mask = 0xa55a;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(command.property == OC::QuantizerProperty::Mask &&
         command.first == 0xa55a);

  request.op = PC_QTZ_RANGE;
  request.low = -3;
  request.high = 4;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(command.kind == OC::QuantizerCommandKind::SetRange &&
         command.first == -3 && command.second == 4);

  request.op = PC_QTZ_RANGE_OFF;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(command.kind == OC::QuantizerCommandKind::DisableRange);

  request.op = PC_QTZ_REPLACE;
  request.scale = 9;
  request.root = 6;
  request.octave = -2;
  request.mask = 0x0123;
  request.range_enabled = 1;
  request.low = -1;
  request.high = 2;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(command.kind == OC::QuantizerCommandKind::Replace &&
         command.replacement.scale == 9 &&
         command.replacement.root == 6 &&
         command.replacement.octave == -2 &&
         command.replacement.mask == 0x0123 &&
         command.replacement.range.low() == -1 &&
         command.replacement.range.high() == 2);

  request.range_enabled = 0;
  request.low = 1;
  request.high = 0;
  assert(phz::ii::DecodeQuantizerRequest(request, command));
  assert(!command.replacement.range.enabled());

  request.low = 0;
  assert(!phz::ii::DecodeQuantizerRequest(request, command));
  request.low = 1;
  request.op = PC_QTZ_CAPTURE_CHORD;
  assert(!phz::ii::DecodeQuantizerRequest(request, command));
  request.op = 255;
  assert(!phz::ii::DecodeQuantizerRequest(request, command));
  request.op = PC_QTZ_ROOT;
  request.slot = 0;
  assert(!phz::ii::DecodeQuantizerRequest(request, command));
  request.slot = 9;
  assert(!phz::ii::DecodeQuantizerRequest(request, command));
  return 0;
}
