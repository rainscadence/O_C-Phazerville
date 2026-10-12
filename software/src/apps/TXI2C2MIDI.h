#pragma once

#include "../ii/Follower.h"
#include "../ii/QuantizerAdapter.h"
#include "../txi2c2midi/PreviewSession.h"
#include "../txi2c2midi/MidiFollower.h"
#include "../txi2c2midi/MidiMonitor.h"
#include "../txi2c2midi/MidiPitch.h"
#include "../txi2c2midi/Telemetry.h"
#include "../txi2c2midi/AudioState.h"
#include "../txi2c2midi/AudioPitch.h"
#include "../txi2c2midi/QuantizerSlots.h"
#include "../txi2c2midi/TxiQuantizer.h"
#include "../txi2c2midi/TxoPitch.h"
#include "../AudioIO.h"

namespace txi2c2midi_ui {
// UI-only strings stay in flash rather than scarce DTCM.
static const char label_0[] PROGMEM = "Bus inactive: preview";
static const char label_1[] PROGMEM = "Notes: USB only";
static const char label_2[] PROGMEM = "Bus inactive";
static const char label_3[] PROGMEM = "Inactive";
static const char label_4[] PROGMEM = "TXI2C2MIDI PREVIEW";
static const char label_5[] PROGMEM = "Mode: ";
static const char label_6[] PROGMEM = "CONFIG FAULT";
static const char label_7[] PROGMEM = "Single";
static const char label_8[] PROGMEM = "Legacy";
static const char label_9[] PROGMEM = "Global: ";
static const char label_10[] PROGMEM = "FAULT";
static const char label_11[] PROGMEM = "Off";
static const char* const wave_names[] = {"Sine", "Triangle", "Saw", "Square", "S&H", "White"};
static const char* const note_names[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// Keep encoder help under the control it describes. A single right-encoder
// action must not look like a left-encoder action merely because it is the
// only footer label on the page.
static void Footer(const char* left, const char* right) {
  if (left) gfxPrint(0,56,left);
  if (right) gfxPrint(128 - strlen(right) * 6,56,right);
}

static void QuantizerText(char *text, size_t size, uint8_t selection,
                          uint8_t shared_slot) {
  if (selection == uint8_t(txi2c2midi::QuantizerSelection::Off))
    snprintf(text,size,"Off");
  else if (selection == uint8_t(txi2c2midi::QuantizerSelection::Shared))
    snprintf(text,size,"Shared(Q%u)",unsigned(shared_slot + 1));
  else
    snprintf(text,size,"Local Q%u",unsigned(selection - 1));
}

// There are 153 ordered octave-window pairs in -8..8. Zero remains the compact
// Range: Off representation, so every valid state fits in one byte.
static uint8_t EncodeRange(const OC::OctaveWindow& range) {
  if (!range.enabled()) return 0;
  uint8_t code = 1;
  for (int low = OC::OctaveWindow::kMinOctave;
       low <= OC::OctaveWindow::kMaxOctave; ++low) {
    for (int high = low; high <= OC::OctaveWindow::kMaxOctave; ++high) {
      if (range.low() == low && range.high() == high) return code;
      ++code;
    }
  }
  return 0;
}

static bool DecodeRange(uint8_t code, OC::OctaveWindow& range) {
  if (!code) {
    range.Disable();
    return true;
  }
  uint8_t candidate = 1;
  for (int low = OC::OctaveWindow::kMinOctave;
       low <= OC::OctaveWindow::kMaxOctave; ++low) {
    for (int high = low; high <= OC::OctaveWindow::kMaxOctave; ++high) {
      if (candidate++ == code) return range.Set(low, high);
    }
  }
  return false;
}
}

namespace txi2c2midi {
#if defined(ARDUINO_TEENSY41)
extern TxoPitch<phz::ii::InterruptLock> txo_pitch;
extern MidiPitch midi_pitch;
#endif
}

// Single-address TXI raw, TXO and USB-note engine/monitor.
OC_APP_CLASS(AppTXI2C2MIDI, TWOCCS("TX"), "TXI2C2MIDI", "TXI2C2MIDI") {
 public:
  OC_APP_INTERFACE_DECLARE(
      AppTXI2C2MIDI,
      84 + txi2c2midi::UserScaleSnapshot::kSerializedBytes);
  PHZ_REMOTE_CODE bool HandleRemoteUi(char selector) override;
  PHZ_REMOTE_CODE phz::remote::UiState RemoteUiState() const override;
  OC::QuantizerCommandResult ApplyQuantizerCommand(
      const OC::QuantizerCommand& command) override;
  bool ApplyQuantizerWire(const pc_qtz_request& request);
  bool IiReady() override { return bus_.Registered(); }
  uint8_t IiFamily() const override { return PC_FAMILY_MIDI; }
  uint16_t IiQuantizerScaleCount() const override {
    return OC::Scales::NUM_SCALES;
  }
 private:
  struct Bus {
    bool Registered() { return phz::ii::FollowerRegistered(); }
    void Stop() { phz::ii::StopFollower(); }
    void Restore() {
      phz::ii::StartFollower(OC::global_settings.ii_address, I2C_Expansion);
    }
  } bus_;
  txi2c2midi::PreviewSession session_;
  struct UsbNotes {
    txi2c2midi::MidiMonitor monitor;
    void On(uint8_t ch,uint8_t note,uint8_t velocity) {
      usbMIDI.sendNoteOn(note,velocity,ch+1);
      monitor.Record(ch,note,velocity,true);
    }
    void Off(uint8_t ch,uint8_t note) {
      usbMIDI.sendNoteOff(note,0,ch+1);
      monitor.Record(ch,note,0,false);
    }
  } notes_;
  txi2c2midi::Telemetry<phz::ii::InterruptLock> telemetry_;
  const char* message_ = txi2c2midi_ui::label_0;
  int page_ = 0; // Settings, TXI, TXO, MIDI, Audio L, Audio R, Quantizers.
  int channel_ = 0; // TXI/TXO selection0..7; MIDI Auto=0/manual=1..16.
  uint8_t telemetry_divider_ = 0;
  uint8_t txo_cv_mask_ = 0xf0;
#if !defined(ARDUINO_TEENSY41)
  txi2c2midi::TxoPitch<phz::ii::InterruptLock> txo_pitch_;
#endif
  uint8_t txo_cursor_ = 0;
  bool txo_settings_open_ = false;
  bool txo_editing_ = false;
  bool txo_dirty_ = false;
#if !defined(ARDUINO_TEENSY41)
  txi2c2midi::MidiPitch midi_pitch_;
#endif
  bool midi_settings_open_ = false;
  bool midi_editing_ = false;
  mutable bool midi_dirty_ = false;
  bool midi_rejected_ = false;
  txi2c2midi::AudioState audio_;
  uint8_t audio_cursor_[2] = {0, 0};
  bool audio_editing_ = false;
  uint8_t shared_quantizer_slot_ = 0;
  uint8_t quantizer_cursor_ = 0;
  bool quantizer_editing_ = false;
  bool quantizer_imported_ = false;
  mutable bool quantizer_dirty_ = false;
  mutable bool txi_knob_dirty_ = false;
  void ApplyAudioVoice(unsigned voice, bool reset_phase = false);
  txi2c2midi::TxoPitch<phz::ii::InterruptLock>& TxoPitchState();
  const txi2c2midi::TxoPitch<phz::ii::InterruptLock>& TxoPitchState() const;
  txi2c2midi::MidiPitch& MidiPitchState();
  const txi2c2midi::MidiPitch& MidiPitchState() const;
  const braids::Scale& QuantizerScale(int scale) const;
  void ConfigureTxoPitch(unsigned channel);
  void ConfigureAllTxoPitch();
  void ConfigureAllMidiPitch();
  txi2c2midi::MidiSelectionResult SetMidiQuantizer(
      unsigned channel, uint8_t selection);
  void SaveTxoSettings();
  void SaveMidiSettings();
  void PublishTxiKnobs();
};

FLASHMEM txi2c2midi::TxoPitch<phz::ii::InterruptLock>&
AppTXI2C2MIDI::TxoPitchState() {
#if defined(ARDUINO_TEENSY41)
  return txi2c2midi::txo_pitch;
#else
  return txo_pitch_;
#endif
}
FLASHMEM const txi2c2midi::TxoPitch<phz::ii::InterruptLock>&
AppTXI2C2MIDI::TxoPitchState() const {
#if defined(ARDUINO_TEENSY41)
  return txi2c2midi::txo_pitch;
#else
  return txo_pitch_;
#endif
}

FLASHMEM txi2c2midi::MidiPitch& AppTXI2C2MIDI::MidiPitchState() {
#if defined(ARDUINO_TEENSY41)
  return txi2c2midi::midi_pitch;
#else
  return midi_pitch_;
#endif
}
FLASHMEM const txi2c2midi::MidiPitch& AppTXI2C2MIDI::MidiPitchState() const {
#if defined(ARDUINO_TEENSY41)
  return txi2c2midi::midi_pitch;
#else
  return midi_pitch_;
#endif
}

FLASHMEM void AppTXI2C2MIDI::Init() {
  audio_.Reset();
  txi2c2midi::quantizer_slots.Reset(OC::Scales::SCALE_SEMI);
  txi2c2midi::user_scale_snapshot.Reset(
      OC::Scales::GetScale(OC::Scales::SCALE_SEMI));
  txi2c2midi::user_scale_snapshot.Capture(
      OC::user_scales, OC::Scales::SCALE_USER_COUNT);
  shared_quantizer_slot_ = 0;
  TxoPitchState().Reset();
  ConfigureAllTxoPitch();
  MidiPitchState().Reset();
  ConfigureAllMidiPitch();
  quantizer_dirty_ = false;
  for (unsigned channel = 0; channel < 8; ++channel)
    telemetry_.SetKnob(channel,0);
  txi_knob_dirty_ = false;
}
FLASHMEM const braids::Scale& AppTXI2C2MIDI::QuantizerScale(int scale) const {
  return scale >= 0 &&
      scale < int(txi2c2midi::UserScaleSnapshot::kCount)
      ? txi2c2midi::user_scale_snapshot.Get(unsigned(scale))
      : OC::Scales::GetScale(scale);
}

FLASHMEM void AppTXI2C2MIDI::PublishTxiKnobs() {
  int16_t knobs[8];
  for (unsigned channel = 0; channel < 8; ++channel)
    knobs[channel] = telemetry_.Knob(channel).value;
  txi2c2midi::MidiFollower::PublishTxiKnobs(knobs,8);
}
FLASHMEM void AppTXI2C2MIDI::ConfigureTxoPitch(unsigned channel) {
  if (channel >= 8) return;
  const uint8_t selection = TxoPitchState().Selection(channel);
  if (selection == uint8_t(txi2c2midi::QuantizerSelection::Off)) {
    TxoPitchState().Configure(channel,nullptr,nullptr);
    return;
  }
  const unsigned slot = selection ==
      uint8_t(txi2c2midi::QuantizerSelection::Shared)
      ? shared_quantizer_slot_ : selection - 2;
  OC::QuantizerSettings settings;
  if (!txi2c2midi::quantizer_slots.Read(slot,settings)) {
    TxoPitchState().Configure(channel,nullptr,nullptr);
    return;
  }
  TxoPitchState().Configure(
      channel,&settings,&QuantizerScale(settings.scale));
}
FLASHMEM void AppTXI2C2MIDI::ConfigureAllTxoPitch() {
  for (unsigned channel = 0; channel < 8; ++channel)
    ConfigureTxoPitch(channel);
}
FLASHMEM void AppTXI2C2MIDI::ConfigureAllMidiPitch() {
  MidiPitchState().SetSharedSlot(shared_quantizer_slot_);
  for (unsigned slot = 0; slot < txi2c2midi::QuantizerSlots::kCount; ++slot) {
    OC::QuantizerSettings settings;
    if (txi2c2midi::quantizer_slots.Read(slot,settings))
      MidiPitchState().ConfigureSlot(
          slot,&settings,&QuantizerScale(settings.scale));
    else
      MidiPitchState().ConfigureSlot(slot,nullptr,nullptr);
  }
}
FLASHMEM txi2c2midi::MidiSelectionResult AppTXI2C2MIDI::SetMidiQuantizer(unsigned channel, uint8_t selection) {
  return MidiPitchState().TrySetSelection(channel,selection);
}
FLASHMEM void AppTXI2C2MIDI::SaveTxoSettings() {
  if (!txo_dirty_) return;
  const bool app_isr_was_enabled = OC::CORE::app_isr_enabled;
  OC::CORE::app_isr_enabled = false;
  OC::save_app_data();
  OC::CORE::app_isr_enabled = app_isr_was_enabled;
  txo_dirty_ = false;
}
FLASHMEM void AppTXI2C2MIDI::SaveMidiSettings() {
  if (!midi_dirty_) return;
  const bool app_isr_was_enabled = OC::CORE::app_isr_enabled;
  OC::CORE::app_isr_enabled = false;
  OC::save_app_data();
  OC::CORE::app_isr_enabled = app_isr_was_enabled;
  midi_dirty_ = false;
}
FLASHMEM OC::QuantizerCommandResult AppTXI2C2MIDI::ApplyQuantizerCommand(
    const OC::QuantizerCommand& command) {
  class Target final : public OC::QuantizerCommandTarget {
   public:
    bool Read(unsigned slot, OC::QuantizerSettings& settings) const override {
      return txi2c2midi::quantizer_slots.Read(slot, settings);
    }
    bool Replace(unsigned slot,
                 const OC::QuantizerSettings& settings) override {
      return txi2c2midi::quantizer_slots.Replace(
          slot, settings, OC::Scales::NUM_SCALES);
    }
    void Changed(unsigned) override {}
  } target;

  const auto result = OC::ApplyQuantizerCommand(
      target, command, OC::Scales::NUM_SCALES, OC::Scales::SCALE_SEMI);
  if (result == OC::QuantizerCommandResult::Applied) {
    quantizer_imported_ = false;
    quantizer_dirty_ = true;
    ApplyAudioVoice(0);
    ApplyAudioVoice(1);
    ConfigureAllTxoPitch();
    ConfigureAllMidiPitch();
  }
  return result;
}
FLASHMEM bool AppTXI2C2MIDI::ApplyQuantizerWire(
    const pc_qtz_request& request) {
  OC::QuantizerCommand command;
  return phz::ii::DecodeQuantizerRequest(request,command) &&
      ApplyQuantizerCommand(command) == OC::QuantizerCommandResult::Applied;
}
PHZ_REMOTE_CODE phz::remote::UiState AppTXI2C2MIDI::RemoteUiState() const {
  phz::remote::UiState state;
  const auto& address = OC::global_settings.ii_address;
  state.known = true;
  // Scope 1 advertises the retired TX-only editor to older USB QA clients.
  // This page is now read-only; global settings owns address editing.
  state.qa_scope = 0;
  state.qa_cursor = 0;
  state.qa_editing = false;
  state.qa_active = address.enabled() ? address.active() : 0;
  state.qa_pending = address.pending_enabled() ? address.pending() : 0;
  state.qa_dirty = address.dirty();
  state.qa_fault = address.faulted() || OC::global_settings.tx_mode.faulted();
  return state;
}
PHZ_REMOTE_CODE bool AppTXI2C2MIDI::HandleRemoteUi(char selector) {
  if (selector == 'Q') return true; // Internal read-only availability probe.
  if (selector == 'J' || selector == 'K' || selector == 'N' || selector == 'P') {
    const bool left = selector == 'J' || selector == 'K';
    const int delta = selector == 'J' || selector == 'N' ? -1 : 1;
    HandleEncoderEvent(UI::Event(UI::EVENT_ENCODER,
      left ? OC::CONTROL_ENCODER_L : OC::CONTROL_ENCODER_R, delta, 0));
    return true;
  }
  if (selector == 'E') {
    // Settings is read-only; editing/saving belongs to global ii settings.
    return true;
  }
  if (selector == 'C') {
    HandleButtonEvent(UI::Event(UI::EVENT_BUTTON_PRESS, OC::CONTROL_BUTTON_L, 0, 0));
    return true;
  }
  return false;
}
FLASHMEM size_t AppTXI2C2MIDI::SaveAppData(util::StreamBufferWriter& stream) const {
  stream.Write<uint16_t>(uint16_t(0x0900) | txo_cv_mask_);
  for (unsigned voice = 0; voice < 2; ++voice) {
    const auto& state = audio_.Voice(voice);
    stream.Write<uint8_t>(state.waveform);
    stream.Write<uint8_t>(state.level_percent);
    const uint32_t value = state.PitchMode() == txi2c2midi::AudioPitchMode::Note
        ? uint16_t(state.pitch) : state.frequency_millihz;
    stream.Write<uint32_t>(value);
  }
  const auto& left = audio_.Voice(0);
  const auto& right = audio_.Voice(1);
  uint16_t flags = uint16_t(left.QuantizerSelection() & 0x0f) |
      uint16_t(left.PitchMode() == txi2c2midi::AudioPitchMode::Note) << 4 |
      uint16_t(right.QuantizerSelection() & 0x0f) << 5 |
      uint16_t(right.PitchMode() == txi2c2midi::AudioPitchMode::Note) << 9 |
      uint16_t(shared_quantizer_slot_ & 0x07) << 10;
  stream.Write<uint16_t>(flags);
  for (unsigned slot = 0; slot < txi2c2midi::QuantizerSlots::kCount; ++slot) {
    const auto& q = txi2c2midi::quantizer_slots.Get(slot);
    const uint64_t packed = uint64_t(uint8_t(q.scale) & 0x3f) |
        uint64_t(uint8_t(q.root) & 0x0f) << 6 |
        uint64_t(uint8_t(q.octave + 8) & 0x1f) << 10 |
        uint64_t(q.mask) << 15 |
        uint64_t(txi2c2midi_ui::EncodeRange(q.range)) << 31;
    for (unsigned byte = 0; byte < 5; ++byte)
      stream.Write<uint8_t>(uint8_t(packed >> (byte * 8)));
  }
  for (unsigned channel = 0; channel < 8; ++channel)
    stream.Write<int16_t>(telemetry_.Knob(channel).value);
  uint32_t txo_quantizers = 0;
  for (unsigned channel = 0; channel < 8; ++channel)
    txo_quantizers |= uint32_t(TxoPitchState().Selection(channel) & 0x0f)
        << (channel * 4);
  stream.Write<uint32_t>(txo_quantizers);
  for (unsigned scale = 0;
       scale < txi2c2midi::UserScaleSnapshot::kCount; ++scale) {
    const auto& snapshot = txi2c2midi::user_scale_snapshot.Get(scale);
    stream.Write<int16_t>(snapshot.span);
    stream.Write<uint8_t>(uint8_t(snapshot.num_notes));
    for (unsigned note = 0;
         note < txi2c2midi::UserScaleSnapshot::kMaxNotes; ++note)
      stream.Write<int16_t>(snapshot.notes[note]);
  }
  for (unsigned pair = 0; pair < 8; ++pair) {
    const uint8_t packed =
        uint8_t(MidiPitchState().Selection(pair * 2) & 0x0f) |
        uint8_t(MidiPitchState().Selection(pair * 2 + 1) & 0x0f) << 4;
    stream.Write<uint8_t>(packed);
  }
  if (!stream.overflow()) {
    quantizer_dirty_ = false;
    txi_knob_dirty_ = false;
    midi_dirty_ = false;
  }
  return stream.written();
}
FLASHMEM size_t AppTXI2C2MIDI::RestoreAppData(util::StreamBufferReader& stream) {
  txo_cv_mask_ = 0xf0;
  audio_.Reset();
  txi2c2midi::quantizer_slots.Reset(OC::Scales::SCALE_SEMI);
  txi2c2midi::user_scale_snapshot.Reset(
      OC::Scales::GetScale(OC::Scales::SCALE_SEMI));
  txi2c2midi::user_scale_snapshot.Capture(
      OC::user_scales, OC::Scales::SCALE_USER_COUNT);
  shared_quantizer_slot_ = 0;
  TxoPitchState().Reset();
  MidiPitchState().Reset();
  txo_cursor_ = 0;
  txo_settings_open_ = false;
  txo_editing_ = false;
  txo_dirty_ = false;
  midi_settings_open_ = false;
  midi_editing_ = false;
  midi_dirty_ = false;
  midi_rejected_ = false;
  quantizer_dirty_ = false;
  for (unsigned channel = 0; channel < 8; ++channel)
    telemetry_.SetKnob(channel,0);
  txi_knob_dirty_ = false;
  if (stream.available() < sizeof(uint16_t)) {
    ConfigureAllTxoPitch();
    ConfigureAllMidiPitch();
    return stream.read();
  }
  const uint16_t saved = stream.Read<uint16_t>();
  const uint8_t version = saved >> 8;
  if (version >= 1 && version <= 9) txo_cv_mask_ = uint8_t(saved);
  if (version >= 1 && version <= 6)
    TxoPitchState().Reset(uint8_t(txi2c2midi::QuantizerSelection::Off));
  if (version >= 2 && version <= 9 && stream.available() >= 14) {
    uint32_t values[2]{};
    for (unsigned voice = 0; voice < 2; ++voice) {
      const uint8_t waveform = stream.Read<uint8_t>();
      const uint8_t level = stream.Read<uint8_t>();
      values[voice] = stream.Read<uint32_t>();
      audio_.SetWaveform(voice, waveform);
      audio_.SetLevel(voice, level);
    }
    const uint16_t flags = stream.Read<uint16_t>();
    for (unsigned voice = 0; voice < 2; ++voice) {
      const unsigned shift = voice ? 5 : 0;
      const bool note = version >= 5 && ((flags >> (voice ? 9 : 4)) & 1);
      if (version >= 5) audio_.SetQuantizer(voice, (flags >> shift) & 0x0f);
      if (note) {
        audio_.SetPitchMode(voice, txi2c2midi::AudioPitchMode::Note);
        audio_.SetPitch(voice, int16_t(uint16_t(values[voice])));
      } else {
        audio_.SetFrequency(voice, values[voice]);
      }
    }
    if (version >= 5) shared_quantizer_slot_ = uint8_t((flags >> 10) & 0x07);
    if (version == 3 && stream.available() >= 48) {
      for (unsigned slot = 0; slot < txi2c2midi::QuantizerSlots::kCount; ++slot) {
        const int scale = stream.Read<int16_t>();
        const int root = stream.Read<int8_t>();
        const int octave = stream.Read<int8_t>();
        const uint16_t mask = stream.Read<uint16_t>();
        txi2c2midi::quantizer_slots.Set(
            slot,scale,root,octave,mask,OC::Scales::NUM_SCALES);
      }
    } else if ((version == 4 || version == 5) && stream.available() >= 64) {
      for (unsigned slot = 0; slot < txi2c2midi::QuantizerSlots::kCount; ++slot) {
        const int scale = stream.Read<int16_t>();
        const int root = stream.Read<int8_t>();
        const int octave = stream.Read<int8_t>();
        const uint16_t mask = stream.Read<uint16_t>();
        const int low = stream.Read<int8_t>();
        const int high = stream.Read<int8_t>();
        txi2c2midi::quantizer_slots.Set(
            slot,scale,root,octave,mask,OC::Scales::NUM_SCALES);
        if (!txi2c2midi::quantizer_slots.SetRange(slot,low,high))
          txi2c2midi::quantizer_slots.DisableRange(slot);
      }
    } else if (version == 6 && stream.available() >= 64) {
      for (unsigned slot = 0; slot < txi2c2midi::QuantizerSlots::kCount; ++slot) {
        OC::QuantizerSettings settings;
        settings.scale = stream.Read<uint8_t>();
        settings.root = stream.Read<int8_t>();
        settings.octave = stream.Read<int8_t>();
        settings.mask = stream.Read<uint16_t>();
        const uint8_t range = stream.Read<uint8_t>();
        const bool valid_range = txi2c2midi_ui::DecodeRange(
            range,settings.range);
        if (valid_range)
          txi2c2midi::quantizer_slots.Replace(
              slot,settings,OC::Scales::NUM_SCALES);
      }
      for (unsigned channel = 0; channel < 8; ++channel) {
        const int value = stream.Read<int16_t>();
        telemetry_.SetKnob(
            channel,value >= 0 && value <= 16383 ? int16_t(value) : 0);
      }
    } else if (version == 7 && stream.available() >= 64) {
      for (unsigned slot = 0; slot < txi2c2midi::QuantizerSlots::kCount; ++slot) {
        uint64_t packed = 0;
        for (unsigned byte = 0; byte < 5; ++byte)
          packed |= uint64_t(stream.Read<uint8_t>()) << (byte * 8);
        OC::QuantizerSettings settings;
        settings.scale = packed & 0x3f;
        settings.root = int8_t((packed >> 6) & 0x0f);
        settings.octave = int8_t((packed >> 10) & 0x1f) - 8;
        settings.mask = uint16_t((packed >> 15) & 0xffff);
        const uint8_t range = uint8_t((packed >> 31) & 0xff);
        const bool valid = !(packed >> 39) &&
            txi2c2midi_ui::DecodeRange(range,settings.range);
        if (valid)
          txi2c2midi::quantizer_slots.Replace(
              slot,settings,OC::Scales::NUM_SCALES);
      }
      for (unsigned channel = 0; channel < 8; ++channel) {
        const int value = stream.Read<int16_t>();
        telemetry_.SetKnob(
            channel,value >= 0 && value <= 16383 ? int16_t(value) : 0);
      }
      const uint32_t txo_quantizers = stream.Read<uint32_t>();
      const uint32_t reserved = stream.Read<uint32_t>();
      for (unsigned channel = 0; channel < 8; ++channel) {
        const uint8_t selection = uint8_t(
            txo_quantizers >> (channel * 4)) & 0x0f;
        TxoPitchState().SetSelection(
            channel, reserved || selection >
                txi2c2midi::TxoPitch<phz::ii::InterruptLock>::kMaxSelection
                ? uint8_t(txi2c2midi::QuantizerSelection::Off) : selection);
      }
    } else if ((version == 8 || version == 9) &&
               stream.available() >= (version == 9 ? 208 : 200)) {
      for (unsigned slot = 0;
           slot < txi2c2midi::QuantizerSlots::kCount; ++slot) {
        uint64_t packed = 0;
        for (unsigned byte = 0; byte < 5; ++byte)
          packed |= uint64_t(stream.Read<uint8_t>()) << (byte * 8);
        OC::QuantizerSettings settings;
        settings.scale = packed & 0x3f;
        settings.root = int8_t((packed >> 6) & 0x0f);
        settings.octave = int8_t((packed >> 10) & 0x1f) - 8;
        settings.mask = uint16_t((packed >> 15) & 0xffff);
        const uint8_t range = uint8_t((packed >> 31) & 0xff);
        const bool valid = !(packed >> 39) &&
            txi2c2midi_ui::DecodeRange(range,settings.range);
        if (valid)
          txi2c2midi::quantizer_slots.Replace(
              slot,settings,OC::Scales::NUM_SCALES);
      }
      for (unsigned channel = 0; channel < 8; ++channel) {
        const int value = stream.Read<int16_t>();
        telemetry_.SetKnob(
            channel,value >= 0 && value <= 16383 ? int16_t(value) : 0);
      }
      const uint32_t txo_quantizers = stream.Read<uint32_t>();
      for (unsigned channel = 0; channel < 8; ++channel) {
        const uint8_t selection = uint8_t(
            txo_quantizers >> (channel * 4)) & 0x0f;
        TxoPitchState().SetSelection(
            channel, selection > txi2c2midi::TxoPitch<
                phz::ii::InterruptLock>::kMaxSelection
                ? uint8_t(txi2c2midi::QuantizerSelection::Off) : selection);
      }
      auto decoded_scales = txi2c2midi::user_scale_snapshot;
      bool valid_scales = true;
      for (unsigned scale = 0;
           scale < txi2c2midi::UserScaleSnapshot::kCount; ++scale) {
        braids::Scale snapshot{};
        snapshot.span = stream.Read<int16_t>();
        snapshot.num_notes = stream.Read<uint8_t>();
        for (unsigned note = 0;
             note < txi2c2midi::UserScaleSnapshot::kMaxNotes; ++note)
          snapshot.notes[note] = stream.Read<int16_t>();
        valid_scales = decoded_scales.Replace(scale,snapshot) && valid_scales;
      }
      if (valid_scales)
        txi2c2midi::user_scale_snapshot = decoded_scales;
      else
        quantizer_dirty_ = true;
      if (version == 9) {
        for (unsigned pair = 0; pair < 8; ++pair) {
          const uint8_t packed = stream.Read<uint8_t>();
          MidiPitchState().RestoreSelection(pair * 2,packed & 0x0f);
          MidiPitchState().RestoreSelection(pair * 2 + 1,(packed >> 4) & 0x0f);
        }
      }
    }
  }
  if (version >= 1 && version <= 7) {
    for (unsigned slot = 0;
         slot < txi2c2midi::QuantizerSlots::kCount; ++slot)
      if (txi2c2midi::quantizer_slots.Get(slot).scale <
          int(txi2c2midi::UserScaleSnapshot::kCount))
        quantizer_dirty_ = true;
  }
  ConfigureAllTxoPitch();
  ConfigureAllMidiPitch();
  return stream.read();
}
FLASHMEM void AppTXI2C2MIDI::ApplyAudioVoice(unsigned voice, bool reset_phase) {
  const auto& state = audio_.Voice(voice);
  float frequency = float(state.frequency_millihz) * 0.001f;
  if (state.PitchMode() == txi2c2midi::AudioPitchMode::Note) {
    int32_t pitch = state.pitch;
    if (state.QuantizerSelection() !=
        uint8_t(txi2c2midi::AudioQuantizerSelection::Off)) {
      const unsigned slot = state.QuantizerSelection() ==
          uint8_t(txi2c2midi::AudioQuantizerSelection::Shared)
          ? shared_quantizer_slot_ : state.QuantizerSelection() - 2;
      OC::QuantizerSettings settings;
      if (txi2c2midi::quantizer_slots.Read(slot, settings))
        pitch = txi2c2midi::QuantizeAudioPitch(
            settings, QuantizerScale(settings.scale), pitch);
    }
    frequency = txi2c2midi::AudioPitchFrequency(pitch);
  }
  OC::AudioIO::ConfigureTxVoice(voice, state.enabled,
      static_cast<OC::AudioIO::TxWaveform>(state.waveform),
      frequency, state.level_percent, reset_phase);
}
FLASHMEM void AppTXI2C2MIDI::HandleAppEvent(OC::AppEvent event) {
  if (event == OC::APP_EVENT_RESUME) {
    page_ = 3; channel_ = 0; telemetry_divider_ = 0;
    txo_cursor_ = 0;
    txo_settings_open_ = false;
    txo_editing_ = false;
    txo_dirty_ = false;
    midi_settings_open_ = false;
    midi_editing_ = false;
    midi_rejected_ = false;
    audio_editing_ = false;
    quantizer_editing_ = false;
    quantizer_imported_ = false;
    audio_.Silence();
    OC::AudioIO::AcquireTxAudio();
    ApplyAudioVoice(0); ApplyAudioVoice(1);
    notes_.monitor.Clear(); telemetry_.ClearRuntime();
    session_.Enter(bus_);
    const auto& settings = OC::global_settings;
    const uint8_t address = txi2c2midi::SingleAddress(
        settings.ii_address, settings.tx_mode, I2C_Expansion);
    txi2c2midi::MidiFollower::Start(address,txo_cv_mask_,telemetry_,millis(),
                                    OC::Scales::NUM_SCALES);
    ConfigureAllTxoPitch();
    TxoPitchState().ClearPrepared();
    PublishTxiKnobs();
    message_ = txi2c2midi::MidiFollower::Active() ? txi2c2midi_ui::label_1 : txi2c2midi_ui::label_2;
  }
  if (event == OC::APP_EVENT_SUSPEND) {
    SaveTxoSettings();
    audio_.Silence();
    OC::AudioIO::ReleaseTxAudio();
    txi2c2midi::MidiFollower::Stop();
    TxoPitchState().ClearPrepared();
    txi2c2midi::MidiFollower::ReleaseNotes(notes_);
    session_.Leave(bus_);
  }
}
FLASHMEM void AppTXI2C2MIDI::Loop() {
  txi2c2midi::MidiFollower::ServiceWithPitch(
      notes_,telemetry_,*this,MidiPitchState(),millis());
  int32_t values[8];
  txi2c2midi::MidiFollower::SnapshotTxo(values);
  TxoPitchState().Publish(values,8,txo_cv_mask_);
}
FLASHMEM __attribute__((noinline))
void AppTXI2C2MIDI::Process(OC::IOFrame* frame) {
  if (++telemetry_divider_ >= 16) {
    telemetry_divider_ = 0;
    telemetry_.CaptureInputs(frame->cv.values, frame->cv.pitch_values,
                             ADC_CHANNEL_COUNT);
    txi2c2midi::MidiFollower::PublishTxi(frame->cv.values,ADC_CHANNEL_COUNT);
    OC::QuantizerSettings settings;
    if (txi2c2midi::quantizer_slots.Read(shared_quantizer_slot_,settings)) {
      txi2c2midi::TxiQuantizedValue knobs[8];
      txi2c2midi::TxiQuantizedValue inputs[8];
      const auto& scale = QuantizerScale(settings.scale);
      for (unsigned channel = 0; channel < 8; ++channel) {
        knobs[channel] = txi2c2midi::QuantizeTxiPitch(
            settings,scale,txi2c2midi::TxiKnobPitch(
                telemetry_.Knob(channel).value));
        inputs[channel] = txi2c2midi::QuantizeTxiPitch(
            settings,scale,frame->cv.pitch_values[channel]);
      }
      txi2c2midi::MidiFollower::PublishTxiQuantized(
          knobs,inputs,ADC_CHANNEL_COUNT);
    }
  }
  TxoPitchState().Snapshot(frame->outputs.values);
  for (unsigned channel = 0; channel < 8; ++channel)
    frame->outputs.modes[channel] = txo_cv_mask_ & (uint8_t(1) << channel)
        ? OC::OUTPUT_MODE_PITCH : OC::OUTPUT_MODE_GATE_5V;
}
FLASHMEM void AppTXI2C2MIDI::GetIOConfig(OC::IOConfig& config) const {
  for (unsigned channel = 0; channel < 8; ++channel) {
    const bool cv = txo_cv_mask_ & (uint8_t(1) << channel);
    config.outputs[channel].set_printf(
        cv ? OC::OUTPUT_MODE_PITCH : OC::OUTPUT_MODE_GATE_5V,
        "TXO %u %s",channel+1,cv ? "CV" : "Gate");
  }
}
FLASHMEM void AppTXI2C2MIDI::DrawDebugInfo() const {}
FLASHMEM void AppTXI2C2MIDI::DrawMenu() const {
  if (page_ == 6) {
    gfxPrint(0,1,"Quantizers"); graphics.drawLine(0,11,127,11);
    char line[24];
    snprintf(line,sizeof(line),"%c Shared: Q%u",
             quantizer_cursor_ == 0 ? '>' : ' ',
             unsigned(shared_quantizer_slot_ + 1));
    gfxPrint(0,16,line);
    snprintf(line,sizeof(line),"%c Import Quadrants",
             quantizer_cursor_ == 1 ? '>' : ' ');
    gfxPrint(0,28,line);
    gfxPrint(0,40,quantizer_dirty_ ?
             (quantizer_imported_ ? "Imported * unsaved" :
                                    "Edited * unsaved") :
             "Replaces local Q1-Q8");
    graphics.drawLine(0,54,127,54);
    if (quantizer_editing_) txi2c2midi_ui::Footer(nullptr,"R:Adjust/Click");
    else txi2c2midi_ui::Footer("L:Page","R:Field/Click");
    return;
  }
  if (page_ == 4 || page_ == 5) {
    const unsigned voice = unsigned(page_ - 4);
    const auto& state = audio_.Voice(voice);
    char line[24];
    snprintf(line,sizeof(line),"Audio %c -> Main %c",voice ? 'R' : 'L',voice ? 'R' : 'L');
    gfxPrint(0,1,line); graphics.drawLine(0,11,127,11);
    const unsigned first = audio_cursor_[voice] > 3
        ? audio_cursor_[voice] - 3 : 0;
    for (unsigned row = 0; row < 4; ++row) {
      const unsigned field = first + row;
      const char marker = audio_cursor_[voice] == field ? '>' : ' ';
      if (field == 0)
        snprintf(line,sizeof(line),"%c Voice: %s",marker,
                 state.enabled ? "On" : "Off");
      else if (field == 1)
        snprintf(line,sizeof(line),"%c Wave: %s",marker,
                 txi2c2midi_ui::wave_names[state.waveform]);
      else if (field == 2)
        snprintf(line,sizeof(line),"%c Mode: %s",marker,
                 state.PitchMode() == txi2c2midi::AudioPitchMode::Note
                     ? "Note" : "Frequency");
      else if (field == 3 &&
               state.PitchMode() == txi2c2midi::AudioPitchMode::Frequency)
        snprintf(line,sizeof(line),"%c Freq: %lu.%03lu",marker,
                 static_cast<unsigned long>(state.frequency_millihz / 1000),
                 static_cast<unsigned long>(state.frequency_millihz % 1000));
      else if (field == 3) {
        const int midi = 60 + state.pitch / 128;
        snprintf(line,sizeof(line),"%c Note: %s%d",marker,
                 txi2c2midi_ui::note_names[midi % 12],midi / 12 - 1);
      } else if (field == 4) {
        if (!state.QuantizerSelection())
          snprintf(line,sizeof(line),"%c Q: Off",marker);
        else if (state.QuantizerSelection() == 1)
          snprintf(line,sizeof(line),"%c Q: Shared(Q%u)",marker,
                   unsigned(shared_quantizer_slot_ + 1));
        else
          snprintf(line,sizeof(line),"%c Q: Local Q%u",marker,
                   unsigned(state.QuantizerSelection() - 1));
      } else
        snprintf(line,sizeof(line),"%c Level: %u%%",marker,
                 unsigned(state.level_percent));
      gfxPrint(0,14 + row * 10,line);
    }
    graphics.drawLine(0,54,127,54);
    if (audio_editing_) txi2c2midi_ui::Footer(nullptr,"R:Adjust/Click");
    else txi2c2midi_ui::Footer("L:Page","R:Field/Click");
    return;
  }
  if (page_ == 1 && channel_ < 0) {
    const unsigned pair = unsigned(-channel_ - 1);
    char line[22];
    gfxPrint(0,1,"TXI virtual knobs");
    graphics.drawLine(0,11,127,11);
    snprintf(line,sizeof(line),"Pair %u-%u%s",
             pair * 2 + 1,
             pair * 2 + 2,
             txi_knob_dirty_ ? " *" : "");
    gfxPrint(0,14,line);
    const unsigned first = pair * 2;
    snprintf(line,sizeof(line),"L K%u: %d",first+1,
             int(telemetry_.Knob(first).value));
    gfxPrint(0,25,line);
    snprintf(line,sizeof(line),"R K%u: %d",first+2,
             int(telemetry_.Knob(first+1).value));
    gfxPrint(0,35,line);
    gfxPrint(0,45,"A B X Y: Select pair");
    graphics.drawLine(0,54,127,54);
    txi2c2midi_ui::Footer("Hold L:Back",nullptr);
    return;
  }
  if (page_ == 2 && txo_settings_open_) {
    const unsigned selected = unsigned(channel_ & 7);
    char line[24];
    snprintf(line,sizeof(line),"TXO output %u%s",selected+1,
             txo_dirty_ ? " *" : "");
    gfxPrint(0,1,line);
    graphics.drawLine(0,11,127,11);
    snprintf(line,sizeof(line),"%c Mode: %s",
             txo_cursor_ == 0 ? '>' : ' ',
             txo_cv_mask_ & (uint8_t(1) << selected) ? "CV" : "Gate");
    gfxPrint(0,16,line);
    char quantizer[18];
    txi2c2midi_ui::QuantizerText(
        quantizer,sizeof(quantizer),TxoPitchState().Selection(selected),
        shared_quantizer_slot_);
    snprintf(line,sizeof(line),"%c Q: %s",
             txo_cursor_ == 1 ? '>' : ' ',quantizer);
    gfxPrint(0,28,line);
    gfxPrint(0,40,"Q applies to CV only");
    graphics.drawLine(0,54,127,54);
    if (txo_editing_) txi2c2midi_ui::Footer(nullptr,"R:Adjust/Click");
    else txi2c2midi_ui::Footer("L:Back","R:Field/Click");
    return;
  }
  if (page_ == 3 && midi_settings_open_) {
    const unsigned selected = unsigned(channel_ - 1);
    char line[24];
    snprintf(line,sizeof(line),"MIDI channel %u%s",selected+1,
             midi_dirty_ ? " *" : "");
    gfxPrint(0,1,line);
    graphics.drawLine(0,11,127,11);
    char quantizer[18];
    txi2c2midi_ui::QuantizerText(
        quantizer,sizeof(quantizer),MidiPitchState().Selection(selected),
        shared_quantizer_slot_);
    snprintf(line,sizeof(line),"> Q: %s",quantizer);
    gfxPrint(0,18,line);
    if (midi_rejected_)
      gfxPrint(0,31,"Microtonal rejected");
    else if (!MidiPitchState().Available(selected))
      gfxPrint(0,31,"Microtonal unavailable");
    else
      gfxPrint(0,31,"Applies to future notes");
    gfxPrint(0,43,"NOTE.O uses sent pitch");
    graphics.drawLine(0,54,127,54);
    if (midi_editing_)
      txi2c2midi_ui::Footer(nullptr,"R:Adjust/Click");
    else
      txi2c2midi_ui::Footer("L:Back","R:Field/Click");
    return;
  }
  if (page_) {
    gfxPrint(0,1,page_ == 1 ? "TXI monitor" : page_ == 2 ? "TXO monitor" : "MIDI monitor");
    graphics.drawLine(0,11,127,11);
    if (page_ == 1) {
      const unsigned selected = unsigned(channel_ & 7);
      const auto input = telemetry_.Input(selected);
      const auto knob = telemetry_.Knob(selected);
      const auto readback = telemetry_.Readback(selected);
      char line[22];
      snprintf(line,sizeof(line),"CV input %u",selected+1); gfxPrint(0,14,line);
      if (input.available) {
        snprintf(line,sizeof(line),"Raw:%ld P:%ld",long(input.raw),long(input.pitch));
        gfxPrint(0,24,line);
        snprintf(line,sizeof(line),"Scaled:%+ldmV",
                 long(OC::IO::pitch_to_millivolts(input.pitch)));
        gfxPrint(0,34,line);
      } else gfxPrint(0,24,"Input unavailable");
      if (knob.available && readback.available)
        snprintf(line,sizeof(line),"K:%d ii:%d",int(knob.value),int(readback.value));
      else if (readback.available)
        snprintf(line,sizeof(line),"K:n/a ii raw:%d",int(readback.value));
      else snprintf(line,sizeof(line),"K:n/a ii:n/a");
      gfxPrint(0,44,line);
    } else if (page_ == 2) {
      const unsigned selected = unsigned(channel_ & 7);
      const auto output = telemetry_.Output(selected);
      char line[22];
      snprintf(line,sizeof(line),"Output %u: %s",selected+1,
               txo_cv_mask_ & (uint8_t(1) << selected) ? "CV" : "Gate");
      gfxPrint(0,14,line);
      if (output.mode == decltype(telemetry_)::TxoMode::Cv) {
        snprintf(line,sizeof(line),"CV cmd:%ld",long(output.commanded)); gfxPrint(0,24,line);
        snprintf(line,sizeof(line),"Target:%+dmV",int(output.millivolts)); gfxPrint(0,34,line);
      } else if (output.mode == decltype(telemetry_)::TxoMode::Gate) {
        snprintf(line,sizeof(line),"Gate cmd:%s",output.gate_high ? "HIGH" : "low"); gfxPrint(0,24,line);
        snprintf(line,sizeof(line),"Timing:%ums",unsigned(output.duration_ms)); gfxPrint(0,34,line);
      } else {
        gfxPrint(0,24,"Command unavailable");
        gfxPrint(0,34,"Timing unavailable");
      }
      char quantizer[18];
      txi2c2midi_ui::QuantizerText(
          quantizer,sizeof(quantizer),TxoPitchState().Selection(selected),
          shared_quantizer_slot_);
      snprintf(line,sizeof(line),"Q: %s",quantizer);
      gfxPrint(0,44,line);
    } else {
      const auto& monitor = notes_.monitor;
      const int ch = channel_ ? channel_-1 : monitor.Last();
      char line[22];
      if (ch < 0) snprintf(line,sizeof(line),"Auto: waiting");
      else snprintf(line,sizeof(line),"%s Ch:%d",channel_ ? "Manual" : "Auto",ch+1);
      gfxPrint(0,14,line);
      if (ch >= 0 && monitor.Get(ch).seen) {
        const auto& c = monitor.Get(ch);
        snprintf(line,sizeof(line),"TX %s %u V:%u",c.on ? "On" : "Off",unsigned(c.note),unsigned(c.velocity));
        gfxPrint(0,24,line);
        snprintf(line,sizeof(line),"Active:%u USB device",unsigned(c.active));
        gfxPrint(0,34,line);
      } else gfxPrint(0,24,"No note events");
      gfxPrint(0,44,"RX unavailable");
    }
    graphics.drawLine(0,54,127,54);
    txi2c2midi_ui::Footer("L:Page",page_ == 1 ? "R:Input/Click" :
           page_ == 2 ? "R:Output/Click" :
           channel_ ? "R:Channel/Click" : "R:Channel");
    return;
  }
  gfxPrint(0, 1, txi2c2midi_ui::label_4);
  graphics.drawLine(0, 11, 127, 11);
  const auto& mode = OC::global_settings.tx_mode;
  gfxPrint(0, 15, txi2c2midi_ui::label_5);
  gfxPrint(mode.faulted() ? txi2c2midi_ui::label_6 :
      mode.active() == txi2c2midi::Mode::Single ? txi2c2midi_ui::label_7 : txi2c2midi_ui::label_8);
  const auto& address = OC::global_settings.ii_address;
  gfxPrint(0, 25, txi2c2midi_ui::label_9);
  if (address.faulted()) gfxPrint(txi2c2midi_ui::label_10);
  else if (!address.enabled()) gfxPrint(txi2c2midi_ui::label_11);
  else { char value[5]; snprintf(value,sizeof(value),"0x%02X",unsigned(address.active())); gfxPrint(value); }
  gfxPrint(0, 35, OC::global_settings.tx_address_ignored ?
      "Legacy addr ignored" : "Shared global address");
  gfxPrint(0, 45, mode.active() == txi2c2midi::Mode::Legacy ?
      "Legacy unavailable" : address.reboot_required() || mode.reboot_required() ?
      "Reboot required" : "Hold Z: Globals");
  graphics.drawLine(0,54,127,54);
  txi2c2midi_ui::Footer("L:Page",nullptr);
}
FLASHMEM void AppTXI2C2MIDI::DrawScreensaver() const { DrawMenu(); }
FLASHMEM void AppTXI2C2MIDI::HandleEncoderEvent(const UI::Event& event) {
  if (!event.value || (event.control != OC::CONTROL_ENCODER_R && event.control != OC::CONTROL_ENCODER_L)) return;
  if (page_ == 1 && channel_ < 0) {
    const unsigned channel = unsigned(-channel_ - 1) * 2 +
        (event.control == OC::CONTROL_ENCODER_R ? 1 : 0);
    const auto knob = telemetry_.Knob(channel);
    int32_t next = int32_t(knob.value) + int32_t(event.value) * 256;
    if (next < 0) next = 0;
    if (next > 16383) next = 16383;
    if (knob.value != next) {
      telemetry_.SetKnob(channel,int16_t(next));
      txi_knob_dirty_ = true;
      PublishTxiKnobs();
    }
    return;
  }
  if (page_ == 2 && txo_settings_open_) {
    if (event.control == OC::CONTROL_ENCODER_L) return;
    const unsigned channel = unsigned(channel_ & 7);
    if (!txo_editing_) {
      txo_cursor_ = uint8_t((txo_cursor_ +
          (event.value > 0 ? 1 : 1)) % 2);
    } else if (txo_cursor_ == 0) {
      txo_cv_mask_ ^= uint8_t(1) << channel;
      txi2c2midi::MidiFollower::SetTxoMode(
          channel,(txo_cv_mask_ & (uint8_t(1) << channel)) != 0,
          telemetry_,millis());
      txo_dirty_ = true;
    } else {
      int selection = int(TxoPitchState().Selection(channel)) +
          (event.value > 0 ? 1 : -1);
      if (selection < 0)
        selection = txi2c2midi::TxoPitch<
            phz::ii::InterruptLock>::kMaxSelection;
      if (selection > txi2c2midi::TxoPitch<
              phz::ii::InterruptLock>::kMaxSelection)
        selection = 0;
      TxoPitchState().SetSelection(channel,uint8_t(selection));
      ConfigureTxoPitch(channel);
      txo_dirty_ = true;
    }
    return;
  }
  if (page_ == 3 && midi_settings_open_) {
    if (event.control == OC::CONTROL_ENCODER_L || !midi_editing_) return;
    const unsigned channel = unsigned(channel_ - 1);
    int selection = int(MidiPitchState().Selection(channel)) +
        (event.value > 0 ? 1 : -1);
    if (selection < 0) selection = txi2c2midi::MidiPitch::kMaxSelection;
    if (selection > txi2c2midi::MidiPitch::kMaxSelection) selection = 0;
    const auto result = SetMidiQuantizer(channel,uint8_t(selection));
    midi_rejected_ = result ==
        txi2c2midi::MidiSelectionResult::Microtonal;
    if (result == txi2c2midi::MidiSelectionResult::Applied)
      midi_dirty_ = true;
    return;
  }
  if (event.control == OC::CONTROL_ENCODER_L) {
    if (audio_editing_ || quantizer_editing_) return;
    page_ = (page_ + (event.value > 0 ? 1 : 6)) % 7;
    return;
  }
  if (page_ == 6) {
    if (!quantizer_editing_) {
      quantizer_cursor_ = uint8_t((quantizer_cursor_ +
          (event.value > 0 ? 1 : 1)) % 2);
    } else if (quantizer_cursor_ == 0) {
      shared_quantizer_slot_ = uint8_t((shared_quantizer_slot_ +
          (event.value > 0 ? 1 : 7)) % 8);
      ApplyAudioVoice(0); ApplyAudioVoice(1);
      ConfigureAllTxoPitch();
      ConfigureAllMidiPitch();
    }
    return;
  }
  if (page_ == 4 || page_ == 5) {
    const unsigned voice = unsigned(page_ - 4);
    if (!audio_editing_) {
      audio_cursor_[voice] = uint8_t((audio_cursor_[voice] +
          (event.value > 0 ? 1 : 5)) % 6);
      return;
    }
    auto& state = audio_.Voice(voice);
    const int delta = event.value;
    switch (audio_cursor_[voice]) {
      case 1: {
        int next = int(state.waveform) + (delta > 0 ? 1 : -1);
        if (next < 0) next = txi2c2midi::AudioState::kWaveformCount - 1;
        if (next >= txi2c2midi::AudioState::kWaveformCount) next = 0;
        audio_.SetWaveform(voice,uint8_t(next));
        break;
      }
      case 2:
        audio_.SetPitchMode(voice,
            state.PitchMode() == txi2c2midi::AudioPitchMode::Frequency
                ? txi2c2midi::AudioPitchMode::Note
                : txi2c2midi::AudioPitchMode::Frequency);
        break;
      case 3: {
        if (state.PitchMode() == txi2c2midi::AudioPitchMode::Note) {
          int next = int(state.pitch) + delta * 128;
          if (next < txi2c2midi::AudioState::kMinPitch)
            next = txi2c2midi::AudioState::kMinPitch;
          if (next > txi2c2midi::AudioState::kMaxPitch)
            next = txi2c2midi::AudioState::kMaxPitch;
          audio_.SetPitch(voice,next);
          break;
        }
        const uint32_t step = state.frequency_millihz < 10000 ? 100 :
                              state.frequency_millihz < 1000000 ? 1000 : 10000;
        int64_t next = int64_t(state.frequency_millihz) + int64_t(delta) * step;
        if (next < txi2c2midi::AudioState::kMinFrequencyMillihz)
          next = txi2c2midi::AudioState::kMinFrequencyMillihz;
        if (next > txi2c2midi::AudioState::kMaxFrequencyMillihz)
          next = txi2c2midi::AudioState::kMaxFrequencyMillihz;
        audio_.SetFrequency(voice,uint32_t(next));
        break;
      }
      case 4: {
        int next = int(state.QuantizerSelection()) + (delta > 0 ? 1 : -1);
        if (next < 0) next = 9;
        if (next > 9) next = 0;
        audio_.SetQuantizer(voice,unsigned(next));
        break;
      }
      case 5: {
        int next = int(state.level_percent) + delta;
        audio_.SetLevel(voice,uint8_t(next < 0 ? 0 : next > 100 ? 100 : next));
        OC::AudioIO::SetTxVoiceLevel(voice,state.enabled,state.level_percent);
        return;
      }
    }
    ApplyAudioVoice(voice);
    return;
  }
  if (page_) {
    if (event.control == OC::CONTROL_ENCODER_R) {
      if (page_ == 3) channel_ = (channel_ + (event.value > 0 ? 1 : 16)) % 17;
      else channel_ = (channel_ + (event.value > 0 ? 1 : 7)) % 8;
    }
    return;
  }
}
FLASHMEM void AppTXI2C2MIDI::HandleButtonEvent(const UI::Event& event) {
  if (page_ == 3 && midi_settings_open_ &&
      event.type == UI::EVENT_BUTTON_PRESS &&
      event.control == OC::CONTROL_BUTTON_L) {
    SaveMidiSettings();
    midi_settings_open_ = false;
    midi_editing_ = false;
    midi_rejected_ = false;
    return;
  }
  if (page_ == 2 && txo_settings_open_ &&
      event.type == UI::EVENT_BUTTON_PRESS &&
      event.control == OC::CONTROL_BUTTON_L) {
    SaveTxoSettings();
    txo_settings_open_ = false;
    txo_editing_ = false;
    return;
  }
  if (page_ == 1 && channel_ < 0) {
    if (event.type == UI::EVENT_BUTTON_LONG_PRESS &&
        event.control == OC::CONTROL_BUTTON_L) {
      channel_ = (-channel_ - 1) * 2;
      return;
    }
    if (event.type == UI::EVENT_BUTTON_PRESS) {
      if (event.control == OC::CONTROL_BUTTON_A) channel_ = -1;
      else if (event.control == OC::CONTROL_BUTTON_B) channel_ = -2;
      else if (event.control == OC::CONTROL_BUTTON_X) channel_ = -3;
      else if (event.control == OC::CONTROL_BUTTON_Y) channel_ = -4;
    }
    return;
  }
  if (page_ == 1 && event.type == UI::EVENT_BUTTON_PRESS &&
      event.control == OC::CONTROL_BUTTON_R) {
    channel_ = -1 - (channel_ & 7) / 2;
    return;
  }
  if (page_ == 3 && !midi_settings_open_ && channel_ > 0 &&
      event.type == UI::EVENT_BUTTON_PRESS &&
      event.control == OC::CONTROL_BUTTON_R) {
    midi_settings_open_ = true;
    midi_editing_ = false;
    midi_rejected_ = false;
    return;
  }
  if (event.type != UI::EVENT_BUTTON_PRESS || event.control != OC::CONTROL_BUTTON_R) return;
  if (page_ == 3 && midi_settings_open_) {
    midi_editing_ = !midi_editing_;
    if (!midi_editing_) SaveMidiSettings();
    return;
  }
  if (page_ == 6) {
    if (quantizer_cursor_ == 0) {
      quantizer_editing_ = !quantizer_editing_;
    } else {
      OC::QuantizerSettings imported[OC::QuantizerBank::kCount];
      bool valid = true;
      for (unsigned slot = 0; slot < OC::QuantizerBank::kCount; ++slot)
        valid = valid && HS::ReadQuantizerSlot(slot, imported[slot]);
      auto imported_slots = txi2c2midi::quantizer_slots;
      if (valid) {
        for (unsigned slot = 0; slot < OC::QuantizerBank::kCount; ++slot)
          valid = valid && imported_slots.Replace(
              slot, imported[slot], OC::Scales::NUM_SCALES);
      }
      auto imported_scales = txi2c2midi::user_scale_snapshot;
      if (valid) {
        valid = imported_scales.Capture(
            OC::user_scales, OC::Scales::SCALE_USER_COUNT);
      }
      if (valid) {
        txi2c2midi::quantizer_slots = imported_slots;
        txi2c2midi::user_scale_snapshot = imported_scales;
        quantizer_imported_ = true;
        quantizer_dirty_ = true;
        ApplyAudioVoice(0); ApplyAudioVoice(1);
        ConfigureAllTxoPitch();
        ConfigureAllMidiPitch();
      }
    }
    return;
  }
  if (page_ == 4 || page_ == 5) {
    const unsigned voice = unsigned(page_ - 4);
    auto& state = audio_.Voice(voice);
    if (audio_cursor_[voice] == 0) {
      state.enabled = !state.enabled;
      ApplyAudioVoice(voice,state.enabled);
    } else audio_editing_ = !audio_editing_;
    return;
  }
  if (page_ != 2) return;
  if (!txo_settings_open_) {
    txo_settings_open_ = true;
    txo_cursor_ = 0;
    txo_editing_ = false;
  } else {
    txo_editing_ = !txo_editing_;
    if (!txo_editing_) SaveTxoSettings();
  }
}
