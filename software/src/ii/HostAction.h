#pragma once
#include <stddef.h>
#include <stdint.h>

namespace phz { namespace ii {
// Main-loop registry/preset validation must follow successful decoding.
enum class HostActionKind : uint8_t { SelectApplet, LoadPreset };
enum class HostActionResult : uint8_t { Rejected, Applied, Failed };
struct HostAction { HostActionKind kind; uint16_t target; uint16_t applet; };

inline bool DecodeHostAction(const uint8_t* bytes, size_t size, HostAction& out) {
  if (!bytes || size != 7) return false;
  const uint16_t target = (uint16_t(bytes[1]) << 8) | bytes[2];
  const uint16_t applet = (uint16_t(bytes[3]) << 8) | bytes[4];
  const uint16_t reserved = (uint16_t(bytes[5]) << 8) | bytes[6];
  if (reserved) return false;
  if (bytes[0] == 7) {
    if (target < 1 || target > 4 || !applet) return false;
    out = {HostActionKind::SelectApplet, target, applet};
    return true;
  }
  if (bytes[0] == 8) {
    if (target > 31 || applet) return false;
    out = {HostActionKind::LoadPreset, target, 0};
    return true;
  }
  return false;
}
}} // namespace phz::ii
