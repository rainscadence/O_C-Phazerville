#pragma once
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <vector>
constexpr size_t BUFFER_LENGTH = 136;
constexpr int IRQ_LPI2C1 = 1;
constexpr uint32_t LPI2C_SCR_RST = 2;
struct TestPort { uint32_t SIER = 0, SCR = 0; };
inline TestPort IMXRT_LPI2C1;
inline bool test_irq_enabled = false;
inline int test_pending_clears = 0;
inline void NVIC_DISABLE_IRQ(int) { test_irq_enabled = false; }
inline void NVIC_CLEAR_PENDING(int) { ++test_pending_clears; }
class TestWire {
 public:
  int reads = 0, starts = 0, transmitted = 0;
  uint8_t address = 0;
  bool stuck = false, early_empty = false;
  void (*receive)(int) = nullptr;
  void (*request)() = nullptr;
  void (*read_check)() = nullptr;
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> reply;
  size_t write_limit = 2;
  size_t write(const uint8_t* data, size_t count) {
    if (count > write_limit) count = write_limit;
    transmitted += count; reply.assign(data, data + count); return count;
  }
  size_t cursor = 0;
  int available() { return stuck ? 7 : (early_empty && reads ? 0 : int(bytes.size() - cursor)); }
  int read() { if (read_check) read_check(); ++reads; return stuck ? -1 : bytes[cursor++]; }
  void onReceive(void (*f)(int)) { receive = f; }
  void onRequest(void (*f)()) { request = f; }
  void begin(uint8_t a) {
    assert(receive && request); // handlers ready before IRQ-capable begin
    ++starts; address = a;
    test_irq_enabled = true;
    IMXRT_LPI2C1.SIER = 15; IMXRT_LPI2C1.SCR = 1;
  }
  void supply(size_t n) { bytes.assign(n, 0); cursor = 0; reads = 0; }
};
extern TestWire Wire;
