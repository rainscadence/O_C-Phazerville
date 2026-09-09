#pragma once

#ifdef __IMXRT1062__
#include <LittleFS.h>
#include <SD.h>
#include "config/Store.h"

extern bool SDcard_Ready;

namespace PhzConfig {
  using KEY = uint16_t;
  using VALUE = uint64_t;
  using ConfigMap = phz::config::Map;

  const char * const CONFIG_FILENAME = "GLOBALS.CFG";

  extern LittleFS_Program myfs;

  // Forward Decl
  void Init();
  void listFiles(FS &fs = myfs);
  bool load_config(const char* filename = CONFIG_FILENAME, FS &fs = myfs);
  bool save_config(const char* filename = CONFIG_FILENAME, FS &fs = myfs);
  void clear_config();
  phz::config::LoadResult last_load_result();
  // Global saves must not replace the current app/bank working context.
  class ScopedContext {
   public:
    ScopedContext();
    ~ScopedContext();
    ScopedContext(const ScopedContext&) = delete;
    ScopedContext& operator=(const ScopedContext&) = delete;
   private:
    phz::config::ContextScope scope_;
    phz::config::LoadResult load_;
  };

  void setValue(KEY key, VALUE value);
  bool getValue(KEY key, VALUE &value);
  void deleteKey(KEY key);

  void setData(KEY key, VALUE value);
  bool getData(KEY key, VALUE &value);
  void deleteData(KEY key);

  void printDirectory(FS &fs = myfs);
  void printDirectory(File dir, int numSpaces);
  void printSpaces(int num);
  void eraseFiles(FS &fs = myfs);

}
#endif
