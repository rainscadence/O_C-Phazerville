// Copyright (c) 2016-2019 Patrick Dowling
//
// Author: Patrick Dowling (pld@gurkenkiste.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "OC_core.h"
#include "OC_gpio.h"
#include "OC_scales.h"
#include "OC_ui.h"
#include "OC_apps.h"
#include "ii/Lifecycle.h"
#include "ii/Follower.h"
#include "txi2c2midi/Configuration.h"
#include "OC_menus.h"
#include "OC_config.h"
#include "OC_digital_inputs.h"
#include "OC_storage.h"
#include "OC_app_switcher.h"
#include "OC_global_settings.h"
#include "util/util_misc.h"
#include "util/util_settings.h"
#include "util/util_integer_sequences.h"
#include "util/util_trigger_delay.h"
#include "util/util_logistic_map.h"
#include "util/util_arp.h"
#include "util/util_grid.h"
#include "util/util_ringbuffer.h"
#include "util/util_settings.h"
#include "util/util_semitone_quantizer.h"
#include "util/util_sync.h"

#include "OC_input_maps.h"
#include "OC_pitch_utils.h"
#include "OC_euclidean_mask_draw.h"
#include "OC_trigger_delays.h"

#include "OC_calibration.h"
#include "OC_patterns.h"
#include "src/drivers/FreqMeasure/OC_FreqMeasure.h"
#include "util/util_pagestorage.h"
#include "src/drivers/EEPROMStorage.h"
#include "PhzConfig.h"
#if defined(ARDUINO_TEENSY41)
#include "ii/Configuration.h"
#endif
#include "VBiasManager.h"
#include "HSClockManager.h"
#ifdef ARDUINO_TEENSY41
#include "AudioIO.h"
#endif

#ifndef NO_HEMISPHERE
// applets
#include "applets/_config.h"
#ifdef ARDUINO_TEENSY41
#include "audio_applets/_config.h"
#endif
#endif

#include "HSApplication.h"
#include "OC_scale_edit.h"
#include "OC_visualfx.h"

// actual apps are included and instantiated here
#include "apps/_config.h"

extern uint_fast8_t MENU_REDRAW;
extern "C" void _reboot_Teensyduino_();

namespace OC {

// NOTE These are slightly wasteful, in that the PageStorage implementation and
// the local data both retain a copy of the data. Removing this would in theory
// reclaim some memory, although RAM isn't currently an issue.
/*extern*/ DMAMEM GlobalSettings global_settings;
/*extern*/ AppSwitcher app_switcher;
static DMAMEM AppData app_data;
#ifndef __IMXRT1062__
static GlobalSettingsStorage global_settings_storage;
#endif
static DMAMEM AppDataStorage app_data_storage;

namespace phz_settings_ui {
static const char app_menu[] PROGMEM = "Phazerville...";
static const char title[] PROGMEM = "Settings";
static const char update_hint[] PROGMEM = "Hold right to update";
static const char update_row[] PROGMEM = "Firmware Update";
static const char footer_save[] PROGMEM = "[BACK]        [SAVE]";
static const char footer_update[] PROGMEM = "[BACK]      [HOLD R]";
static const char update_mode[] PROGMEM = "Firmware Update Mode";
static const char update_loader[] PROGMEM = "(use Teensy Loader)";
#ifdef AUDIO_INTERFACE
static const char usb_audio_row[] PROGMEM = "USB Audio: %s";
#endif
}

#if defined(ARDUINO_TEENSY41) && defined(AUDIO_INTERFACE)
static constexpr uint16_t kUsbAudioMirrorSettingsKey = 0x0902;
#endif

FLASHMEM void EnterFirmwareUpdateMode() {
  // Prevent the loader from holding a previously high or non-zero output.
  CORE::app_isr_enabled = false;
  delay(1);
  DAC::set_all_octave(0);
  DAC::Update();

  const uint32_t start = millis();
  while (millis() < start + SETTINGS_SAVE_TIMEOUT_MS) {
    GRAPHICS_BEGIN_FRAME(true);
    gfxPos(5, 10);
    graphics.print(phz_settings_ui::update_mode);
    gfxPos(5, 19);
    graphics.print(phz_settings_ui::update_loader);
    GRAPHICS_END_FRAME();
  }

  _reboot_Teensyduino_();
}

#ifdef __IMXRT1062__
enum GlobalSettingsDataKeys : uint16_t {
  // upper 8 bits of key, non-zero
  METADATA_KEY        = 1 << 8, // selected app id, etc.
  USER_SCALES_KEY     = 2 << 8,
  SEQUENCES_KEY       = 3 << 8,
  CHORDS_KEY          = 4 << 8,
  TURING_MACHINES_KEY = 5 << 8,
  WAVEFORMS_KEY       = 6 << 8,
  AUTOCAL_KEY         = 7 << 8,

