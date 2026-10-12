#pragma once
#include <cstddef>
#include <cstdint>
namespace phz { namespace ii {
// Qwen-drafted state; supervisor added pending guards and binding identity.
// State only: never clears bindings or authorizes mutation by itself.
class DirectionChangePrompt {
 public:
  void Begin(int slot, unsigned target, size_t users, uint16_t producer,
             uint64_t bindings) {
    if (slot < 0 || slot > 31 || target > 2) { Cancel(); return; }
    slot_ = slot; target_ = target;
    Refresh(users, producer, bindings);
  }
  void Cancel() { slot_ = -1; target_ = 0; users_ = 0; producer_ = 0; bindings_ = 0; }
  bool Pending() const { return slot_ >= 0; }
  int Slot() const { return slot_; }
  unsigned Target() const { return target_; }
  size_t Users() const { return users_; }
  bool Matches(size_t users, uint16_t producer, uint64_t bindings) const {
    return Pending() && users_ == users && producer_ == producer && bindings_ == bindings;
  }
  void Refresh(size_t users, uint16_t producer, uint64_t bindings) {
    if (!Pending()) return;
    users_ = users; producer_ = producer; bindings_ = bindings;
  }
 private:
  int slot_ = -1;
  unsigned target_ = 0;
  size_t users_ = 0;
  uint16_t producer_ = 0;
  uint64_t bindings_ = 0;
};
}}
