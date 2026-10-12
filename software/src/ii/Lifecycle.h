#pragma once
#include <stdint.h>
#if defined(ARDUINO_TEENSY41)
namespace phz { namespace ii {
void BeginTransition(bool recovery);
void EndTransition(bool success);
void SetHostActive(bool active, uint8_t family = 1,
                   uint16_t quantizer_scale_count = 32768);
}}
#endif
namespace phz { namespace ii {
// Success must be explicit; early return after partial mutation stays closed.
// Recovery=true is reserved for a complete restore/reinitialization, not a
// single-slot edit. Inner failure cannot be hidden by an outer success.
class Transition {
 public:
  explicit Transition(bool recovery = false) {
#if defined(ARDUINO_TEENSY41)
    BeginTransition(recovery);
#else
    (void)recovery;
#endif
  }
  ~Transition() {
#if defined(ARDUINO_TEENSY41)
    EndTransition(success_);
#endif
  }
  void Complete() { success_ = true; }
  Transition(const Transition&) = delete;
  Transition& operator=(const Transition&) = delete;
 private:
  bool success_ = false;
};
#if !defined(ARDUINO_TEENSY41)
inline void SetHostActive(bool, uint8_t = 1, uint16_t = 32768) {}
#endif
}} // namespace phz::ii
