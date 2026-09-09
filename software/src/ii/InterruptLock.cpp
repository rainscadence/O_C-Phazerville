#include "InterruptLock.h"
#if defined(ARDUINO_TEENSY41)
namespace phz { namespace ii {
InterruptLock::InterruptLock() {
  asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(previous_) :: "memory");
}
InterruptLock::~InterruptLock() {
  asm volatile("dmb\n\tmsr primask, %0" :: "r"(previous_) : "memory");
}
}} // namespace phz::ii
#endif