  // lower 8 bits of key
  SCALE_METADATA = 0xff,
  SCALE_NOTEDATA = 0,
};
#endif

FLASHMEM
static bool SaveGlobalSettings(bool factory = false) {
  APPS_SERIAL_PRINTLN("Save global settings");

#ifdef __IMXRT1062__
  PhzConfig::ScopedContext globals;
  if (!factory && !PhzConfig::load_config()) {
    HS::PokePopup(HS::MESSAGE_POPUP, "Globals invalid");
    return false;
  }
#if defined(ARDUINO_TEENSY41)
  if (factory) {
    uint64_t record = 0;
    if (!global_settings.ii_address.PendingRecord(record)) return false;
    PhzConfig::setValue(phz::ii::kSettingsKey, record);
    PhzConfig::setValue(txi2c2midi::kModeSettingsKey, global_settings.tx_mode.PendingRecord());
  }
#endif

  // Metadata
  uint64_t data = 0;
  // TODO:
  //global_settings.DAC_scaling = OC::DAC::store_scaling();
  Pack(data, PackLocation{0, 16}, global_settings.current_app_id);
  Pack(data, PackLocation{16, 1}, global_settings.encoders_enable_acceleration);
  Pack(data, PackLocation{17, 1}, 1); // v2.0 first-run validation
  PhzConfig::setValue(METADATA_KEY, data);

  // User Scales
  char filename[] = "000.SCL";
  for (size_t i = 0; i < Scales::SCALE_USER_COUNT; ++i) {
    PhzConfig::setValue(USER_SCALES_KEY | (i << 4) | SCALE_METADATA, uint64_t(user_scales[i].span) << 16 | user_scales[i].num_notes);
    data = 0;
    for (size_t nn = 0; nn < user_scales[i].num_notes; ++nn) {
      Pack(data, PackLocation{(nn & 0x3)*16, 16}, (uint16_t)user_scales[i].notes[nn]);

      // after every 4th value (64 bits), store and reset
      if ((nn & 0x3) == 0x3) {
        PhzConfig::setValue(USER_SCALES_KEY | (i << 4) | (SCALE_NOTEDATA + (nn >> 2)), data);
        data = 0;
      }
    }

    if (SDcard_Ready) {
      filename[2] = char('0' + i);
      SD.remove(filename);
      File file = SD.open(filename, FILE_WRITE_BEGIN);
      if (file) {
        Scales::SaveToScala(user_scales[i], file);
      }
      file.close();
    }
  }

  // User Patterns aka Sequences
  for (size_t i = 0; i < Patterns::PATTERN_USER_COUNT; ++i) {
    data = 0;
    for (size_t step = 0; step < ARRAY_SIZE(Pattern::notes); ++step) {
      Pack(data, PackLocation{(step & 0x3)*16, 16}, (uint16_t)user_patterns[i].notes[step]);
      if ((step & 0x3) == 0x3) {
        PhzConfig::setValue(SEQUENCES_KEY | (i << 3) | (step >> 2), data);
        data = 0;
      }
    }
  }

  // User Chords (progression sequences from Acid Curds)
  for (size_t i = 0; i < Chords::CHORDS_USER_COUNT; ++i) {
    data = 0;
    Pack(data, PackLocation{0, 8}, (uint8_t)user_chords[i].quality);
    Pack(data, PackLocation{8, 8}, (uint8_t)user_chords[i].inversion);
    Pack(data, PackLocation{16,8}, (uint8_t)user_chords[i].voicing);
    Pack(data, PackLocation{24,8}, (uint8_t)user_chords[i].base_note);
    Pack(data, PackLocation{32,8}, (uint8_t)user_chords[i].octave);
    PhzConfig::setValue(CHORDS_KEY | i, data);
  }

  // User Turing Machines (for Enigma and friends)
  for (size_t i = 0; i < HS::TURING_MACHINE_COUNT; ++i) {
    data = 0;
    Pack(data, PackLocation{0, 16}, HS::user_turing_machines[i].reg);
    Pack(data, PackLocation{16, 8}, HS::user_turing_machines[i].len);
    Pack(data, PackLocation{24, 1}, HS::user_turing_machines[i].favorite);
    PhzConfig::setValue(TURING_MACHINES_KEY | i, data);
  }

#ifndef NO_HEMISPHERE
  data = 0;
  // User Waveform (custom VectorOsc shapes)
  for (size_t i = 0; i < HS::VO_SEGMENT_COUNT; ++i) {
    Pack(data, PackLocation{(i & 0x3) * 16, 16}, uint16_t(HS::user_waveforms[i].level) << 8 | HS::user_waveforms[i].time);

    if ((i & 0x3) == 0x3) {
      PhzConfig::setValue(WAVEFORMS_KEY | (i >> 2), data);
      data = 0;
    }
  }
#endif

  // Auto Calibration Data
  /*
  for (size_t i = 0; i < DAC_CHANNEL_COUNT; ++i) {
    data = 0;
    PhzConfig::setValue(AUTOCAL_KEY | (0xff - i), auto_calibration_data[i].use_auto_calibration_);

    for (size_t oct = 0; oct < OCTAVES + 1; ++oct) {
      Pack(data, PackLocation{(oct & 0x3) * 16, 16}, auto_calibration_data[i].auto_calibrated_octaves[oct]);
      if ((oct & 0x3) == 0x3) {
        PhzConfig::setValue(AUTOCAL_KEY | (i << 4) | (oct >> 2), data);
        data = 0;
      }
    }
  }
  */

  return PhzConfig::save_config(); // verified replacement, no live rebind
#else // --- Teensy 3.2
  memcpy(global_settings.user_scales, OC::user_scales, sizeof(OC::user_scales));
  memcpy(global_settings.user_patterns, OC::user_patterns, sizeof(OC::user_patterns));
#ifdef ENABLE_APP_CHORDS
  memcpy(global_settings.user_chords, OC::user_chords, sizeof(OC::user_chords));
#else
  memcpy(global_settings.user_turing_machines, HS::user_turing_machines, sizeof(HS::user_turing_machines));
#endif
#ifndef NO_HEMISPHERE
  memcpy(global_settings.user_waveforms, HS::user_waveforms, sizeof(HS::user_waveforms));
#endif

  for (int i = 0; i < QUANT_CHANNEL_COUNT; ++i) {
    global_settings.q_engines[i].scale = HS::q_engine[i].scale;
    global_settings.q_engines[i].mask = HS::q_engine[i].mask;
    global_settings.q_engines[i].octave = HS::q_engine[i].octave;
    global_settings.q_engines[i].root_note = HS::q_engine[i].root_note;
  }
  for (int i = 0; i < MIDIMAP_MAX; ++i) {
    global_settings.midi_maps[i].channel       = HS::frame.MIDIState.mapping[i].channel      ;
    global_settings.midi_maps[i].dac_polyvoice = HS::frame.MIDIState.mapping[i].dac_polyvoice;
    global_settings.midi_maps[i].function      = HS::frame.MIDIState.mapping[i].function     ;
    global_settings.midi_maps[i].function_cc   = HS::frame.MIDIState.mapping[i].function_cc  ;
    global_settings.midi_maps[i].transpose     = HS::frame.MIDIState.mapping[i].transpose    ;
    global_settings.midi_maps[i].range_low     = HS::frame.MIDIState.mapping[i].range_low    ;
    global_settings.midi_maps[i].range_high    = HS::frame.MIDIState.mapping[i].range_high   ;
  }

  global_settings_storage.Save(global_settings);
  APPS_SERIAL_PRINTLN("Saved global settings: page_index %d", global_settings_storage.page_index());
  return true;
#endif
}

/* old eeprom space checking logic
static constexpr size_t total_storage_size() {
    size_t used = 0;
    for (size_t i = 0; i < app_container.num_apps(); ++i) {
        used += app_container[i]->storage_size() + sizeof(AppChunkHeader);
        if (used & 1) ++used; // align on 2-byte boundaries
    }
    return used;
}
static constexpr size_t totalsize = total_storage_size();
static_assert(totalsize < OC::AppData::kAppDataSize, "EEPROM Allocation Exceeded");
*/

FLASHMEM void SaveAppData() {
  if (!SaveGlobalSettings()) return; // never continue after failed globals save
  APPS_SERIAL_PRINTLN("Save app data... (%u bytes available)", OC::AppData::kAppDataSize);

  app_data.used = 0;
  uint8_t *data = app_data.data;
  uint8_t *data_end = data + OC::AppData::kAppDataSize;

  size_t start_app = random(app_container.num_apps());
  for (size_t i = 0; i < app_container.num_apps(); ++i) {
    const AppBase* app = (AppBase*)app_container[(start_app + i) % app_container.num_apps()].instance;
    if (!app) continue;
    size_t storage_size = app->storage_size() + sizeof(AppChunkHeader);
    if (storage_size & 1) ++storage_size; // Align chunks on 2-byte boundaries
    if (storage_size > sizeof(AppChunkHeader)) {
      if (data + storage_size > data_end) {
        APPS_SERIAL_PRINTLN("%s: ERROR: %u BYTES NEEDED, %u BYTES AVAILABLE OF %u BYTES TOTAL", app->name(), storage_size, data_end - data, AppData::kAppDataSize);
        continue;
      }

      AppChunkHeader *chunk = reinterpret_cast<AppChunkHeader *>(data);
      chunk->id = app->id();
      chunk->length = storage_size;

      util::StreamBufferWriter stream_buffer{chunk + 1, chunk->length};
      auto result = app->Save(stream_buffer);
      if (stream_buffer.overflow()) {
        APPS_SERIAL_PRINTLN("* %s (%02x) : Save overflowed, result=%u, skipping app...", 
                            app->name(), app->id(), result);
      } else {
        APPS_SERIAL_PRINTLN("* %s (%02x) : Saved %u bytes... (%u)",
                            app->name(), app->id(), result, storage_size);
        app_data.used += chunk->length;
        data += chunk->length;
      }
      (void)result;
    }
  }
  APPS_SERIAL_PRINTLN("App settings used: %u/%u", app_data.used, EEPROM_APPDATA_BINARY_SIZE);
  app_data_storage.Save(app_data);
  APPS_SERIAL_PRINTLN("Saved app settings in page_index %d", app_data_storage.page_index());
}

FLASHMEM
static void RestoreAppData() {
  APPS_SERIAL_PRINTLN("Restoring app data from page_index %d, used=%u", app_data_storage.page_index(), app_data.used);

  const uint8_t *data = app_data.data;
  const uint8_t *data_end = data + app_data.used;
  size_t restored_bytes = 0;

  while (data < data_end) {
    const AppChunkHeader *chunk = reinterpret_cast<const AppChunkHeader *>(data);
    if (data + chunk->length > data_end) {
      APPS_SERIAL_PRINTLN("App chunk length %u exceeds available space (%u)", chunk->length, data_end - data);
      break;
    }
    if (!chunk->id || !chunk->length) {
      APPS_SERIAL_PRINTLN("Invalid app chunk id=%02x, length=%d, stopping restore", chunk->id, chunk->length);
      break;
    }

    auto app = app_container.FindAppByID(chunk->id);
    if (!app) {
      APPS_SERIAL_PRINTLN("App %02x not found, ignoring chunk... skipping %u", chunk->id, chunk->length);
      if (!chunk->length)
        break;
      data += chunk->length;
      continue;
    }
    size_t expected_length = app->storage_size() + sizeof(AppChunkHeader);
    if (expected_length & 0x1) ++expected_length;
    if (chunk->length != expected_length) {
      APPS_SERIAL_PRINTLN("* %s (%02x): chunk length %u != %u (storage_size=%u), skipping...", app->name(), chunk->id, chunk->length, expected_length, app->storage_size());
      data += chunk->length;
      continue;
    }

    util::StreamBufferReader stream_buffer{chunk + 1, chunk->length};
    auto result = app->Restore(stream_buffer);
    if (stream_buffer.underflow()) {
      APPS_SERIAL_PRINTLN("* %s (%02x): Restore underflow, result=%u, re-init",
                          app->name(), chunk->id, result);
      app->InitDefaults();
    } else {
      APPS_SERIAL_PRINTLN("* %s (%02x): Restored %u from %u (chunk length %u)...",
                          app->name(), chunk->id, result, chunk->length - sizeof(AppChunkHeader), chunk->length);
      restored_bytes += chunk->length;
    }
    (void)result;

    data += chunk->length;
  }

  APPS_SERIAL_PRINTLN("App data restored: %u, expected %u", restored_bytes, app_data.used);
}

FLASHMEM
void AppSwitcher::set_current_app(size_t index)
{
  phz::ii::SetHostActive(false);
  phz::ii::Transition transition;
  current_app_ = app_container[index];
  global_settings.current_app_id = current_app_.id();
  #ifdef VOR
  VBiasManager *vbias_m = vbias_m->get();
  vbias_m->SetStateForApp(current_app_);
  #endif
  transition.Complete();
}

FLASHMEM
void AppSwitcher::Init(bool reset_settings, bool reset_confirmed) {
  phz::ii::SetHostActive(false);
  phz::ii::Transition transition(true);

  APPS_SERIAL_PRINTLN("Init");
  app_container.for_each([](RuntimeSlot app) {
    APPS_SERIAL_PRINTLN("> %s", app->name());
    app.InitDefaults(app.instance);
  });

  current_app_ = app_container[DEFAULT_APP_INDEX];

  SERIAL_PRINTLN("[App Initializations]");

  Scales::Init();
  HS::Init();
#ifndef NO_HEMISPHERE
  HS::showhide_cursor.Init(0, HEMISPHERE_AVAILABLE_APPLETS - 1);
#endif
  HS::frame.Init();

  global_settings.Init();
  global_settings.encoders_enable_acceleration = OC_ENCODERS_ENABLE_ACCELERATION_DEFAULT;
  global_settings.usb_audio_mirror = true;
  global_settings.reserved1 = false;
  global_settings.reserved2 = 0U;
  global_settings.current_app_id = DEFAULT_APP_ID;
  memset(HS::user_turing_machines, 0, sizeof(HS::user_turing_machines));

  PhzConfig::ScopedContext globals;
  const bool globals_valid = PhzConfig::load_config();
  global_settings.valid = false;
  uint64_t data = 0;
  // check metadata for validity
  if (PhzConfig::getValue(METADATA_KEY, data)) {
    global_settings.current_app_id = Unpack(data, PackLocation{0, 16});
    global_settings.encoders_enable_acceleration = Unpack(data, PackLocation{16, 1});
    global_settings.valid = Unpack(data, PackLocation{17, 1});
    // 15 bits empty...
    // TODO:
    //global_settings.DAC_scaling = Unpack(data, PackLocation{32, 32});
    //OC::DAC::restore_scaling(global_settings.DAC_scaling);
  }

#if defined(ARDUINO_TEENSY41)
  uint64_t ii_record = 0;
  const bool ii_present = PhzConfig::getValue(phz::ii::kSettingsKey, ii_record);
  global_settings.ii_address.Boot(globals_valid && global_settings.valid,
                                  ii_present, ii_record);
  uint64_t tx_record = 0;
  const bool tx_present = PhzConfig::getValue(txi2c2midi::kModeSettingsKey, tx_record);
  global_settings.tx_mode.Boot(globals_valid && global_settings.valid, tx_present, tx_record);
  uint64_t tx_address_record = 0;
  const bool tx_address_present = PhzConfig::getValue(txi2c2midi::kDeviceAddressKey, tx_address_record);
  global_settings.tx_address_ignored = txi2c2midi::LegacyAddressIgnored(
      global_settings.ii_address, tx_address_present, tx_address_record);
#ifdef AUDIO_INTERFACE
  uint64_t usb_audio_record = 0;
  const bool usb_audio_present = PhzConfig::getValue(
      kUsbAudioMirrorSettingsKey, usb_audio_record);
  global_settings.usb_audio_mirror = !usb_audio_present || usb_audio_record == 0x101;
  if (usb_audio_record != 0x100 && usb_audio_record != 0x101)
    global_settings.usb_audio_mirror = true;
  AudioIO::SetUsbAudioMirror(global_settings.usb_audio_mirror);
#endif
#endif

  if (reset_settings || !global_settings.valid) {
    if (reset_confirmed || ui.ConfirmReset()) {
      reset_settings = true; // do not restore the pre-reset maps afterward
      APPS_SERIAL_PRINTLN("Erase EEPROM ...");
      EEPtr d = EEPROM_GLOBALSETTINGS_START;
      size_t len = EEPROMStorage::LENGTH - EEPROM_GLOBALSETTINGS_START;
      while (len--)
        *d++ = 0;
      APPS_SERIAL_PRINTLN("...done");
      APPS_SERIAL_PRINTLN("Skip settings, using defaults...");
#ifdef __IMXRT1062__
      PhzConfig::eraseFiles();
#else
      global_settings_storage.Init();
#endif
      app_data_storage.Init();
      // This branch is reached only after explicit factory-reset approval.
      PhzConfig::clear_config();
      global_settings.valid = true;
#if defined(ARDUINO_TEENSY41)
      global_settings.ii_address.FactoryDefaults();
      global_settings.tx_mode.Boot(true, false, 0);
      global_settings.tx_address_ignored = false;
#endif
      if (!SaveGlobalSettings(true)) {
        global_settings.valid = false;
#if defined(ARDUINO_TEENSY41)
        global_settings.ii_address.Boot(false, false, 0);
        global_settings.tx_mode.Boot(false, false, 0);
#endif
      }
    } else {
      reset_settings = false;
    }
  }

  if (!reset_settings) {
#ifdef __IMXRT1062__
    // User Scales
    char filename[] = "000.SCL";
    bool scala_file_loaded[Scales::SCALE_USER_COUNT] = {false};
    for (size_t i = 0; i < Scales::SCALE_USER_COUNT; ++i) {
      if (SDcard_Ready && SD.exists(filename)) {
        filename[2] = char('0' + i);
        File file = SD.open(filename);
        if (file) {
          Scales::LoadScala(user_scales[i], file);
          scala_file_loaded[i] = true;
        }
        file.close();
      }
    }

    // Metadata
    if (global_settings.valid) {
      // User Scales
      for (size_t i = 0; i < Scales::SCALE_USER_COUNT; ++i) {
        if (scala_file_loaded[i] ||
            !PhzConfig::getValue(USER_SCALES_KEY | (i << 4) | SCALE_METADATA, data))
            continue;

        user_scales[i].span = (data >> 16) & 0xffff;
        user_scales[i].num_notes = data & 0x00ff;

        for (size_t nn = 0; nn < user_scales[i].num_notes; ++nn) {
          // the first of every 4 values needs a new config chunk
          if ((nn & 0x3) == 0x0) {
            data = 0;
            if (!PhzConfig::getValue(USER_SCALES_KEY | (i << 4) | (SCALE_NOTEDATA + (nn >> 2)), data))
              break;
          }
          user_scales[i].notes[nn] = Unpack(data, PackLocation{(nn & 0x3)*16, 16});
        }
      }

      // User Patterns aka Sequences
      for (size_t i = 0; i < Patterns::PATTERN_USER_COUNT; ++i) {
        for (size_t step = 0; step < ARRAY_SIZE(Pattern::notes); ++step) {
          if ((step & 0x3) == 0x0) {
            data = 0;
            if (!PhzConfig::getValue(SEQUENCES_KEY | (i << 3) | (step >> 2), data))
              break;
          }
          user_patterns[i].notes[step] = Unpack(data, PackLocation{(step & 0x3)*16, 16});
        }
      }

      // User Chords (progression sequences from Acid Curds)
      for (size_t i = 0; i < Chords::CHORDS_USER_COUNT; ++i) {
        data = 0;
        if (!PhzConfig::getValue(CHORDS_KEY | i, data))
          break;
        user_chords[i].quality = Unpack(data, PackLocation{0, 8});
        user_chords[i].inversion = Unpack(data, PackLocation{8, 8});
        user_chords[i].voicing = Unpack(data, PackLocation{16,8});
        user_chords[i].base_note = Unpack(data, PackLocation{24,8});
        user_chords[i].octave = Unpack(data, PackLocation{32,8});
      }

      // -- User Turing Machines (for Enigma and friends)
      for (size_t i = 0; i < HS::TURING_MACHINE_COUNT; ++i) {
        data = 0;
        if (!PhzConfig::getValue(TURING_MACHINES_KEY | i, data))
          break;
        HS::user_turing_machines[i].reg = Unpack(data, PackLocation{0, 16});
        HS::user_turing_machines[i].len = Unpack(data, PackLocation{16, 8});
        HS::user_turing_machines[i].favorite = Unpack(data, PackLocation{24, 1});
      }

#ifndef NO_HEMISPHERE
      // -- User Waveform (custom VectorOsc shapes)
      for (size_t i = 0; i < HS::VO_SEGMENT_COUNT; ++i) {
        if ((i & 0x3) == 0x0) {
          data = 0;
          if (!PhzConfig::getValue(WAVEFORMS_KEY | (i >> 2), data))
            break;
        }
        uint16_t wavedata = Unpack(data, PackLocation{(i & 0x3) * 16, 16});
        HS::user_waveforms[i].level = (wavedata >> 8) & 0xff;
        HS::user_waveforms[i].time = wavedata & 0xff;
      }
#endif

      // -- Auto Calibration Data
      /*
      for (size_t i = 0; i < DAC_CHANNEL_COUNT; ++i) {
        data = 0;
        if (!PhzConfig::getValue(AUTOCAL_KEY | (0xff - i), data))
          break;
        auto_calibration_data[i].use_auto_calibration_ = data;
        for (size_t oct = 0; oct < OCTAVES + 1; ++oct) {
          if ((oct & 0x3) == 0x0) {
            data = 0;
            if (!PhzConfig::getValue(AUTOCAL_KEY | (i << 4) | (oct >> 2), data))
              break;
          }
          auto_calibration_data[i].auto_calibrated_octaves[oct] = Unpack(data, PackLocation{(oct & 0x3) * 16, 16});
        }
      }
      */
    }

#else // Teensy 3.2
    APPS_SERIAL_PRINTLN("Load global settings: size: %u, PAGESIZE=%u, PAGES=%u, LENGTH=%u",
                  sizeof(GlobalSettings),
                  GlobalSettingsStorage::PAGESIZE,
                  GlobalSettingsStorage::PAGES,
                  GlobalSettingsStorage::LENGTH);

    if (!global_settings_storage.Load(global_settings)) {
      APPS_SERIAL_PRINTLN("Settings invalid, using defaults!");
    } else {
      APPS_SERIAL_PRINTLN("Loaded settings from page_index %d, current_app_id is %02x",
                    global_settings_storage.page_index(),global_settings.current_app_id);
      memcpy(user_scales, global_settings.user_scales, sizeof(user_scales));
      memcpy(user_patterns, global_settings.user_patterns, sizeof(user_patterns));
#ifdef ENABLE_APP_CHORDS
      memcpy(user_chords, global_settings.user_chords, sizeof(user_chords));
#else
      memcpy(HS::user_turing_machines, global_settings.user_turing_machines, sizeof(HS::user_turing_machines));
#endif
#ifndef NO_HEMISPHERE
      memcpy(HS::user_waveforms, global_settings.user_waveforms, sizeof(HS::user_waveforms));
#endif

      // restore q_engines and midi_maps
      for (int i = 0; i < QUANT_CHANNEL_COUNT; ++i) {
        HS::q_engine[i].scale     = global_settings.q_engines[i].scale;
        HS::q_engine[i].mask      = global_settings.q_engines[i].mask;
        HS::q_engine[i].octave    = global_settings.q_engines[i].octave;
        HS::q_engine[i].root_note = global_settings.q_engines[i].root_note;
        HS::q_engine[i].Reconfig();
      }
      for (int i = 0; i < MIDIMAP_MAX; ++i) {
        HS::frame.MIDIState.mapping[i].channel       = global_settings.midi_maps[i].channel      ;
        HS::frame.MIDIState.mapping[i].dac_polyvoice = global_settings.midi_maps[i].dac_polyvoice;
        HS::frame.MIDIState.mapping[i].function      = global_settings.midi_maps[i].function     ;
        HS::frame.MIDIState.mapping[i].function_cc   = global_settings.midi_maps[i].function_cc  ;
        HS::frame.MIDIState.mapping[i].transpose     = global_settings.midi_maps[i].transpose    ;
        HS::frame.MIDIState.mapping[i].range_low     = global_settings.midi_maps[i].range_low    ;
        HS::frame.MIDIState.mapping[i].range_high    = global_settings.midi_maps[i].range_high   ;
      }
      HS::frame.MIDIState.UpdateMidiChannelFilter();
      HS::frame.MIDIState.UpdateMaxPolyphony();
    }
#endif

    // old school EEPROM storage for legacy apps
    APPS_SERIAL_PRINTLN("Load app data: size is %u, PAGESIZE=%u, PAGES=%u, LENGTH=%u",
                  sizeof(AppData),
                  AppDataStorage::PAGESIZE,
                  AppDataStorage::PAGES,
                  AppDataStorage::LENGTH);

    if (!app_data_storage.Load(app_data)) {
      APPS_SERIAL_PRINTLN("Data not loaded, using defaults!");
    } else {
      RestoreAppData();
    }
  }

  // Validation to guard against junk data
  Chords::Validate();
  Scales::Validate();
#ifndef NO_HEMISPHERE
  WaveformManager::Validate();
#endif
  for (int i = 0; i < HS::TURING_MACHINE_COUNT; ++i) {
    HS::user_turing_machines[i].Validate();
  }

  size_t current_app_index = app_container.IndexOfAppByID(global_settings.current_app_id);
  if (current_app_index >= app_container.num_apps()) {
    APPS_SERIAL_PRINTLN("App id %02x not found, using default!", global_settings.current_app_id);
    current_app_index = DEFAULT_APP_INDEX;
  }

  APPS_SERIAL_PRINTLN("Encoder acceleration: %s", global_settings.encoders_enable_acceleration ? "enabled" : "disabled");
  ui.encoders_enable_acceleration(global_settings.encoders_enable_acceleration);

  set_current_app(current_app_index);
  transition.Complete();

  delay(100);
}

FLASHMEM
void draw_save_message(uint8_t c) {
  GRAPHICS_BEGIN_FRAME(true);
  uint8_t _size = c % 120;
  graphics.setPrintPos(37, 18);
  graphics.print("Saving...");
  graphics.drawRect(0, 28, _size, 8);
  GRAPHICS_END_FRAME();
}

#if defined(ARDUINO_TEENSY41) && defined(ENABLE_APP_TXI2C2MIDI)
FLASHMEM
static size_t AppMenuInsertionIndex() {
  #ifdef ENABLE_APP_MIDI
  return app_container.IndexOfAppByID(AppCaptainMIDI::kAppId) + 1;
  #else
  return 1;
  #endif
}
#endif

FLASHMEM
static size_t AppMenuToContainerIndex(size_t menu_index) {
#if defined(ARDUINO_TEENSY41) && defined(ENABLE_APP_TXI2C2MIDI)
  const size_t insertion = AppMenuInsertionIndex();
  const size_t tx = app_container.IndexOfAppByID(AppTXI2C2MIDI::kAppId);
  if (menu_index == insertion) return tx;
  if (menu_index > insertion && menu_index <= tx) return menu_index - 1;
#endif
  return menu_index;
}

FLASHMEM
static size_t ContainerToAppMenuIndex(size_t container_index) {
#if defined(ARDUINO_TEENSY41) && defined(ENABLE_APP_TXI2C2MIDI)
  const size_t insertion = AppMenuInsertionIndex();
  const size_t tx = app_container.IndexOfAppByID(AppTXI2C2MIDI::kAppId);
  if (container_index == tx) return insertion;
  if (container_index >= insertion && container_index < tx) return container_index + 1;
#endif
  return container_index;
}

FLASHMEM
bool Ui::AppSettings(bool drawmenu) {
  static menu::ScreenCursor<5> cursor;
  const int app_count = app_container.num_apps();
#if defined(ARDUINO_TEENSY41)
  const int menu_count = app_count + 1;
#else
  const int menu_count = app_count;
#endif
  static bool change_app = false;
  static bool save = false;
  static bool opened = false;

  // --- state change: entering App Menu
  if (!opened) {
    cursor.Init(0, menu_count - 1);
    cursor.Scroll(ContainerToAppMenuIndex(
        app_container.IndexOfAppByID(global_settings.current_app_id)));
    opened = true;
  }

#if defined(ARDUINO_TEENSY41)
  // Service this child page once per main-loop visit. Keeping it inside a
  // private modal loop prevents USB capture and other foreground services.
  if (ii_settings_open_) {
    IiSettings(drawmenu);
    return true;
  }
#endif

  // View - graphics
  if (drawmenu) {
    // assumes this is called from within a graphics frame context
    if (global_settings.encoders_enable_acceleration)
      graphics.drawBitmap8(120, 1, 4, bitmap_indicator_4x8);

    menu::SettingsListItem item;
    item.x = menu::kIndentDx + 8;
    item.y = (64 - (5 * menu::kMenuLineH)) / 2;

    for (int current = max(cursor.first_visible(), 0);
         current <= cursor.last_visible() && current < menu_count;
         ++current, item.y += menu::kMenuLineH) {
      item.selected = current == cursor.cursor_pos();
      item.SetPrintPos();
      graphics.movePrintPos(weegfx::Graphics::kFixedFontW, 0);
      // todo: make a secret button combo to switch to boring names
      // if (your_mom_is_boring)
      //   graphics.print(app_container[current]->boring_name());
      // else
      const size_t app_index = AppMenuToContainerIndex(current);
      if (current == app_count) graphics.print(phz_settings_ui::app_menu);
      else graphics.print(app_container[app_index].name());
      if (current < app_count && global_settings.current_app_id == app_container[app_index].id())
        graphics.drawBitmap8(0, item.y + 1, 8, ZAP_ICON);

      item.DrawCustom();
    }

#ifdef VOR
    VBiasManager *vbias_m = vbias_m->get();
    vbias_m->DrawPopupPerhaps();
#endif

    return true;
  }

  // UI - event handling
  if (!change_app && idle_time() < APP_SELECTION_TIMEOUT_MS) {
    while (event_queue_.available()) {
      UI::Event event = event_queue_.PullEvent();
      if (IgnoreEvent(event))
        continue;

      switch (event.control) {
      case CONTROL_ENCODER_R:
        if (UI::EVENT_ENCODER == event.type)
          cursor.Scroll(event.value);
        break;

      case CONTROL_BUTTON_R:
#if defined(ARDUINO_TEENSY41)
        if (cursor.cursor_pos() == app_count) {
          if (UI::EVENT_BUTTON_PRESS == event.type) {
            ii_settings_open_ = true;
            ii_settings_field_ = 0;
            ii_settings_message_ = "";
            event_queue_.Flush();
            SetButtonIgnoreMask();
            MENU_REDRAW = 1;
            return true;
          }
          break;
        }
#endif
        save = event.type == UI::EVENT_BUTTON_LONG_PRESS;
        change_app = event.type != UI::EVENT_BUTTON_DOWN; // true on button release
        break;
      case CONTROL_BUTTON_L:
        if (UI::EVENT_BUTTON_PRESS == event.type)
            ui.DebugStats();
        break;
      case CONTROL_BUTTON_UP:
#ifdef VOR
        // VBias menu for units without Range button
        if (UI::EVENT_BUTTON_LONG_PRESS == event.type || UI::EVENT_BUTTON_DOWN == event.type) {
          VBiasManager *vbias_m = vbias_m->get();
          vbias_m->AdvanceBias();
        }
#endif
        break;
      case CONTROL_BUTTON_DOWN:
        if (UI::EVENT_BUTTON_PRESS == event.type) {
            bool enabled = !global_settings.encoders_enable_acceleration;
            APPS_SERIAL_PRINTLN("Encoder acceleration: %s", enabled ? "enabled" : "disabled");
            ui.encoders_enable_acceleration(enabled);
            global_settings.encoders_enable_acceleration = enabled;
        }
        break;

        default: break;
      }
    }

    return true;
  }
  // else... idle time expired, or an app was selected via UI
  // cleanup and exit
  event_queue_.Flush();
  event_queue_.Poke();

  // --- state change: exiting App menu
  CORE::app_isr_enabled = false;
  delay(1);

  if (change_app) {
    app_switcher.set_current_app(AppMenuToContainerIndex(cursor.cursor_pos()));
    FreqMeasure.end();
    OC::DigitalInputs::reInit();
    if (save) {
      SaveAppData();
      // draw message:
      int cnt = 0;
      while(idle_time() < SETTINGS_SAVE_TIMEOUT_MS)
        draw_save_message((cnt++) >> 4);
      save = false;
    }
    change_app = false;
  }

  OC::ui.encoders_enable_acceleration(global_settings.encoders_enable_acceleration);

  // Restore state
  app_switcher.current_app()->DispatchAppEvent(APP_EVENT_RESUME);
  CORE::app_isr_enabled = true;

  opened = false;
  return false; // close menu
}

#if defined(ARDUINO_TEENSY41)
bool SaveIiAddress() {
  const bool was_enabled = CORE::app_isr_enabled;
  CORE::app_isr_enabled = false;
  const bool saved = phz::ii::SaveAddress(PhzConfig::myfs,
      PhzConfig::CONFIG_FILENAME, global_settings.ii_address, &global_settings.tx_mode
#ifdef AUDIO_INTERFACE
      , kUsbAudioMirrorSettingsKey,
      uint64_t(0x100 | (global_settings.usb_audio_mirror ? 1 : 0))
#endif
      );
  CORE::app_isr_enabled = was_enabled;
  return saved;
}

FLASHMEM void Ui::IiSettings(bool drawmenu) {
  auto& address = global_settings.ii_address;
  auto& mode = global_settings.tx_mode;
#ifdef AUDIO_INTERFACE
  constexpr int usb_audio_field = 3;
  constexpr int update_field = 4;
#else
  constexpr int update_field = 3;
#endif
  if (drawmenu) {
    // Main.cpp already owns this graphics frame, just like the parent menu.
    graphics.setPrintPos(1, 1); graphics.print(phz_settings_ui::title);
    graphics.drawLine(0, 10, 127, 10);
    graphics.drawLine(0, 12, 127, 12);

    char line[24];
    if (address.enabled()) snprintf(line, sizeof(line), "I2C Active: 0x%02X", address.active());
    else snprintf(line, sizeof(line), address.faulted() ? "I2C Active: FAULT" : "I2C Active: OFF");
    graphics.setPrintPos(1, 15);
    if (*ii_settings_message_) graphics.print(ii_settings_message_);
    else if (ii_settings_field_ == update_field) graphics.print(phz_settings_ui::update_hint);
    else if (ii_settings_field_ == 2) {
#ifdef ENABLE_APP_TXI2C2MIDI
      graphics.print("TX preview only");
#else
      graphics.print("TX app not installed");
#endif
    }
    else if (address.reboot_required()) graphics.print("Reboot required");
    else graphics.print(line);

    constexpr int visible_fields = 3;
    constexpr int first_field_y = 25;
    constexpr int field_height = 10;
    const int first_visible = max(0, ii_settings_field_ - (visible_fields - 1));
    for (int field = first_visible;
         field <= update_field && field < first_visible + visible_fields;
         ++field) {
      switch (field) {
      case 0:
        snprintf(line, sizeof(line), "Enable: %s%s",
          address.pending_enabled() ? "On" : "Off", address.dirty() ? " *" : "");
        break;
      case 1:
        snprintf(line, sizeof(line), "Pending: 0x%02X%s",
          address.pending(), address.dirty() ? " *" : "");
        break;
      case 2:
        snprintf(line, sizeof(line), "TX Mode: %s%s",
          mode.pending() == txi2c2midi::Mode::Single ? "Single" : "Legacy",
          mode.faulted() ? " !" : (mode.dirty() ? " *" : (mode.reboot_required() ? " R" : "")));
        break;
#ifdef AUDIO_INTERFACE
      case usb_audio_field:
        snprintf(line, sizeof(line), phz_settings_ui::usb_audio_row,
          global_settings.usb_audio_mirror ? "On" : "Off");
        break;
#endif
      default:
        snprintf(line, sizeof(line), "%s", phz_settings_ui::update_row);
        break;
      }

      const int y = first_field_y + (field - first_visible) * field_height;
      graphics.setPrintPos(1, y);
      graphics.print(line);
      if (field == ii_settings_field_) graphics.invertRect(0, y, 128, 9);
    }

    graphics.setPrintPos(0, 55);
    graphics.print(ii_settings_field_ == update_field ?
      phz_settings_ui::footer_update : phz_settings_ui::footer_save);
    return;
  }

  // Even continuous encoder traffic must yield back to USB/background work.
  for (size_t budget = kEventQueueDepth; budget && event_queue_.available(); --budget) {
    const UI::Event event = event_queue_.PullEvent();
    if (IgnoreEvent(event)) continue;
    MENU_REDRAW = 1;
    if (event.control == CONTROL_ENCODER_R && event.type == UI::EVENT_ENCODER) {
      if (event.value) {
        ii_settings_field_ += event.value > 0 ? 1 : -1;
        if (ii_settings_field_ < 0) ii_settings_field_ = 0;
        if (ii_settings_field_ > update_field) ii_settings_field_ = update_field;
        ii_settings_message_ = "";
      }
    } else if (event.control == CONTROL_ENCODER_L && event.type == UI::EVENT_ENCODER) {
      if (ii_settings_field_ == 0) {
        if (event.value) address.EditEnabled(event.value > 0);
      } else if (ii_settings_field_ == 1) {
        const int64_t next = int64_t(address.pending()) + event.value;
        address.Edit(next < 8 ? 8 : (next > 0x77 ? 0x77 : int(next)));
      } else if (ii_settings_field_ == 2 && event.value) {
        mode.Edit(event.value > 0 ? txi2c2midi::Mode::Legacy : txi2c2midi::Mode::Single);
#ifdef AUDIO_INTERFACE
      } else if (ii_settings_field_ == usb_audio_field && event.value) {
        global_settings.usb_audio_mirror = event.value > 0;
        AudioIO::SetUsbAudioMirror(global_settings.usb_audio_mirror);
#endif
      }
      ii_settings_message_ = "";
    } else if (event.control == CONTROL_BUTTON_R && ii_settings_field_ == update_field &&
               event.type == UI::EVENT_BUTTON_LONG_PRESS) {
      EnterFirmwareUpdateMode();
    } else if (event.control == CONTROL_BUTTON_R && ii_settings_field_ != update_field &&
               event.type == UI::EVENT_BUTTON_PRESS) {
      ii_settings_message_ = SaveIiAddress() ? "Saved. Reboot to use" : "SAVE FAILED";
    } else if (event.control == CONTROL_BUTTON_L && event.type == UI::EVENT_BUTTON_PRESS) {
      ii_settings_open_ = false;
      event_queue_.Flush();
      event_queue_.Poke();
      SetButtonIgnoreMask();
      return;
    }
  }
}
#endif

FLASHMEM
bool Ui::ConfirmReset() {

  SetButtonIgnoreMask();

  constexpr uint32_t kAutoCancelMs = 10000;
  elapsedMillis auto_cancel = 0;
  bool done = false;
  bool confirm = false;

  do {
    while (event_queue_.available()) {
      UI::Event event = event_queue_.PullEvent();
      if (IgnoreEvent(event))
        continue;
      const bool click = UI::EVENT_BUTTON_DOWN == event.type ||
                         UI::EVENT_BUTTON_PRESS == event.type;
      if (CONTROL_BUTTON_R == event.control && click) {
        confirm = true;
        done = true;
      } else if (CONTROL_BUTTON_L == event.control && click) {
        confirm = false;
        done = true;
      }
    }

    GRAPHICS_BEGIN_FRAME(true);
    graphics.setPrintPos(1, 2);
    graphics.print("Setup: Reset");
    graphics.drawLine(0, 10, 127, 10);
    graphics.drawLine(0, 12, 127, 12);

    graphics.setPrintPos(1, 15);
    graphics.print("Reset application");
    graphics.setPrintPos(1, 25);
    graphics.print("settings on EEPROM?");

    graphics.setPrintPos(1, 40);
    graphics.print("Auto-cancel: ");
    const uint32_t elapsed = auto_cancel;
    const uint32_t remaining = elapsed < kAutoCancelMs ? kAutoCancelMs - elapsed : 0;
    graphics.print(static_cast<int>((remaining + 999) / 1000));
    graphics.print("s");

    graphics.setPrintPos(0, 55);
    graphics.print("[CANCEL]         [OK]");

    GRAPHICS_END_FRAME();

    // setup() has not reached loop() yet, so this modal must explicitly let
    // the Teensy core service USB while it waits for a decision.
    yield();

    // Never let an unanswered destructive confirmation prevent startup. The
    // safe default preserves EEPROM and continues with the available data.
    if (auto_cancel >= kAutoCancelMs)
      done = true;

  } while (!done);

  // A BUTTON_DOWN decision returns before the physical release. Do not leak
  // that release (or another queued modal event) into the next screen.
  event_queue_.Flush();
  SetButtonIgnoreMask();

  return confirm;
}

FLASHMEM
void start_calibration() {
  OC::calibration_data.set_calstart();
  OC::app_switcher.set_current_app(0);
}

FLASHMEM
QuantizerCommandResult ApplyActiveQuantizerCommand(
    const QuantizerCommand& command) {
  // Q slots can be consumed from the app ISR. Keep the commit indivisible from
  // its perspective; transports must call this service from the main loop.
  struct PauseDsp {
    bool previous = CORE::app_isr_enabled;
    PauseDsp() { CORE::app_isr_enabled = false; }
    ~PauseDsp() { CORE::app_isr_enabled = previous; }
  } pause;
  return app_switcher.current_app()->ApplyQuantizerCommand(command);
}

}; // namespace OC

