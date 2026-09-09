#pragma once
#include <stdint.h>
namespace phz { namespace ii {
class InterruptLock {
 public:
  InterruptLock();
  ~InterruptLock();
  InterruptLock(const InterruptLock&) = delete;
  InterruptLock& operator=(const InterruptLock&) = delete;
 private:
  uint32_t previous_;
};
}} // namespace phz::ii
