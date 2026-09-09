#pragma once
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <vector>
constexpr size_t BUFFER_LENGTH = 136;
class TestWire {
 public:
  int reads = 0, starts = 0, transmitted = 0;
  uint8_t address = 0;
  bool stuck = false, early_empty = false;
  void (*receive)(int) = nullptr;
  void (*request)() = nullptr;
  void (*read_check)() = nullptr;
  std::vector<uint8_t> bytes;
  size_t cursor = 0;
  int available() { return stuck ? 7 : (early_empty && reads ? 0 : int(bytes.size() - cursor)); }
  int read() { if (read_check) read_check(); ++reads; return stuck ? -1 : bytes[cursor++]; }
  void onReceive(void (*f)(int)) { receive = f; }
  void onRequest(void (*f)()) { request = f; }
  void begin(uint8_t a) {
    assert(receive && request); // handlers ready before IRQ-capable begin
    ++starts; address = a;
  }
  void supply(size_t n) { bytes.assign(n, 0); cursor = 0; reads = 0; }
};
extern TestWire Wire;
