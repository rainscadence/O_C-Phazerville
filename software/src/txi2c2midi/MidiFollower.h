#pragma once
#include <Wire.h>
#include "MidiSettings.h"
#include "MidiPitch.h"
#include "NativeIngress.h"
#include "TxiRaw.h"
#include "TxoEngine.h"
#include "../ii/InterruptLock.h"
#include "../ii/compact.h"

namespace txi2c2midi {
#if defined(ARDUINO_TEENSY41)
extern TxoEngine<phz::ii::InterruptLock> txo_engine;
extern TxiRawQueries<phz::ii::InterruptLock> txi_queries;
#endif
// T41 only, exclusive Wire owner, main-loop Start/Stop/Service.
class MidiFollower {
 public:
  template<class Monitor>
  static bool Start(uint8_t address, uint8_t txo_cv_mask,
                    Monitor& monitor, uint32_t now,
                    uint16_t quantizer_scale_count = 0) {
    Txo().Configure(txo_cv_mask, monitor, now);
    phz::ii::InterruptLock lock;
    if (active_ || address < 8 || address > 0x77) return false;
    control_.Clear();
    Txi().Clear();
    ingress_.Clear();
    ClearCompact();
    quantizer_scale_count_ = quantizer_scale_count;
    Wire.onReceive(Receive); Wire.onRequest(Request);
    NVIC_CLEAR_PENDING(IRQ_LPI2C1);
    active_ = true;
    Wire.begin(address);
    return true;
  }
  static void Stop() {
    phz::ii::InterruptLock lock;
    if (!active_) return;
    NVIC_DISABLE_IRQ(IRQ_LPI2C1);
    IMXRT_LPI2C1.SIER = 0;
    IMXRT_LPI2C1.SCR = LPI2C_SCR_RST;
    IMXRT_LPI2C1.SCR = 0;
    NVIC_CLEAR_PENDING(IRQ_LPI2C1);
    active_ = false;
    ingress_.Clear();
    ClearCompact();
    quantizer_scale_count_ = 0;
    Wire.onReceive(nullptr); Wire.onRequest(nullptr);
    control_.Clear();
    Txi().Clear();
    Txo().Clear();
#if defined(__arm__)
    asm volatile("dsb" ::: "memory");
#endif
  }
  static bool Active() { phz::ii::InterruptLock lock; return active_; }
  template<class Sink, class Monitor>
  static void Service(Sink& sink, Monitor& monitor, uint32_t now) {
    struct UnsupportedQuantizer {
      bool ApplyQuantizerWire(const pc_qtz_request&) { return false; }
    } quantizer;
    Service(sink,monitor,quantizer,now);
  }
  template<class Sink, class Monitor, class Quantizer>
  static void Service(Sink& sink, Monitor& monitor, Quantizer& quantizer,
                      uint32_t now) {
    ServiceImpl(sink,monitor,quantizer,nullptr,now);
  }
  template<class Sink, class Monitor, class Quantizer>
  static inline __attribute__((always_inline)) void ServiceWithPitch(
      Sink& sink, Monitor& monitor, Quantizer& quantizer, MidiPitch& pitch,
      uint32_t now) {
    ServiceImpl(sink,monitor,quantizer,&pitch,now);
  }
 private:
  template<class Sink, class Monitor, class Quantizer>
  static inline __attribute__((always_inline)) void ServiceImpl(
      Sink& sink, Monitor& monitor, Quantizer& quantizer, MidiPitch* pitch,
      uint32_t now) {
    if (Active()) {
      typename NativeIngress<phz::ii::InterruptLock>::Item item;
      if (ingress_.Pop(item)) {
        switch (item.kind) {
          case NativeIngress<phz::ii::InterruptLock>::Kind::Midi:
            control_.Capture(item.bytes,item.size);
            break;
          case NativeIngress<phz::ii::InterruptLock>::Kind::Txo:
            Txo().Receive(item.bytes,item.size);
            break;
          case NativeIngress<phz::ii::InterruptLock>::Kind::Quantizer:
            quantizer.ApplyQuantizerWire(item.quantizer);
            break;
        }
      }
      if (pitch) control_.Service(sink,*pitch,now);
      else control_.Service(sink,now);
      Txo().Service(monitor,now);
    }
  }
 public:
  template<class Values> static void PublishTxi(const Values& values, unsigned count) {
    Txi().Publish(values,count);
  }
  template<class Values>
  static void PublishTxiKnobs(const Values& values, unsigned count) {
    Txi().PublishKnobs(values,count);
  }
  template<class KnobValues, class InputValues>
  static void PublishTxiQuantized(const KnobValues& knobs,
                                  const InputValues& inputs,
                                  unsigned count) {
    Txi().PublishQuantized(knobs,inputs,count);
  }
  template<class Sink> static void ReleaseNotes(Sink& sink) { control_.ReleaseNotes(sink); }
  template<class Monitor>
  static void SetTxoMode(unsigned channel, bool cv, Monitor& monitor,
                         uint32_t now) {
    Txo().SetMode(channel,cv,monitor,now);
  }
  template<class Values>
  static void SnapshotTxo(Values& values) {
    Txo().Snapshot(values);
  }
 private:
  static void Receive(int count)
#if defined(ARDUINO_TEENSY41)
      __attribute__((noinline,noclone,section(".flashmem")))
#endif
  {
    phz::ii::InterruptLock lock;
    if (!active_) return;
    const bool valid = count >= 4 && count <= 16 && Wire.available() == count;
    uint8_t bytes[16]{};
    size_t consumed = 0;
    bool complete = valid;
    while (consumed < BUFFER_LENGTH && Wire.available() > 0) {
      int value = Wire.read();
      if (value < 0) { complete = false; break; }
      if (consumed < sizeof(bytes)) bytes[consumed] = uint8_t(value);
      ++consumed;
    }
    if (complete && consumed == size_t(count) && !Wire.available()) {
      Txi().InvalidateSelection();
      control_.InvalidateSelection();
      if (bytes[0]==0x50) {
        ClearCompact();
        if (consumed >= 3 && bytes[1] == 1 &&
            bytes[2] == PC_FAMILY_QUANTIZER) {
          pc_qtz_request request{};
          if (!pc_qtz_decode(bytes,consumed,&request)) return;
          uint8_t status = PC_ADMITTED;
          if ((request.op == PC_QTZ_SCALE || request.op == PC_QTZ_REPLACE) &&
              request.scale >= quantizer_scale_count_)
            status = PC_ARGUMENT;
          else if (request.op == PC_QTZ_CAPTURE_CHORD)
            status = PC_UNSUPPORTED;
          else if (!ingress_.CaptureQuantizer(request))
            status = PC_FULL;
          compact_quantizer_request_ = request;
          compact_quantizer_status_ = status;
          compact_quantizer_selected_ = true;
          return;
        }
        compact_selected_=true;
        pc_request request{};
        if (pc_decode(bytes,consumed,&request)) {
          pc_select(&compact_,bytes,consumed,1,
                    request.family==0 ? PC_READ : PC_WRONG_APP,
                    request.family==0 ? 0x0202 : 0,0);
        } else pc_clear(&compact_);
        return; // Never reinterpret a malformed compact frame as native MIDI.
      }
      if (Txi().Select(bytes,consumed)) {
        ClearCompact();
        return;
      }
      if (Txo().Accepts(bytes,consumed)) {
        ClearCompact();
        ingress_.CaptureNative(
            NativeIngress<phz::ii::InterruptLock>::Kind::Txo,
            bytes,consumed);
        return;
      }
      ClearCompact();
      if (control_.Select(bytes,consumed)) return;
      if (control_.Accepts(bytes,consumed))
        ingress_.CaptureNative(
            NativeIngress<phz::ii::InterruptLock>::Kind::Midi,
            bytes,consumed);
    } else {
      Txi().InvalidateSelection();
      control_.InvalidateSelection();
      ClearCompact();
    }
  }
  static void Request()
#if defined(ARDUINO_TEENSY41)
      __attribute__((noinline,noclone,section(".flashmem")))
#endif
  {
    phz::ii::InterruptLock lock;
    if (!active_) return;
    if (compact_quantizer_selected_) {
      const auto request = compact_quantizer_request_;
      const uint8_t status = compact_quantizer_status_;
      compact_quantizer_selected_ = false;
      const uint8_t reply[9] = {
        0x52,1,PC_FAMILY_QUANTIZER,request.op,
        uint8_t(request.tag >> 8),uint8_t(request.tag),status,0,0
      };
      Wire.write(reply,sizeof reply);
      return;
    }
    if (compact_selected_) {
      pc_take(&compact_,1,[](void*,const uint8_t* bytes,size_t size) {
        return Wire.write(bytes,size)==size ? 1 : 0;
      },nullptr);
      return;
    }
    uint8_t reply[2];
    if (Txi().Selected()) Txi().Reply(reply);
    else control_.Reply(reply);
    if (Wire.write(reply,2) != 2) {
      Txi().InvalidateSelection();
      control_.Receive(nullptr,0);
    }
  }
  inline static MidiControl<phz::ii::InterruptLock> control_;
  inline static NativeIngress<phz::ii::InterruptLock> ingress_;
#if !defined(ARDUINO_TEENSY41)
  inline static TxiRawQueries<phz::ii::InterruptLock> txi_;
#endif
#if !defined(ARDUINO_TEENSY41)
  inline static TxoEngine<phz::ii::InterruptLock> txo_;
#endif
  static TxoEngine<phz::ii::InterruptLock>& Txo() {
#if defined(ARDUINO_TEENSY41)
    return txo_engine;
#else
    return txo_;
#endif
  }
  static TxiRawQueries<phz::ii::InterruptLock>& Txi() {
#if defined(ARDUINO_TEENSY41)
    return txi_queries;
#else
    return txi_;
#endif
  }
  inline static bool active_ = false;
  // Start/Stop clear this one-shot reply; it never crosses a listener lifetime.
  inline static pc_response compact_{};
  inline static bool compact_selected_=false;
  inline static pc_qtz_request compact_quantizer_request_{};
  inline static uint16_t quantizer_scale_count_=0;
  inline static uint8_t compact_quantizer_status_=PC_UNSUPPORTED;
  inline static bool compact_quantizer_selected_=false;
  static void ClearCompact() {
    pc_clear(&compact_);
    compact_selected_=false;
    compact_quantizer_request_={};
    compact_quantizer_status_=PC_UNSUPPORTED;
    compact_quantizer_selected_=false;
  }
};
}
