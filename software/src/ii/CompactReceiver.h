// Compact adapter around the firmware queue and mapping bank.
// Caller holds the existing IRQ lock
// across Receive/Take and all lifecycle changes; no bus wait or allocation.
#pragma once
#include "ii/Dispatcher.h"
#include "ii/DirectionalMappings.h"
extern "C" {
#include "compact.h"
}
class CompactReceiver {
 public:
  void Clear() {
    pc_clear(&reply_);
    quantizer_pending_ = false;
    quantizer_epoch_ = 0;
  }
  template<class Lock>
  bool Receive(const uint8_t* bytes, size_t size, uint8_t active_family,
               uint16_t quantizer_scale_count,
               phz::ii::DirectionalMappings& mappings,
               phz::ii::Dispatcher<Lock>& dispatcher) {
    if (bytes && size >= 3 && bytes[0] == 0x50 &&
        bytes[2] == PC_FAMILY_QUANTIZER) {
      pc_qtz_request request{};
      pc_clear(&reply_); // Any new message invalidates a retained OUT sample.
      quantizer_pending_ = false;
      if (!pc_qtz_decode(bytes,size,&request)) return false;
      const auto d=dispatcher.Inspect();
      const bool open=d.active && !d.fault && !d.depth && !d.exhausted;
      uint8_t status=PC_ADMITTED;
      if (active_family != PC_FAMILY_QUADRANTS &&
          active_family != PC_FAMILY_MIDI) status=PC_WRONG_APP;
      else if ((request.op == PC_QTZ_SCALE || request.op == PC_QTZ_REPLACE) &&
               request.scale >= quantizer_scale_count) status=PC_ARGUMENT;
      else if (request.op == PC_QTZ_CAPTURE_CHORD) status=PC_UNSUPPORTED;
      else if (!open) status=PC_BUSY;
      else if (!dispatcher.CaptureQuantizer(request)) status=PC_FULL;
      quantizer_request_=request;
      quantizer_epoch_=d.epoch;
      quantizer_status_=status;
      quantizer_pending_=true;
      return true;
    }
    pc_request request{};
    if (!pc_decode(bytes,size,&request)) { Clear(); return false; }
    quantizer_pending_=false;
    const auto d=dispatcher.Inspect();
    const bool open=d.active && !d.fault && !d.depth && !d.exhausted;
    uint8_t status=PC_READ;
    uint16_t data=0;
    bool valid=false;
    if (request.family==0) {
      const uint8_t capabilities =
          (active_family == PC_FAMILY_QUADRANTS ? 1 : 0) |
          ((active_family == PC_FAMILY_QUADRANTS ||
            active_family == PC_FAMILY_MIDI) ? 2 : 0);
      data=uint16_t(active_family)<<8 | capabilities;
    } else if (active_family!=1) status=PC_WRONG_APP;
    else if (!open) status=PC_BUSY;
    else if (request.op<=2) {
      const auto mapping=mappings.Get(request.mapping-1);
      if (mapping.direction!=phz::ii::DirectionalMappings::Direction::Input ||
          mapping.range==phz::ii::VirtualInputs::Range::Off) status=PC_UNAVAILABLE;
      else {
        // Reuse the existing signal FIFO and main-loop application path.
        // Its lifecycle invalidation purges admitted work at setup changes.
        uint8_t legacy[7]={uint8_t(request.op==1 ? 3 : 4),0,
          request.mapping,0,0,uint8_t(request.value>>8),uint8_t(request.value)};
        status=dispatcher.Capture(legacy,sizeof legacy) ? PC_ADMITTED : PC_FULL;
      }
    } else if (request.op==3) valid=mappings.ReadVoltage(request.mapping,data);
    // VALID uses retained availability, not a new sample of the mapping bank.
    return pc_select(&reply_,bytes,size,d.epoch,status,data,valid);
  }
  bool Take(uint32_t epoch,pc_copy copy,void* context) {
    if (quantizer_pending_) {
      const auto request=quantizer_request_;
      const uint32_t selected_epoch=quantizer_epoch_;
      const uint8_t status=quantizer_status_;
      quantizer_pending_=false;
      quantizer_epoch_=0;
      if (!epoch || epoch != selected_epoch || !copy) return false;
      const uint8_t reply[9]={0x52,1,PC_FAMILY_QUANTIZER,request.op,
        uint8_t(request.tag>>8),uint8_t(request.tag),status,0,0};
      return copy(context,reply,sizeof reply);
    }
    return pc_take(&reply_,epoch,copy,context);
  }
 private:
  pc_response reply_{};
  pc_qtz_request quantizer_request_{};
  uint32_t quantizer_epoch_=0;
  uint8_t quantizer_status_=PC_UNSUPPORTED;
  bool quantizer_pending_=false;
};
