#pragma once
#include <stdint.h>

namespace phz { namespace remote {
// Read-only main-loop snapshot. Unknown apps must not look like the ii grid.
struct UiState {
  // Share initialization instead of duplicating DTCM constants at every call.
  __attribute__((noinline)) UiState() {}
  bool known = false;
  int page = -1;
  int input = 0; // 1..32 only while the ii Maps page/cursor is selected
  bool popup = false;
  int view = -1;
  int select = -1;
  int preset = 0;
  bool clock = false;
  bool quant = false;
  bool midi = false;
  uint8_t qa_scope = 0; // 1: TX address editor; never inferred from a page number.
  int8_t qa_cursor = -1;
  bool qa_editing = false;
  int8_t qa_active = -1, qa_pending = -1; // -1 unknown; 0/off or 7-bit address.
  bool qa_dirty = false, qa_fault = true;
};
}}
