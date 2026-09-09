#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <unordered_map>
#include <utility>

// The same implementation is used with Teensy's FS/File and the host fault
// adapter. All operations are main-loop only, never interrupt safe.
namespace phz { namespace config {
using Map = std::unordered_map<uint16_t, uint64_t>;
struct Snapshot {
  Map values;
  Map data;
  bool operator==(const Snapshot& rhs) const {
    return values == rhs.values && data == rhs.data;
  }
};
enum class LoadResult { Invalid, Primary, Backup };

// A global operation temporarily owns empty maps and restores the caller's
// unsaved bank on every return path, including nested saves and failures.
class ContextScope {
 public:
  explicit ContextScope(Snapshot& current) : current_(current) {
    saved_.values.swap(current_.values);
    saved_.data.swap(current_.data);
  }
  ~ContextScope() {
    saved_.values.swap(current_.values);
    saved_.data.swap(current_.data);
  }
  ContextScope(const ContextScope&) = delete;
  ContextScope& operator=(const ContextScope&) = delete;
 private:
  Snapshot& current_;
  Snapshot saved_;
};

inline uint32_t Crc(uint32_t crc, const uint8_t* bytes, size_t n) {
  while (n--) {
    crc ^= *bytes++;
    for (int b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
  }
  return crc;
}
inline uint64_t Get(const uint8_t* bytes, size_t n) {
  uint64_t value = 0;
  for (size_t i = 0; i < n; ++i) value |= uint64_t(bytes[i]) << (8 * i);
  return value;
}
inline void Put(uint8_t* bytes, size_t n, uint64_t value) {
  for (size_t i = 0; i < n; ++i) bytes[i] = value >> (8 * i);
}

template<class File>
bool ReadChunk(File& file, char kind, Map& map, uint32_t& crc,
               bool& modern) {
  uint8_t header[12];
  if (file.read(header, sizeof(header)) != sizeof(header) ||
      header[0] != 'P') return false;
  if (kind == 'Z') modern = header[1] == 'z';
  if (header[1] != (modern ? kind + ('a' - 'A') : kind)) return false;
  crc = Crc(crc, header, sizeof(header));
  const size_t count = Get(header + 2, 2);
  const uint64_t expected = Get(header + 4, 8);
  if (file.position() > file.size() ||
      count > (file.size() - file.position()) / 10) return false;
  uint64_t checksum = 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t record[10];
    if (file.read(record, sizeof(record)) != sizeof(record)) return false;
    crc = Crc(crc, record, sizeof(record));
    const uint16_t key = Get(record, 2);
    const uint64_t value = Get(record + 2, 8);
    if (!map.emplace(key, value).second) return false; // duplicate key
    checksum ^= value;
  }
  return checksum == expected;
}

template<class File>
bool Read(File& file, Snapshot& result, bool require_crc = false) {
  Snapshot candidate;
  uint32_t crc = 0xffffffffu;
  bool modern = false;
  if (!ReadChunk(file, 'Z', candidate.values, crc, modern) ||
      !ReadChunk(file, 'X', candidate.data, crc, modern)) return false;
  const auto remaining = file.size() - file.position();
  if (remaining == 0) {
    if (require_crc || modern) return false; // legacy: structural + XOR only
  } else {
    uint8_t footer[8];
    if (remaining != sizeof(footer) ||
        file.read(footer, sizeof(footer)) != sizeof(footer) ||
        footer[0] != 'P' || footer[1] != 'Z' ||
        footer[2] != 'C' || footer[3] != 1 ||
        Get(footer + 4, 4) != (crc ^ 0xffffffffu)) return false;
  }
  result = std::move(candidate); // failed reads never publish partial state
  return true;
}

template<class File>
bool WriteChunk(File& file, char kind, const Map& map, uint32_t& crc) {
  if (map.size() > 65535) return false;
  uint64_t checksum = 0;
  for (const auto& row : map) checksum ^= row.second;
  uint8_t header[12] = {'P', static_cast<uint8_t>(kind + ('a' - 'A'))};
  Put(header + 2, 2, map.size());
  Put(header + 4, 8, checksum);
  if (file.write(header, sizeof(header)) != sizeof(header)) return false;
  crc = Crc(crc, header, sizeof(header));
  for (const auto& row : map) {
    uint8_t record[10];
    Put(record, 2, row.first);
    Put(record + 2, 8, row.second);
    if (file.write(record, sizeof(record)) != sizeof(record)) return false;
    crc = Crc(crc, record, sizeof(record));
  }
  return true;
}
template<class File>
bool Write(File& file, const Snapshot& snapshot) {
  uint32_t crc = 0xffffffffu;
  if (!WriteChunk(file, 'Z', snapshot.values, crc) ||
      !WriteChunk(file, 'X', snapshot.data, crc)) return false;
  // Lowercase signatures require the footer, including after truncation.
  // Original record layout remains readable by migration tooling. Older firmware
  // does not understand this integrity footer: downgrade requires backup.
  uint8_t footer[8] = {'P', 'Z', 'C', 1};
  Put(footer + 4, 4, crc ^ 0xffffffffu);
  return file.write(footer, sizeof(footer)) == sizeof(footer);
}

template<class Fs>
bool ReadPath(Fs& fs, const char* path, Snapshot& result,
              bool require_crc = false) {
  auto file = fs.open(path, 0); // Teensy FILE_READ
  if (!file) return false;
  const bool ok = Read(file, result, require_crc);
  file.close();
  return ok;
}
template<class Fs>
LoadResult Load(Fs& fs, const char* path, Snapshot& result) {
  if (ReadPath(fs, path, result)) return LoadResult::Primary;
  const auto backup = std::string(path) + ".bak";
  if (ReadPath(fs, backup.c_str(), result)) return LoadResult::Backup;
  // A .tmp may be an uncommitted attempt: never activate it on startup.
  return LoadResult::Invalid;
}

// A verified previous version is kept as .bak. Interrupted promotion recovers
// the old version, not a partial or merely staged new version. This protocol
// does not assert hardware power-loss durability beyond the filesystem.
template<class Fs>
bool Save(Fs& fs, const char* path, const Snapshot& snapshot) {
  const auto stage = std::string(path) + ".tmp";
  const auto backup = std::string(path) + ".bak";
  Snapshot previous;
  const bool primary_exists = fs.exists(path);
  const bool primary_valid = ReadPath(fs, path, previous);
  if (primary_exists && !primary_valid &&
      !ReadPath(fs, backup.c_str(), previous)) return false;

  if (fs.exists(stage.c_str()) && !fs.remove(stage.c_str())) return false;
  auto file = fs.open(stage.c_str(), 2); // Teensy FILE_WRITE_BEGIN
  if (!file) return false;
  const bool written = Write(file, snapshot);
  file.flush();
  file.close();
  // Teensy flush()/close() return void. Reopen and compare the complete
  // independently parsed context. A backend which lies about durable sync
  // still requires hardware power-cut validation.
  Snapshot verified;
  if (!written || !ReadPath(fs, stage.c_str(), verified, true) ||
      !(verified == snapshot)) return false;
  if (primary_valid) {
    if (fs.exists(backup.c_str()) && !fs.remove(backup.c_str())) return false;
    if (!fs.rename(path, backup.c_str())) return false;
  } else if (primary_exists && !fs.remove(path)) {
    return false; // valid backup remains untouched
  }
  if (!fs.rename(stage.c_str(), path)) return false;
  Snapshot committed;
  return ReadPath(fs, path, committed, true) && committed == snapshot;
}
}} // namespace phz::config