#if defined(ARDUINO_TEENSY41)
#include "ii/QuantizerAdapter.h"
namespace OC {
namespace {
class IiBackend final : public phz::ii::Backend {
 public:
  bool SnapshotHostIdentity(phz::ii::HostIdentity& out) override {
    return app_switcher.current_app()->SnapshotIiIdentity(out);
  }
  phz::ii::HostActionResult ApplyHostAction(const phz::ii::HostAction& action) override {
    struct PauseDsp {
      bool previous = CORE::app_isr_enabled;
      PauseDsp() { CORE::app_isr_enabled = false; }
      ~PauseDsp() { CORE::app_isr_enabled = previous; }
    } pause;
    return app_switcher.current_app()->ApplyIiHostAction(action);
  }
  bool Describe(const phz::ii::Write& write, phz::ii::Descriptor& out) override {
    return app_switcher.current_app()->DescribeIi(write, out);
  }
  bool Apply(const phz::ii::Prepared& update) override {
    return app_switcher.current_app()->ApplyIi(update);
  }
  bool ApplyQuantizer(const pc_qtz_request& request) override {
    QuantizerCommand command;
    if (!phz::ii::DecodeQuantizerRequest(request, command)) return false;
    return ApplyActiveQuantizerCommand(command) ==
           QuantizerCommandResult::Applied;
  }
};
}
void ServiceIi() {
  static IiBackend backend;
  phz::ii::ServiceFollower(backend);
}
}
#endif
