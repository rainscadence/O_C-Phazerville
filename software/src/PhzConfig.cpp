/* Phazerville Config File
 *
 * Primarily stored on LittleFS in flash storage,
 * or SD card if available, or other any similar FS object.
 * Supercedes previous EEPROM mechanism
 */
#ifdef __IMXRT1062__
#include "PhzConfig.h"
#include "HSUtils.h"
#include "util/util_misc.h"
#include "usb_desc.h"

#ifdef MTP_INTERFACE
#include <MTP_Teensy.h>
#endif

namespace PhzConfig {

LittleFS_Program myfs;
phz::config::Snapshot store;
ConfigMap& cfg_store = store.values;
ConfigMap& data_store = store.data;
static phz::config::LoadResult load_result = phz::config::LoadResult::Invalid;
phz::config::LoadResult last_load_result() { return load_result; }
ScopedContext::ScopedContext() : scope_(store), load_(load_result) {}
ScopedContext::~ScopedContext() {
  load_result = load_;
}

// Specify size to use of onboard Teensy Program Flash chip.
// the maximum flash available for LittleFS is 960 blocks of 1024 bytes
static constexpr uint32_t diskSize = 1024 * 512;

FLASHMEM
void Init()
{
#ifdef MTP_INTERFACE
  MTP.begin();
#endif
  if (SDcard_Ready) {
#ifdef MTP_INTERFACE
    MTP.addFilesystem(SD, "SD_Card");
#endif
    SERIAL_PRINTLN("SD card available for preset storage");
    //listFiles(SD);
  }

  // This mounts or creates a LittleFS drive in Teensy PCB Flash.
  if (!myfs.begin(diskSize)) {
    SERIAL_PRINTLN("LittleFS unavailable!! Settings WILL NOT BE SAVED!");
    return;
  }
  SERIAL_PRINTLN("LittleFS initialized.");

#ifdef MTP_INTERFACE
  MTP.addFilesystem(myfs, "Internal_LFS");
#endif

  /*
  if (myfs.mediaPresent()) {
    listFiles(myfs);
    //load_config();
  }
  */
}

void clear_config() {
  cfg_store.clear();
  data_store.clear();
}

void setValue(KEY key, VALUE value)
{
  cfg_store[key] = value;
}

bool getValue(KEY key, VALUE &value)
{
  auto thing = cfg_store.find(key);
  if (thing != cfg_store.end()) {
    value = thing->second;
    return true;
  }
  return false;
}

void deleteKey(KEY key) {
  cfg_store.erase(key);
}

void setData(KEY key, VALUE value) {
  data_store[key] = value;
}
bool getData(KEY key, VALUE &value) {
  auto thing = data_store.find(key);
  if (thing != data_store.end()) {
    value = thing->second;
    return true;
  }
  return false;
}
void deleteData(KEY key) {
  data_store.erase(key);
}

bool save_config(const char* filename, FS &fs) {
  const bool saved = phz::config::Save(fs, filename, store);
  if (!saved) HS::PokePopup(HS::MESSAGE_POPUP, "Save FAILED");
  return saved;
}

bool load_config(const char* filename, FS &fs) {
  phz::config::Snapshot candidate;
  load_result = phz::config::Load(fs, filename, candidate);
  // Never expose partly parsed maps or leave a different bank in place.
  store = std::move(candidate);
  if (load_result == phz::config::LoadResult::Backup)
    HS::PokePopup(HS::MESSAGE_POPUP, "Recovered backup");
  return load_result != phz::config::LoadResult::Invalid;
}

FLASHMEM
void listFiles(FS &fs)
{
  Serial.print("\n     Space Used = ");
  Serial.println(fs.usedSize());
  Serial.print("Filesystem Size = ");
  Serial.println(fs.totalSize());

  printDirectory(fs);
}

FLASHMEM
void eraseFiles(FS &fs)
{
  //myfs.quickFormat();
  fs.format();
  Serial.println("\nFilesystem formatted - All files erased !");
}

FLASHMEM
void printDirectory(FS &fs) {
  Serial.println("Directory\n---------");
  printDirectory(fs.open("/"), 0);
  Serial.println();
}

FLASHMEM
void printDirectory(File dir, int numSpaces) {
   while(true) {
     File entry = dir.openNextFile();
     if (! entry) {
       //Serial.println("** no more files **");
       break;
     }
     printSpaces(numSpaces);
     Serial.print(entry.name());
     if (entry.isDirectory()) {
       Serial.println("/");
       printDirectory(entry, numSpaces+2);
     } else {
       // files have sizes, directories do not
       printSpaces(36 - numSpaces - strlen(entry.name()));
       Serial.print("  ");
       Serial.println(entry.size(), DEC);
     }
     entry.close();
   }
}

FLASHMEM
void printSpaces(int num) {
  for (int i=0; i < num; i++) {
    Serial.print(" ");
  }
}

} // namespace PhzConfig
#endif
