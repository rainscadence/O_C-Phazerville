// Copyright (c) 2018, Jason Justian
// Copyright (c) 2024, Nicholas J. Michalek
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

#pragma once
#include "ii/Lifecycle.h"
#include "ii/InterruptLock.h"
#include "ii/DirectionalPreset.h"
#include "ii/DirectionChangePrompt.h"
#include "ii/MappingSelection.h"
#include "ii/RecallRoutes.h"
#include "ii/HostIdentity.h"
#include "routing/OutputPreset.h"

// per bank file
static constexpr int QUAD_PRESET_COUNT = 32;
static constexpr int PRESET_FILE_REVISION = 1;

using namespace HS;

void QuadrantSysExHandler();

OC_APP_CLASS(AppQuadrants, TWOCCS("QS"), "Quadrants", "4x Applets"),
  public HSApplication {
public:
  OC_APP_INTERFACE_DECLARE(AppQuadrants, 0);

    HS::HemisphereApplet* IiTarget(uint16_t word, uint64_t& type) {
      phz::ii::Slot slot{};
      if (!phz::ii::DecodeSlot(word, slot)) return nullptr;
      if (slot.bank != phz::ii::SlotBank::Standard)
        return audio_app.PeekIiTarget(slot, type);
      const int index = active_applet_index[slot.index];
      if (index < 0 || index >= HS::HEMISPHERE_AVAILABLE_APPLETS) return nullptr;
      type = HS::appletIds[index];
      return active_applet[slot.index];
    }
    FLASHMEM phz::ii::HostActionResult ApplyIiHostAction(const phz::ii::HostAction& action) override;
    OC::QuantizerCommandResult ApplyQuantizerCommand(
        const OC::QuantizerCommand& command) override {
      class Target final : public OC::QuantizerCommandTarget {
       public:
        bool Read(unsigned slot,
                  OC::QuantizerSettings& settings) const override {
          return HS::ReadQuantizerSlot(slot, settings);
        }
        bool Replace(unsigned slot,
                     const OC::QuantizerSettings& settings) override {
          return HS::ReplaceQuantizerSlot(slot, settings);
        }
        void Changed(unsigned) override {}
      } target;
      const auto result = OC::ApplyQuantizerCommand(
          target, command, OC::Scales::NUM_SCALES, OC::Scales::SCALE_SEMI);
      if (result == OC::QuantizerCommandResult::Applied)
        quantizer_dirty_ = true;
      return result;
    }
    phz::ii::LoadedSetupIdentity ii_loaded_setup;
    bool SnapshotIiIdentity(phz::ii::HostIdentity& out) override {
      for(int slot=0;slot<4;++slot){
        const int index=active_applet_index[slot];
        if(!active_applet[slot] || index<0 || index>=HS::HEMISPHERE_AVAILABLE_APPLETS)return false;
        out.applets[slot]=HS::appletIds[index];
      }
      ii_loaded_setup.CopyTo(out);return true;
    }
    bool IiReady() override {
      uint64_t type;
      for (uint16_t slot = 1; slot <= 4; ++slot)
        if (!IiTarget(slot, type)) return false;
      return audio_app.IiReady();
    }
    uint8_t IiFamily() const override { return PC_FAMILY_QUADRANTS; }
    uint16_t IiQuantizerScaleCount() const override {
      return OC::Scales::NUM_SCALES;
    }
    bool DescribeIi(const phz::ii::Write& write, phz::ii::Descriptor& out) override {
      uint64_t type;
      auto* target = IiTarget(write.slot, type);
      if (!target || !target->DescribeIiParameter(write.parameter, out)) return false;
      out.instance = target;
      out.type = type;
      out.slot = write.slot;
      out.parameter = write.parameter;
      return true;
    }
    bool ApplyIi(const phz::ii::Prepared& update) override {
      uint64_t type;
      const auto& d = update.descriptor;
      auto* target = IiTarget(d.slot, type);
      return target && target == d.instance && type == d.type &&
        target->ApplyIiParameter(d.parameter, update.value, d.dependencies);
    }

    void Start() {
    phz::ii::Transition transition(true);
        ii_loaded_setup.Reset();
        audio_app.Init();

        //bank_filename[16] = "BANK_000.DAT";
        bank_num = 0;
        preset_id = -1;
        queued_preset = -1;
        preset_cursor = 0;
        quantizer_dirty_ = false;

        view_slot[0] = 0;
        view_slot[1] = 0;
        config_cursor = LOAD_PRESET;

        select_mode = -1;
        zoom_slot = HEM_SIDE(0);
        zoom_cursor = 0;
        click_tick = 0;
        first_click = -1;

        mask = 0;
        last_mask = 0;

        Randomizer();
        //SetApplet(HEM_SIDE(0), HS::get_applet_index_by_id(18)); // DualTM
        //SetApplet(HEM_SIDE(1), HS::get_applet_index_by_id(15)); // EuclidX
        //SetApplet(HEM_SIDE(2), HS::get_applet_index_by_id(68)); // DivSeq
        //SetApplet(HEM_SIDE(3), HS::get_applet_index_by_id(71)); // Pigeons
    if (IiReady()) transition.Complete();
  }

    void Resume() {
        SetBank(bank_num);

        if (preset_id < 0)
          LoadFromPreset(0);

        for (auto& env : HS::env_) env.reset();
    }
    void Suspend() {
        HS::frame.MIDIState.StopAutoOutput();
        if (preset_id >= 0) {
            if (HS::auto_save_enabled)
              StoreToPreset(preset_id);
            // TODO
            //OnSendSysEx();
        }
    }
    FLASHMEM __attribute__((noinline)) void SetBank(uint8_t id);
    using IiRecallBank = phz::ii::RecallBank<128,16>;
    using IiRecallProfile = IiRecallBank::Profile;
    std::unique_ptr<IiRecallBank> ii_recall_bank;
    phz::ii::RecallOwnership<> ii_recall_owners;
    IiRecallProfile ii_recall_baseline[4];
    bool ii_recall_loaded=false, ii_recall_sd=false, ii_recall_restoring=false;
    uint32_t ii_recall_changed=0, ii_recall_polled=0, ii_recall_retry=0;
    FLASHMEM void RecallLoad(uint8_t bank);
    FLASHMEM bool RecallSave();
    FLASHMEM void RecallTick();
    FLASHMEM void RecallCapture(int slot, bool confirmed = false);
    FLASHMEM void RecallBaseline();
    FLASHMEM bool RecallSnapshot(int slot, IiRecallProfile& out);
    FLASHMEM void RecallSwitch(int slot, HemisphereApplet* old);
    FLASHMEM HS::CVInputMap* RecallInput(int slot, HemisphereApplet* applet, uint16_t field);

    // lower 11 bits of PhzConfig KEY
    // Audio applet data keys also use 11 bits, with the upper 3 being non-zero...
    // - watch out for collisions, especially on Preset A (index 0)
    enum PresetDataKeys : uint16_t {
        APPLET_METADATA_KEY = 0, // applet ids
        CLOCK_DATA_KEY = 1,
        GLOBALS_KEY = 2,
        ANCIENT_TRIGMAP_KEY = 3, // from v1.9
        ANCIENT_CVMAP_KEY = 4, // from v1.9
        OUTSKIP_KEY = 5,
        OUTSLEW_KEY = 6,
        OUTATTEN_KEY = 7,
        INSKIP_KEY = 8,

        APPLET_L1_DATA_KEY = 10,
        APPLET_R1_DATA_KEY = 11,
        APPLET_L2_DATA_KEY = 12,
        APPLET_R2_DATA_KEY = 13,

        CVMAP_KEY = 20, // 16-bit CVInputMap, multiple pages

        OLD_TRIGMAP_KEY = 30, // 2 blobs, 16-bit DigitalInputMap
        TRIGMAP_KEY = 32, // 32-bit DigitalInputMap, 4 pages

        // 100s = Globals
        FILTERMASK1_KEY = 100,
        FILTERMASK2_KEY = 101,

        MIDI_GLOBALS_KEY  = 110,
        PRESET_JUMP_KEY = 111,

        MIDI_MAPS_KEY   = 150, // + 0..32

        // 200s = Quantizers
        Q_ENGINE_KEY    = 200, // + slot number (200 - 207)

        // 256 = used by Audio Applets

        // 300-428 = Sequences (aka Patterns)
        SEQUENCES_KEY   = 300, // + blob index

        // More ranges used by Audio Applet data:
        // 512-522
        // 768-778
        // 1024+
        // ... check AudioAppletSubapp to be sure!

        VERSION_KEY = 0xFFFF // 65535
    };

    void DeletePreset(int id) {
        uint16_t preset_key = id << 11;
        // non-global values are all 0-99 in the enum
        for (int i = 0; i < 100; ++i) {
          PhzConfig::deleteKey(preset_key | i);
        }
        // TODO:
        //audio_app.deletePresetData(id);
    }

    void StoreToPreset(int id);
    void store_to_preset(int id);

    void LoadFromPreset(int id);
    phz::ii::HostActionResult load_from_preset(int id);
    FLASHMEM __attribute__((noinline)) void LoadGlobals() {
        // applet filtering
        PhzConfig::getValue(FILTERMASK1_KEY, HS::hidden_applets[0]);
        PhzConfig::getValue(FILTERMASK2_KEY, HS::hidden_applets[1]);

        uint64_t data = 0;
        if (PhzConfig::getValue(MIDI_GLOBALS_KEY, data)) {
          UnpackPackables(data,
              HS::frame.MIDIState.pc_channel,
              HS::frame.MIDIState.bend_range,
              midi_thru_disable,
              midi_clkrx_disable,
              midi_clktx_disable,
              midi_msgrx_disable,
              midi_msgtx_disable
          );
        }
        if (midi_thru_disable & mMaskSerial) MIDI1.turnThruOff();
        else MIDI1.turnThruOn();

        if (PhzConfig::getValue(PRESET_JUMP_KEY, data))
          UnpackPackables(data, jump_trig_);

        // Global quantizer settings
        for (size_t qslot = 0; qslot < QUANT_CHANNEL_COUNT; ++qslot) {
          if (!PhzConfig::getValue(Q_ENGINE_KEY + qslot, data))
              break;
          auto &q = q_engine[qslot];
          uint8_t range_low = 0;
          uint8_t range_high = 0;
          UnpackPackables(data,
              q.scale,
              q.octave,
              q.root_note,
              q.mask,
              range_low,
              range_high);
          if (!range_low || !range_high ||
              !q.range.Set(int(range_low) - 9, int(range_high) - 9))
            q.range.Disable();
          q.Reconfig();
        }

        // Global MIDI Maps
        for (size_t midx = 0; midx < MIDIMAP_MAX; ++midx) {
          if (!PhzConfig::getValue(MIDI_MAPS_KEY + midx, data))
              break;
          UnpackPackables(data, frame.MIDIState.mapping[midx]);
        }
        frame.MIDIState.UpdateMidiChannelFilter();
        frame.MIDIState.UpdateMaxPolyphony();

        // User Patterns aka Sequences
        for (size_t i = 0; i < OC::Patterns::PATTERN_USER_COUNT; ++i) {
          for (size_t step = 0; step < ARRAY_SIZE(OC::Pattern::notes); ++step) {
            if ((step & 0x3) == 0x0) {
              data = 0;
              if (!PhzConfig::getValue(SEQUENCES_KEY + ((i << 2) | (step >> 2)), data))
                break;
            }
            OC::user_patterns[i].notes[step] = Unpack(data, PackLocation{(step & 0x3)*16, 16});
          }
        }
    }
    void ProcessQueue() {
      if (queued_preset >= 0) {
        LoadFromPreset(queued_preset);
        queued_preset = -1;
      }
    }
    void QueuePresetLoad(int id) {
      if (HS::clock_m.IsRunning()) {
        queued_preset = id;
        HS::clock_m.BeatSync( [this](){ ProcessQueue(); } );
      }
      else
        LoadFromPreset(id);
    }
    void JumpToNextPreset() {
      int next_id = preset_id + 1;
      while (!isValidPreset(next_id) && next_id != preset_id) {
        ++next_id %= QUAD_PRESET_COUNT;
      }
      if (next_id != preset_id)
        QueuePresetLoad(next_id);
    }

    // does not modify the preset, only the current state
    FLASHMEM __attribute__((noinline)) void SetApplet(HEM_SIDE hemisphere, int index);
    void ChangeApplet(HEM_SIDE h, int dir) {
        int index = HS::get_next_applet_index(next_applet_index[h], dir);
        next_applet_index[h] = index;
    }

    void SendMIDIThru(const MIDIMessage &msg, uint8_t exclude_mask) {
      exclude_mask |= midi_thru_disable;
      if (~exclude_mask & mMaskUSBDev) {
        usbMIDI.send(msg.message, msg.data1, msg.data2, msg.channel, 0);
      }
      if (~exclude_mask & mMaskUSBHost) {
        usbHostMIDI[0].send(msg.message, msg.data1, msg.data2, msg.channel);
      }
      if (~exclude_mask & mMaskUSBHost2) {
        usbHostMIDI[1].send(msg.message, msg.data1, msg.data2, msg.channel);
      }
      if (~exclude_mask & mMaskSerial) {
        MIDI1.send((midi::MidiType)msg.message, msg.data1, msg.data2, msg.channel);
      }
    }

    template <typename T1>
    void ProcessMIDI(T1 &device) {
        HS::IOFrame &f = HS::frame;
        int load_slot = -1;

        uint8_t thrumask = 0;
        // who needs types? we have pointers!
        if ((void*)&device == (void*)&usbMIDI) thrumask = mMaskUSBDev;
        if ((void*)&device == (void*)&usbHostMIDI[0]) thrumask = mMaskUSBHost;
        if ((void*)&device == (void*)&usbHostMIDI[1]) thrumask = mMaskUSBHost2;
        if ((void*)&device == (void*)&MIDI1) thrumask = mMaskSerial;
        // Serial-to-Serial Thru still happens in the library, if enabled

        const bool clkrx = ~midi_clkrx_disable & thrumask;
        const bool msgrx = ~midi_msgrx_disable & thrumask;

        while (timeout < 60 && device.read()) {
          const MIDIMessage msg = {
            device.getChannel(),
            device.getType(),
            device.getData1(),
            device.getData2()
          };

          switch (msg.message) {
            case midi::SystemExclusive:
              QuadrantSysExHandler();
              continue;

            case midi::ProgramChange:
              if (msg.channel == f.MIDIState.pc_channel || f.MIDIState.pc_channel == f.MIDIState.PC_OMNI) {
                load_slot = msg.data1;
              }
              break;

            case midi::Clock:
            case midi::Start:
            case midi::Stop:
              if (clkrx) f.MIDIState.ProcessMIDIMsg(msg); // receive it
                break;

            default:
              if (msgrx && (msg.message >> 4) != 0xF) f.MIDIState.ProcessMIDIMsg(msg); // receive it
                break;
          }

          // send it along
          SendMIDIThru(msg, thrumask);
        }
        if (load_slot >= 0 && load_slot < QUAD_PRESET_COUNT) {
            QueuePresetLoad(load_slot);
        }
    }

    void mainloop() {
        timeout = 0;
        // top-level MIDI-to-CV handling - alters frame outputs
        ProcessMIDI(usbMIDI);
        ProcessMIDI(usbHostMIDI[0]);
        ProcessMIDI(usbHostMIDI[1]);
        ProcessMIDI(MIDI1);
    }
    void Controller() {
        // Clock Setup applet handles internal clock duties
        ClockSetup_instance.Controller();
        // ^ this will process the queue and load presets

        // this might be triggered by the internal clock...
        if (jump_trig_.Clock() && !HS::clock_m.auto_reset) {
          JumpToNextPreset();
          // ^ this will sometimes queue a preset load

          // The paradox is we need to process the clock first, in case jump_trig needs it,
          // but then jump_trig will queue another preset load,
          // so we have to process the queue again.
          if (jump_trig_.is_clock()) ProcessQueue();
        }

        // execute Applets
        for (int h = 0; h < APPLET_SLOTS; h++)
        {
            if (HS::clock_m.auto_reset)
                active_applet[h]->Reset();

            active_applet[h]->Controller();
        }
        audio_app.Controller();
        HemisphereApplet::ProcessCursors();
        HS::clock_m.auto_reset = false;
    }

    FLASHMEM __attribute__((noinline)) void DrawFullScreen() const;

    void DrawOverview() const {
      active_applet[0]->gfxHeader(0);
      active_applet[1]->gfxHeader(0);
      active_applet[2]->gfxHeader(54);
      active_applet[3]->gfxHeader(54);

      gfxDottedLine(63, 0, 63, 63); // vert
      gfxDottedLine(0, 32, 127, 32); // horiz

      ForAllChannels(applet) {
        ForEachChannel(ch) {
            int length;
            int max_length = 62;
            int in_bar_y = 13 + (applet>>1)*22 + (ch * 10);
            int out_bar_y = 17 + (applet>>1)*22 + (ch * 10);

            // positive values extend bars from left side of screen to the right
            // negative values go from right side to left
            length = ProportionCV(abs(DetentedIn(applet*2 + ch)), max_length);
            if (DetentedIn(applet*2 + ch) < 0)
                active_applet[applet]->gfxFrame(max_length - length, in_bar_y, length, 3);
            else
                active_applet[applet]->gfxFrame(0, in_bar_y, length, 3);

            length = ProportionCV(abs(ViewOut(applet*2 + ch)), max_length);
            if (ViewOut(applet*2 + ch) < 0)
                active_applet[applet]->gfxRect(max_length - length, out_bar_y, length, 3);
            else
                active_applet[applet]->gfxRect(0, out_bar_y, length, 3);
        }
      }
    }

    FLASHMEM __attribute__((noinline)) void View() const;

    // always act-on-press for encoder
    FLASHMEM __attribute__((noinline)) void DelegateEncoderPush(const UI::Event &event);

    const HEM_SIDE ButtonToSlot(const UI::Event &event) {
      if (event.control == OC::CONTROL_BUTTON_X)
        return LEFT2_HEMISPHERE;
      if (event.control == OC::CONTROL_BUTTON_B)
        return RIGHT_HEMISPHERE;
      if (event.control == OC::CONTROL_BUTTON_Y)
        return RIGHT2_HEMISPHERE;

      //if (event.control == OC::CONTROL_BUTTON_A)
      return LEFT_HEMISPHERE; // default
    }

    // returns true if combo detected and button release should be ignored
    bool CheckButtonCombos(const UI::Event &event) {
        HEM_SIDE slot = ButtonToSlot(event);

        // hold Left Enc + (A or B) to toggle view
        if (CheckButtonCombo(OC::CONTROL_BUTTON_L | OC::CONTROL_BUTTON_A) ||
            CheckButtonCombo(OC::CONTROL_BUTTON_L | OC::CONTROL_BUTTON_B)) {
            SwapViewSlot(slot);
            return true;
        }

        // dual press A+B for Clock Setup
        if (CheckButtonCombo(OC::CONTROL_BUTTON_A | OC::CONTROL_BUTTON_B)) {
            clock_overlay = true;
            return true;
        }
        // dual press X+Y for Audio Setup
        if (CheckButtonCombo(OC::CONTROL_BUTTON_X | OC::CONTROL_BUTTON_Y)) {
            view_state = AUDIO_SETUP;
            return true;
        }
        // dual press A+X for Load Preset
        if (CheckButtonCombo(OC::CONTROL_BUTTON_A | OC::CONTROL_BUTTON_X)) {
            ShowPresetSelector();
            return true;
        }

        // dual press B+Y for Input Mapping
        if (CheckButtonCombo(OC::CONTROL_BUTTON_B | OC::CONTROL_BUTTON_Y)) {
            config_page = INPUT_SETTINGS;
            config_cursor = TRIGMAP1;
            return true;
        }

        // cancel clock view
        if (clock_overlay) {
          clock_overlay = false;
          return true;
        }

        // cancel preset select or config screens
        if (config_page || preset_cursor) {
          preset_cursor = 0;
          config_page = HIDE_CONFIG;
          HS::popup_tick = 0;
          return true;
        }

        // cancel other view layers
        if (view_state != APPLETS && view_state != APPLET_FULLSCREEN && view_state != OVERVIEW) {
          view_state = APPLETS;
          return true;
        }

        // A/B/X/Y buttons becomes aux button while editing a param
        if (SlotIsVisible(slot) && active_applet[slot]->EditMode()) {
          active_applet[slot]->AuxButton();
          return true;
        }

        return false;
    }

    FLASHMEM __attribute__((noinline)) void DelegateEncoderMovement(const UI::Event &event);

    void SetConfigPageFromCursor() {
      if (config_cursor <= RANDOMIZE_PRESET) config_page = LOADSAVE_POPUP;
      else if (config_cursor < TRIGMAP1) config_page = CONFIG_SETTINGS;
      else if (config_cursor < QUANT1) config_page = INPUT_SETTINGS;
      else if (config_cursor < MIDIMAP1) config_page = QUANTIZER_SETTINGS;
      else if (config_cursor < IIMAP1) config_page = MIDI_MAPS_PAGE;
      else if (config_cursor < SHOWHIDELIST) config_page = II_MAPS_PAGE;
      else config_page = LAST_PAGE;
    }
    void ToggleConfigMenu() {
      if (config_page) {
        config_page = HIDE_CONFIG;
      } else {
        SetConfigPageFromCursor();
      }
    }
    void ShowPresetSelector() {
      config_cursor = LOAD_PRESET;
      // Zero closes the chooser; -1 means no preset has been loaded yet.
      preset_cursor = constrain(preset_id + 1, 1, QUAD_PRESET_COUNT);
    }

    // this toggles the view on a given side
    void SwapViewSlot(int h) {
      // h should be 0 or 1 (left or right)
      //h %= 2;

      view_slot[h] = 1 - view_slot[h];
      // also switch fullscreen to corresponding side/slot
      zoom_slot = HEM_SIDE(view_slot[h]*2 + h);
    }

    bool SlotIsVisible(HEM_SIDE h) {
      if (view_state == APPLET_FULLSCREEN)
        return zoom_slot == h;

      return (view_slot[h % 2] == h / 2);
    }
    // this brings a specific applet into view on the appropriate side
    void SwitchToSlot(HEM_SIDE h) {
      view_slot[h % 2] = h / 2;
      zoom_slot = h;
    }

    void ExitFullScreen() {
      output_pending = false;
      view_state = APPLETS;
      isEditing = false;
      ClearEditInputMap();
      select_mode = -1;
    }
    void SetFullScreen(HEM_SIDE hemisphere) {
      SwitchToSlot(hemisphere);
      view_state = APPLET_FULLSCREEN;
      isEditing = false;
      ClearEditInputMap();
      select_mode = -1;
    }
    void ToggleFullScreen() {
      view_state = (view_state == APPLET_FULLSCREEN) ? APPLETS : APPLET_FULLSCREEN;
    }

protected:
    bool CheckButtonCombo(uint16_t combo) {
        return mask == combo && mask != last_mask;
    }

private:
    char bank_filename[16] = "BANK_000.DAT";
    uint8_t bank_num = 0;
    int queued_preset = -1;
    int preset_cursor = 0;
    bool quantizer_dirty_ = false;
    HemisphereApplet *active_applet[4]{}; // Pointers to actual applets
    int active_applet_index[4]{}; // Indexes to applets
                      // Left side: 0,2
                      // Right side: 1,3
    int next_applet_index[4]; // queued from UI thread, handled by Controller
    uint64_t clock_data, global_data, applet_data[4]; // cache of applet data
    bool view_slot[2] = {0, 0}; // Two applets on each side, only one visible at a time
    int config_cursor = LOAD_PRESET;

    int select_mode = -1;
    HEM_SIDE zoom_slot; // Which of the hemispheres (if any) is in fullscreen/help mode
    int zoom_cursor; // 0=select; 1,2=trigmap; 3,4=cvmap; 5,6=outmode
    uint32_t click_tick; // Measure time between clicks for double-click
    int first_click; // The first button pushed of a double-click set, to see if the same one is pressed

    // Button combos can cause multiple triggers if the buttons are pressed
    // close enough together. Each press will have its own event with both
    // button marked in the mask. So, we track mask history to ensure button
    // state has actually changed between events to register a combo.
    uint16_t mask = 0;
    uint16_t last_mask = 0;

    elapsedMicros timeout = 0;

    // State machine
    enum QuadrantsView {
      APPLETS,
      APPLET_FULLSCREEN,
      OVERVIEW,
      //CONFIG_MENU,
      //PRESET_PICKER,
      //CLOCK_SETUP,
      AUDIO_SETUP,
    };
    QuadrantsView view_state = APPLETS;
    bool clock_overlay = false;

    enum QuadrantsConfigPage {
      HIDE_CONFIG,
      LOADSAVE_POPUP,
      CONFIG_SETTINGS,
      INPUT_SETTINGS,
      QUANTIZER_SETTINGS,
      MIDI_MAPS_PAGE,
      II_MAPS_PAGE,
      SHOWHIDE_APPLETS,

      LAST_PAGE = SHOWHIDE_APPLETS
    };
    int config_page = HIDE_CONFIG;

    enum QuadrantsConfigCursor {
        DELETE_PRESET,
        LOAD_PRESET, SAVE_PRESET,
        AUTO_SAVE,
        RANDOMIZE_PRESET, // past this point goes full screen

        // General Settings
        TRIG_LENGTH,
        SCREENSAVER_MODE,
        CURSOR_MODE,
        PRESET_JUMP_TRIG,
        MIDI_BEND_RANGE,
        MIDI_PC_CHANNEL,
        AUTO_MIDI,
        MIDI_POLY_MODE,
        MIDI_THRU_SERIAL,
        MIDI_THRU_USBDEV,
        MIDI_THRU_USBHOST1,
        MIDI_THRU_USBHOST2,
        MIDI_CLKRX_SERIAL,
        MIDI_CLKRX_USBDEV,
        MIDI_CLKRX_USBHOST1,
        MIDI_CLKRX_USBHOST2,
        MIDI_CLKTX_SERIAL,
        MIDI_CLKTX_USBDEV,
        MIDI_CLKTX_USBHOST1,
        MIDI_CLKTX_USBHOST2,
        MIDI_MSGRX_SERIAL,
        MIDI_MSGRX_USBDEV,
        MIDI_MSGRX_USBHOST1,
        MIDI_MSGRX_USBHOST2,
        MIDI_MSGTX_SERIAL,
        MIDI_MSGTX_USBDEV,
        MIDI_MSGTX_USBHOST1,
        MIDI_MSGTX_USBHOST2,

        // Input Remapping
        TRIGMAP1, TRIGMAP2, TRIGMAP3, TRIGMAP4,
        CVMAP1, CVMAP2, CVMAP3, CVMAP4,
        TRIGMAP5, TRIGMAP6, TRIGMAP7, TRIGMAP8,
        CVMAP5, CVMAP6, CVMAP7, CVMAP8,

        // Global Quantizers: 4x(Scale, Root, Octave, Mask?)
        QUANT1, QUANT2, QUANT3, QUANT4,
        QUANT5, QUANT6, QUANT7, QUANT8,

        // MIDI Mappings
        MIDIMAP1, MIDIMAP2, MIDIMAP3, MIDIMAP4,
        MIDIMAP5, MIDIMAP6, MIDIMAP7, MIDIMAP8,
        MIDIMAP9, MIDIMAP10, MIDIMAP11, MIDIMAP12,
        MIDIMAP13, MIDIMAP14, MIDIMAP15, MIDIMAP16,
        MIDIMAP17, MIDIMAP18, MIDIMAP19, MIDIMAP20,
        MIDIMAP21, MIDIMAP22, MIDIMAP23, MIDIMAP24,
        MIDIMAP25, MIDIMAP26, MIDIMAP27, MIDIMAP28,
        MIDIMAP29, MIDIMAP30, MIDIMAP31, MIDIMAP32,

        IIMAP1,
        IIMAP32 = IIMAP1 + 31,

        // Applet visibility (dummy position)
        SHOWHIDELIST,

        MAX_CURSOR = IIMAP32
    };

    FLASHMEM __attribute__((noinline)) void Randomizer(bool audio = false);

    FLASHMEM __attribute__((noinline)) void ConfigEncoderAction(int h, int dir);
    FLASHMEM __attribute__((noinline)) void DrawIiMaps() const;
    FLASHMEM __attribute__((noinline)) void AdjustIiRange(int direction);
    FLASHMEM void AdjustIiDirection(int direction);
    FLASHMEM void ConfirmIiDirection();
    FLASHMEM void AdjustIiOutput(int direction);
    FLASHMEM void AdjustIiField(bool left, int direction);
    bool OutputEditorOpen() const {
      return view_state == APPLET_FULLSCREEN && isEditing &&
        (zoom_cursor == 5 || zoom_cursor == 6) && zoom_slot >= 0 && zoom_slot < APPLET_SLOTS &&
        config_page == HIDE_CONFIG && !preset_cursor && !clock_overlay && !HS::q_edit && !HS::midi_edit;
    }
    FLASHMEM int OutputFieldAt(int row) const;
    FLASHMEM void DrawOutputEditor() const;
    FLASHMEM void AdjustOutputField(bool left, int direction);
    FLASHMEM bool HandleOutputButton(const UI::Event& event);
    int output_field = 0;
    bool output_pending = false;
    int output_pending_source = -1, output_pending_target = 0;
    int output_pending_field = -1;
    phz::routing::OutputRouting<DAC_CHANNEL_COUNT>::Proposal output_proposal;
    FLASHMEM bool HandleIiMapButton(const UI::Event &event);
    PHZ_REMOTE_CODE bool HandleRemoteUi(char selector) override;
    PHZ_REMOTE_CODE phz::remote::UiState RemoteUiState() const override;
    bool ii_edit = false;
    phz::ii::DirectionChangePrompt ii_change;
    unsigned ii_from = 0;
    int ii_field = 0; // 0=Type, 1=Input range; matches MIDI popup cursor pattern
    PHZ_REMOTE_CODE void ConfigButtonPush(int h) {
      static uint8_t old_bank_num = 0xff;
        if (preset_cursor) {
            // left enc toggles bank select
            if (0 == h || isEditing) {
              isEditing = !isEditing;
              if (!isEditing) {
                if (old_bank_num != bank_num) SetBank(bank_num);
              } else
                old_bank_num = bank_num;

              return;
            }

            // Save or Load on button push
            switch (config_cursor) {
              case DELETE_PRESET:
                DeletePreset(preset_cursor - 1);
                return; // don't close menu
                break;
              case SAVE_PRESET:
                StoreToPreset(preset_cursor - 1);
                break;
              case LOAD_PRESET:
                QueuePresetLoad(preset_cursor - 1);
                break;
            }

            preset_cursor = 0; // deactivate preset selection
            config_page = HIDE_CONFIG;
            view_state = APPLETS;
            isEditing = false;
            return;
        }

        switch (config_cursor) {
          case RANDOMIZE_PRESET:
            // reset input mappings to defaults
            HS::Init();
            Randomizer(true);
            jump_trig_.Clear();
            config_page = HIDE_CONFIG;
            break;

          case DELETE_PRESET:
          case SAVE_PRESET:
          case LOAD_PRESET:
            preset_cursor = constrain(preset_id + 1, 1, QUAD_PRESET_COUNT);
            break;

          case AUTO_SAVE:
            HS::auto_save_enabled = !HS::auto_save_enabled;
            break;

          case QUANT1:
          case QUANT2:
          case QUANT3:
          case QUANT4:
          case QUANT5:
          case QUANT6:
          case QUANT7:
          case QUANT8:
            HS::QuantizerEdit(config_cursor - QUANT1);
            break;

          case CVMAP1:
          case CVMAP2:
          case CVMAP3:
          case CVMAP4:
          case CVMAP5:
          case CVMAP6:
          case CVMAP7:
          case CVMAP8:
            if (CheckEditInputMapPress(
                  config_cursor,
                  IndexedInput(CVMAP1, cvmap[0]),
                  IndexedInput(CVMAP2, cvmap[1]),
                  IndexedInput(CVMAP3, cvmap[2]),
                  IndexedInput(CVMAP4, cvmap[3]),
                  IndexedInput(CVMAP5, cvmap[4]),
                  IndexedInput(CVMAP6, cvmap[5]),
                  IndexedInput(CVMAP7, cvmap[6]),
                  IndexedInput(CVMAP8, cvmap[7])
                ))
              break;
          case TRIGMAP1:
          case TRIGMAP2:
          case TRIGMAP3:
          case TRIGMAP4:
          case TRIGMAP5:
          case TRIGMAP6:
          case TRIGMAP7:
          case TRIGMAP8:
            if (CheckEditInputMapPress(
                  config_cursor,
                  IndexedInput(TRIGMAP1, trigmap[0]),
                  IndexedInput(TRIGMAP2, trigmap[1]),
                  IndexedInput(TRIGMAP3, trigmap[2]),
                  IndexedInput(TRIGMAP4, trigmap[3]),
                  IndexedInput(TRIGMAP5, trigmap[4]),
                  IndexedInput(TRIGMAP6, trigmap[5]),
                  IndexedInput(TRIGMAP7, trigmap[6]),
                  IndexedInput(TRIGMAP8, trigmap[7])
                ))
              break;
          case TRIG_LENGTH:
          case MIDI_BEND_RANGE:
          case MIDI_PC_CHANNEL:
          case MIDI_POLY_MODE:
          case SCREENSAVER_MODE:
            isEditing = !isEditing;
            break;

          case PRESET_JUMP_TRIG:
            if (!CheckEditInputMapPress(
                  config_cursor, IndexedInput(PRESET_JUMP_TRIG, jump_trig_)
                ))
              isEditing ^= 1;
            break;

          case CURSOR_MODE:
            HS::cursor_wrap = !HS::cursor_wrap;
            break;

          case AUTO_MIDI:
            HS::frame.autoMIDIOut = !HS::frame.autoMIDIOut;
            break;

          case MIDI_THRU_SERIAL:
            HS::midi_thru_disable ^= mMaskSerial;
            if (midi_thru_disable & mMaskSerial)
              MIDI1.turnThruOff();
            else
              MIDI1.turnThruOn();
            break;
          case MIDI_THRU_USBDEV:
            HS::midi_thru_disable ^= mMaskUSBDev;
            break;
          case MIDI_THRU_USBHOST1:
            HS::midi_thru_disable ^= mMaskUSBHost;
            break;
          case MIDI_THRU_USBHOST2:
            HS::midi_thru_disable ^= mMaskUSBHost2;
            break;

          case MIDI_CLKRX_SERIAL:
            HS::midi_clkrx_disable ^= mMaskSerial;
            break;
          case MIDI_CLKRX_USBDEV:
            HS::midi_clkrx_disable ^= mMaskUSBDev;
            break;
          case MIDI_CLKRX_USBHOST1:
            HS::midi_clkrx_disable ^= mMaskUSBHost;
            break;
          case MIDI_CLKRX_USBHOST2:
            HS::midi_clkrx_disable ^= mMaskUSBHost2;
            break;

          case MIDI_CLKTX_SERIAL:
            HS::midi_clktx_disable ^= mMaskSerial;
            break;
          case MIDI_CLKTX_USBDEV:
            HS::midi_clktx_disable ^= mMaskUSBDev;
            break;
          case MIDI_CLKTX_USBHOST1:
            HS::midi_clktx_disable ^= mMaskUSBHost;
            break;
          case MIDI_CLKTX_USBHOST2:
            HS::midi_clktx_disable ^= mMaskUSBHost2;
            break;

          case MIDI_MSGRX_SERIAL:
            HS::midi_msgrx_disable ^= mMaskSerial;
            break;
          case MIDI_MSGRX_USBDEV:
            HS::midi_msgrx_disable ^= mMaskUSBDev;
            break;
          case MIDI_MSGRX_USBHOST1:
            HS::midi_msgrx_disable ^= mMaskUSBHost;
            break;
          case MIDI_MSGRX_USBHOST2:
            HS::midi_msgrx_disable ^= mMaskUSBHost2;
            break;

          case MIDI_MSGTX_SERIAL:
            HS::midi_msgtx_disable ^= mMaskSerial;
            break;
          case MIDI_MSGTX_USBDEV:
            HS::midi_msgtx_disable ^= mMaskUSBDev;
            break;
          case MIDI_MSGTX_USBHOST1:
            HS::midi_msgtx_disable ^= mMaskUSBHost;
            break;
          case MIDI_MSGTX_USBHOST2:
            HS::midi_msgtx_disable ^= mMaskUSBHost2;
            break;

          case SHOWHIDELIST:
            if (h == 0) // left encoder inverts selection
            {
              HS::hidden_applets[0] = ~HS::hidden_applets[0];
              HS::hidden_applets[1] = ~HS::hidden_applets[1];
            } else // right encoder toggles current
              HS::showhide_applet(showhide_cursor.cursor_pos());
            break;
          default: {
            if (config_cursor >= IIMAP1 && config_cursor <= IIMAP32) {
              ii_edit = true;
              ii_field = 0;
              HS::popup_tick = 0;
              break;
            }
            // I'm not going to paste 32 different MIDI cursor positions so it's just the default :P
            int midx = constrain(config_cursor - MIDIMAP1, 0, 31);
            HS::MidiMapEdit(midx);
            break;
          }
        }
    }

    void DrawInputMappings() const {
        gfxHeader("<  Input Mapping    >");
        gfxIcon(25, 13, TR_ICON); gfxIcon(89, 13, TR_ICON);
        gfxIcon(25, 26, CV_ICON); gfxIcon(89, 26, CV_ICON);
        gfxIcon(25, 39, TR_ICON); gfxIcon(89, 39, TR_ICON);
        gfxIcon(25, 52, CV_ICON); gfxIcon(89, 52, CV_ICON);

        for (int ch=0; ch<4; ++ch) {
          // Physical trigger input mappings
          // Physical CV input mappings
          // Top 2 applets
          gfxPrint(4 + ch*32, 15, HS::trigmap[ch].InputName() );
          gfxPrint(4 + ch*32, 28, HS::cvmap[ch].InputName() );

          // Bottom 2 applets
          gfxPrint(4 + ch*32, 41, HS::trigmap[ch + 4].InputName() );
          gfxPrint(4 + ch*32, 54, HS::cvmap[ch + 4].InputName() );
        }

        gfxDottedLine(63, 11, 63, 63); // vert
        gfxDottedLine(0, 38, 127, 38); // horiz

        // Cursor location is within a 4x4 grid
        const int cur_x = (config_cursor-TRIGMAP1) % 4;
        const int cur_y = (config_cursor-TRIGMAP1) / 4;

        gfxCursor(4 + 32*cur_x, 23 + 13*cur_y, 19);

        gfxDisplayInputMapEditor();
    }
    void DrawQuantizerConfig() const {
        gfxHeader("<  Quantizer Setup  >");
        if (quantizer_dirty_) gfxPrint(120, 1, "*");

        for (int ch=0; ch<4; ++ch) {
          const int x = 8 + ch*32;

          // 1-4 on top
          gfxPrint(x, 15, "Q");
          gfxPrint(ch + 1);
          gfxLine(x, 23, x + 14, 23);
          gfxLine(x + 14, 13, x + 14, 23);

          const bool upper = config_cursor < QUANT5;
          const int ch_view = upper ? ch : ch + 4;
          auto &q = q_engine[ch_view];

          gfxIcon(x + 3, upper? 25 : 45, upper? UP_BTN_ICON : DOWN_BTN_ICON);

          // Scale
          gfxPrint(x - 3, 30, OC::scale_names_short[ q.scale ]);

          // Root Note + Octave
          gfxPrint(x - 3, 40, OC::Strings::note_names[ q.root_note ]);
          if (q.octave >= 0) gfxPrint("+");
          gfxPrint(q.octave);

          // 5-8 on bottom
          gfxPrint(x, 55, "Q");
          gfxPrint(ch + 5);
          gfxLine(x, 53, x + 14, 53);
          gfxLine(x + 14, 53, x + 14, 63);
        }

        switch (config_cursor) {
        case QUANT1:
        case QUANT2:
        case QUANT3:
        case QUANT4:
          gfxIcon( 32*(config_cursor-QUANT1), 15, RIGHT_ICON);
          break;
        case QUANT5:
        case QUANT6:
        case QUANT7:
        case QUANT8:
          gfxIcon( 32*(config_cursor-QUANT5), 55, RIGHT_ICON);
          break;
        }
    }

    void DrawConfigMenu() const {
        // --- Config Selection
        gfxHeader("< General Settings  >");
        const int NUM_ROWS = TRIGMAP1 - TRIG_LENGTH; // lol weird
        const int SHOW_ROWS = 6; // originally 5
        const int ROW_HEIGHT = 8; // originally 10

        int scroll_top = config_cursor - TRIG_LENGTH - 2;
        CONSTRAIN(scroll_top, 0, NUM_ROWS - SHOW_ROWS);

        // Draw 6 visible rows from scroll_top (scroll_top is a row index)
        for (int i = 0; i < SHOW_ROWS; ++i) {
            int row = scroll_top + i;
            if (row >= NUM_ROWS) break;
            HS::DrawConfigRow(
              row,
              15 + i * ROW_HEIGHT,
              (config_cursor - TRIG_LENGTH) == row,
              EditMode()
            );
        }

        // Scroll arrows
        if (scroll_top > 0)
            gfxIcon(121, 14, UP_ICON);
        if (scroll_top + SHOW_ROWS < NUM_ROWS)
            gfxIcon(121, 56, DOWN_ICON);

        gfxDisplayInputMapEditor();
    }

    bool isValidPreset(int id) const {
      uint64_t data;
      return PhzConfig::getValue(id << 11 | APPLET_METADATA_KEY, data);
    }
    HemisphereApplet* GetApplet(int id, size_t h) const {
        uint64_t data = 0;
        PhzConfig::getValue(id << 11 | APPLET_METADATA_KEY, data);
        int idx = HS::get_applet_index_by_id( Unpack(data, PackLocation{h*8, 8}) );
        return HS::get_applet(idx, HEM_SIDE(h));
    }
    void DrawPresetSelector() const {
        const char * const hdrtxt[] = { "DEL!", "Load", "Save", "???" };
        gfxHeader(hdrtxt[config_cursor]);
        gfxPrint(30, 1, "Preset: Bank# ");
        gfxPrint(bank_num);
        if (!SDcard_Ready) gfxInvert(78, 0, 30, 9);

        // bank select cursor
        if (isEditing) {
          gfxIcon(114, 11, UP_ICON);
          return;
        }

        // Draw preset list
        gfxDottedLine(16, 11, 16, 63);
        int y = 5 + constrain(preset_cursor,1,5)*10;
        gfxIcon(0, y, RIGHT_ICON);
        const int top = constrain(preset_cursor - 4, 1, QUAD_PRESET_COUNT) - 1;
        y = 15;
        for (int i = top; i < QUAD_PRESET_COUNT && i < top + 5; ++i)
        {
            if (i == preset_id)
              gfxIcon(8, y, ZAP_ICON);
            else
              gfxPrint(8, y, OC::Strings::capital_letters[i]);

            if (!isValidPreset(i))
                gfxPrint(18, y, "(empty)");
            else {
                gfxIcon(18, y, GetApplet(i, LEFT_HEMISPHERE)->applet_icon());
                gfxPrint(26, y, GetApplet(i, LEFT_HEMISPHERE)->applet_name());
                gfxPrint(", ");
                gfxPrint(GetApplet(i, RIGHT_HEMISPHERE)->applet_name());
                gfxIcon(120, y, GetApplet(i, RIGHT_HEMISPHERE)->applet_icon(), true);
            }

            y += 10;
        }
    }
};

void QuadrantSysExHandler() {
  // TODO
}

////////////////////////////////////////////////////////////////////////////////
//// O_C App Functions
////////////////////////////////////////////////////////////////////////////////

// App stubs
void AppQuadrants::Init() {
    BaseStart();
}

size_t AppQuadrants::SaveAppData(util::StreamBufferWriter &stream_buffer) const {
  return 0;
}
size_t AppQuadrants::RestoreAppData(util::StreamBufferReader &stream_buffer) {
  return 0;
}

void AppQuadrants::Process(OC::IOFrame *ioframe) {
  BaseController(ioframe);
}
void AppQuadrants::GetIOConfig(OC::IOConfig &ioconfig) const
{
  using namespace OC;
  ioconfig.digital_inputs[DIGITAL_INPUT_1].set("TR1");
  ioconfig.digital_inputs[DIGITAL_INPUT_2].set("TR2");
  ioconfig.digital_inputs[DIGITAL_INPUT_3].set("TR3");
  ioconfig.digital_inputs[DIGITAL_INPUT_4].set("TR4");

  ioconfig.cv[0].set("CV1");
  ioconfig.cv[1].set("CV2");
  ioconfig.cv[2].set("CV3");
  ioconfig.cv[3].set("CV4");
  ioconfig.cv[4].set("CV5");
  ioconfig.cv[5].set("CV6");
  ioconfig.cv[6].set("CV7");
  ioconfig.cv[7].set("CV8");

  ioconfig.outputs[0].set("Out A", OUTPUT_MODE_PITCH);
  ioconfig.outputs[1].set("Out B", OUTPUT_MODE_PITCH);
  ioconfig.outputs[2].set("Out C", OUTPUT_MODE_PITCH);
  ioconfig.outputs[3].set("Out D", OUTPUT_MODE_PITCH);
  ioconfig.outputs[4].set("Out E", OUTPUT_MODE_PITCH);
  ioconfig.outputs[5].set("Out F", OUTPUT_MODE_PITCH);
  ioconfig.outputs[6].set("Out G", OUTPUT_MODE_PITCH);
  ioconfig.outputs[7].set("Out H", OUTPUT_MODE_PITCH);
}

void AppQuadrants::HandleAppEvent(OC::AppEvent event) {
    switch (event) {
    case OC::APP_EVENT_RESUME:
        Resume();
        break;

    case OC::APP_EVENT_SCREENSAVER_ON:
    case OC::APP_EVENT_SUSPEND:
        Suspend();
        break;

    default: break;
    }
}

void AppQuadrants::Loop() {
    mainloop();
    audio_app.mainloop();
    RecallTick();
}

FLASHMEM
void AppQuadrants::DrawMenu() const {
    View();
}

void AppQuadrants::DrawScreensaver() const {
    switch (HS::screensaver_mode) {
    case SCREEN_ZIPS:
    case SCREEN_STARS:
    case SCREEN_ZAPS:
        ZapScreensaver(screensaver_mode - SCREEN_ZAPS);
        break;
    case SCREEN_SCOPE: // output scope
        OC::scope_render();
        break;
    case SCREEN_METERS:
        BaseScreensaver(true); // show note names
        break;
    case SCREEN_BEATS:
        BeatCounterScreensaver();
        break;
    default: break; // blank screen
    }
}
void AppQuadrants::DrawDebugInfo() const {
  // TODO:
}

FLASHMEM
void AppQuadrants::HandleButtonEvent(const UI::Event &event) {
    last_mask = mask;
    mask = event.mask;
    if (HandleOutputButton(event)) return;
    if (HandleIiMapButton(event)) return;
    /*SERIAL_PRINTLN(*/
    /*  "mask=%d type=%d value=%d control=%d last_mask=%d",*/
    /*  event.mask, event.type, event.value, event.control,*/
    /*  last_mask*/
    /*);*/

    if (AUDIO_SETUP == view_state && !clock_overlay && !preset_cursor) {
      if (CheckButtonCombo(OC::CONTROL_BUTTON_A | OC::CONTROL_BUTTON_B)) {
        clock_overlay = true;
        //view_state = APPLETS;
        return;
      }
      // dual press A+X for Load Preset
      if (CheckButtonCombo(OC::CONTROL_BUTTON_A | OC::CONTROL_BUTTON_X)) {
        ShowPresetSelector();
        return;
      }
      if ((event.control == OC::CONTROL_BUTTON_L
           || event.control == OC::CONTROL_BUTTON_R)) {
        audio_app.HandleEncoderButtonEvent(event);
        return;
      }
      if (event.control == OC::CONTROL_BUTTON_X
          || event.control == OC::CONTROL_BUTTON_Y
          || event.control == OC::CONTROL_BUTTON_A
          || event.control == OC::CONTROL_BUTTON_B) {
        if (audio_app.HandleButtonEvent(event))
          view_state = APPLETS;
        return;
      }
    }

    if (CheckButtonCombo(OC::CONTROL_BUTTON_A | OC::CONTROL_BUTTON_Y) ||
        CheckButtonCombo(OC::CONTROL_BUTTON_X | OC::CONTROL_BUTTON_B)) {
      view_state = OVERVIEW;
      OC::ui.SetButtonIgnoreMask();
      return;
    }

    switch (event.type) {
    case UI::EVENT_BUTTON_DOWN:

      // Quantizer popup editor intercepts everything on-press
      if (HS::q_edit) {
        if (event.control == OC::CONTROL_BUTTON_B) {
          HS::NudgeOctave(HS::qview, 1);
          quantizer_dirty_ = true;
        } else if (event.control == OC::CONTROL_BUTTON_A) {
          HS::NudgeOctave(HS::qview, -1);
          quantizer_dirty_ = true;
        } else {
          HS::q_edit = 0;
          HS::popup_tick = 0;
          select_mode = -1;
        }

        OC::ui.SetButtonIgnoreMask();
        break;
      }
      if (HS::midi_edit) {
        if (event.control == OC::CONTROL_BUTTON_A) {
          mview = constrain(mview - 1, 0, MIDIMAP_MAX-1);
          config_cursor = MIDIMAP1 + mview;
        } else if (event.control == OC::CONTROL_BUTTON_B) {
          mview = constrain(mview + 1, 0, MIDIMAP_MAX-1);
          config_cursor = MIDIMAP1 + mview;
        } else if (event.control == OC::CONTROL_BUTTON_Z) {
          frame.MIDIState.mapping[mview].AutoLearn();
          frame.MIDIState.UpdateMidiChannelFilter();
        } else {
          HS::midi_edit = 0;
          HS::popup_tick = 0;
          select_mode = -1;
        }
        OC::ui.SetButtonIgnoreMask();
        break;
      }

      if (event.control == OC::CONTROL_BUTTON_L ||
          event.control == OC::CONTROL_BUTTON_R)
      {
          if (clock_overlay) {
            ClockSetup_instance.OnButtonPress();
          } else
            DelegateEncoderPush(event);
          // ignore long-press to prevent Main Menu >:)
          //OC::ui.SetButtonIgnoreMask();
      } else if (
        event.control == OC::CONTROL_BUTTON_A ||
        event.control == OC::CONTROL_BUTTON_B ||
        event.control == OC::CONTROL_BUTTON_X ||
        event.control == OC::CONTROL_BUTTON_Y)
      {
          if (CheckButtonCombos(event)) {
            select_mode = -1;
            isEditing = false;
            ClearEditInputMap();
            OC::ui.SetButtonIgnoreMask(); // ignore release and long-press
          } else {
            HEM_SIDE slot = ButtonToSlot(event);
            if (OC::CORE::ticks - click_tick < HEMISPHERE_DOUBLE_CLICK_TIME
                && (slot == first_click))
            {
                // This is a double-click on one button.
                SetFullScreen(slot);
                click_tick = 0;
                OC::ui.SetButtonIgnoreMask(); // ignore button release
                return;
            }

            // -- Single click
            // If a help screen is already selected, and the button is for
            // the opposite one, go to the other help screen
            if (view_state == APPLET_FULLSCREEN) {
                if (zoom_slot != slot) SetFullScreen(slot);
                else ExitFullScreen(); // Exit help screen if same button is clicked
                OC::ui.SetButtonIgnoreMask(); // ignore release
            }
            if (view_state == OVERVIEW) {
              // TODO: what do A/B/X/Y do in the Overview? modifiers?
            }

            // mark this single click
            click_tick = OC::CORE::ticks;
            first_click = slot;
          }
      }

      break;

    case UI::EVENT_BUTTON_PRESS:
      // A/B/X/Y switch to corresponding applet on release
      if (event.control == OC::CONTROL_BUTTON_A ||
          event.control == OC::CONTROL_BUTTON_B ||
          event.control == OC::CONTROL_BUTTON_X ||
          event.control == OC::CONTROL_BUTTON_Y)
      {
        HEM_SIDE slot = ButtonToSlot(event);
        if (view_state == APPLET_FULLSCREEN && slot == zoom_slot)
          view_state = APPLETS;

        if (view_state == OVERVIEW) {
          // jump to applet view on release
          SetFullScreen(slot);
          zoom_cursor = -1; // applet UI, not config
        } else
          SwitchToSlot(slot);
      }
      // fall thru here
    case UI::EVENT_BUTTON_LONG_RELEASE:
      // Z-button acts on release, either short or long
      if (event.control == OC::CONTROL_BUTTON_Z) {
        ToggleClockRun();
      }

      // ignore all other button release events
      break;

    case UI::EVENT_BUTTON_LONG_PRESS:
      if (event.control == OC::CONTROL_BUTTON_B) ToggleConfigMenu();
      if (event.control == OC::CONTROL_BUTTON_Z) {
        // TODO: show a menu when Z is held long enough
        // - encoder can change selected Z action
      }
      break;

    default: break;
    }
}

FLASHMEM
void AppQuadrants::HandleEncoderEvent(const UI::Event &event) {
    DelegateEncoderMovement(event);
}

FLASHMEM
void AppQuadrants::StoreToPreset(int id) {
  store_to_preset(id);
}
FLASHMEM
void AppQuadrants::LoadFromPreset(int id) {
  load_from_preset(id);
}

// Cold UI/bank routines are out-of-class so FLASHMEM is honored by the linker.
FLASHMEM void AppQuadrants::SetBank(uint8_t id) {
    for(int slot=0;slot<4;++slot)RecallCapture(slot);
    if (ii_recall_bank && !RecallSave()) { bank_num=ii_recall_bank->Bank(); return; }
    phz::ii::Transition transition(false);
      bank_filename[5] = '0' + char(id / 100);
      bank_filename[6] = '0' + char(id / 10 % 10);
      bank_filename[7] = '0' + char(id % 10);

      bool success = false;
      if (SDcard_Ready) // load from SD card
        success = PhzConfig::load_config(bank_filename, SD);

      if (!success) // fallback load from LFS
        success = PhzConfig::load_config(bank_filename);

      LoadGlobals();

      // Version flag - for future reconfigurations...
      //  - This can be used to migrate old data after breaking changes to the schema
      //  - Right now, its existence indicates v1.10.1+
      uint64_t data = 0;
      bool version_key_present = PhzConfig::getValue(VERSION_KEY, data);
      int preset_file_revision = version_key_present? data : 0;

      // Data migration to protect against a bug in v1.10
      // This can be removed later, when we're sure no one is still using broken preset files...
      if (!version_key_present) {
        for (size_t i = 0; i < QUAD_PRESET_COUNT; ++i) {
          PhzConfig::deleteKey((i << 11) | (CVMAP_KEY + 1));
          PhzConfig::deleteKey((i << 11) | (TRIGMAP_KEY + 1));
        }
      }
      if (preset_file_revision < 1) {
        // migrations from v1.x to v2.0 go here
        // these are handled elsewhere in Unpack() functions:
        // - CVInputMap sources got remapped
        // - MIDI Map functions got remapped
      }

      // update version key after all data migrations
      PhzConfig::setValue(VERSION_KEY, PRESET_FILE_REVISION);
      RecallLoad(id);
    if (success) { ii_loaded_setup.BankLoaded(id); transition.Complete(); }
  }
FLASHMEM void AppQuadrants::Randomizer(bool audio) {
    // Only the full standard+audio reset can recover a failed audio restore.
    phz::ii::Transition transition(audio);
      static uint8_t cycle = 0;

      if (audio) audio_app.ReInit();

      switch (cycle) {
        case 0:
          // randomize all applets
          for (int ch = 0; ch < APPLET_SLOTS; ++ch) {
            size_t index = random(HEMISPHERE_AVAILABLE_APPLETS);
            SetApplet(HEM_SIDE(ch), index);
#ifdef PEWPEWPEW
            // load random data !!!
            // this will expose critical bugs in data validation ;)
            HS::get_applet(index, ch)->OnDataReceive(uint64_t(random()) << 32 | (uint64_t)random());
#endif
          }
          PokePopup(HS::MESSAGE_POPUP, "Reset/Random");
          break;
        case 3:
          // oops all LFOs
          SetApplet(HEM_SIDE(0), HS::get_applet_index_by_id(7)); // Ebb&LFO
          SetApplet(HEM_SIDE(1), HS::get_applet_index_by_id(89)); // Relabi
          SetApplet(HEM_SIDE(2), HS::get_applet_index_by_id(49)); // VectorLFO
          SetApplet(HEM_SIDE(3), HS::get_applet_index_by_id(49)); // VectorLFO
          PokePopup(HS::MESSAGE_POPUP, "LFOs!");
          break;
        case 2:
          // oops all envelopes
          SetApplet(HEM_SIDE(0), HS::get_applet_index_by_id(8)); // ADSR
          SetApplet(HEM_SIDE(1), HS::get_applet_index_by_id(52)); // VectorEG
          SetApplet(HEM_SIDE(2), HS::get_applet_index_by_id(8)); // ADSR
          SetApplet(HEM_SIDE(3), HS::get_applet_index_by_id(52)); // VectorEG
          PokePopup(HS::MESSAGE_POPUP, "Envelopes!");
          break;
        case 1:
          // oops all quantizers
          SetApplet(HEM_SIDE(0), HS::get_applet_index_by_id(9)); // DualQuant
          SetApplet(HEM_SIDE(1), HS::get_applet_index_by_id(9)); // DualQuant
          SetApplet(HEM_SIDE(2), HS::get_applet_index_by_id(46)); // Squanch
          SetApplet(HEM_SIDE(3), HS::get_applet_index_by_id(46)); // Squanch
          PokePopup(HS::MESSAGE_POPUP, "Quantizers!");
          break;
        case 4:
          // oops all sequencers
          SetApplet(HEM_SIDE(0), HS::get_applet_index_by_id(14)); // SeqX
          SetApplet(HEM_SIDE(1), HS::get_applet_index_by_id(60)); // TB-3PO
          SetApplet(HEM_SIDE(2), HS::get_applet_index_by_id(58)); // Shredder
          SetApplet(HEM_SIDE(3), HS::get_applet_index_by_id(18)); // TwoRings
          PokePopup(HS::MESSAGE_POPUP, "Sequencers!");
          break;
        case 5:
          // oops all clocks
          SetApplet(HEM_SIDE(0), HS::get_applet_index_by_id(6)); // ClockDiv
          SetApplet(HEM_SIDE(1), HS::get_applet_index_by_id(50)); // Metronome
          SetApplet(HEM_SIDE(2), HS::get_applet_index_by_id(72)); // PolyDiv
          SetApplet(HEM_SIDE(3), HS::get_applet_index_by_id(15)); // EuclidX
          PokePopup(HS::MESSAGE_POPUP, "Clocks!");
          break;
      }

      ++cycle %= 6;
    if (IiReady()) transition.Complete();
  }
PHZ_REMOTE_CODE phz::remote::UiState AppQuadrants::RemoteUiState() const {
  phz::remote::UiState s;
  s.known = true;
  s.page = config_page;
  s.input = config_page == II_MAPS_PAGE && config_cursor >= IIMAP1 &&
    config_cursor <= IIMAP32 ? config_cursor - IIMAP1 + 1 : 0;
  s.popup = ii_edit; // Raw state exposes stale popup flags even off-page.
  s.view = view_state;
  s.select = select_mode;
  s.preset = preset_cursor;
  s.clock = clock_overlay;
  s.quant = HS::q_edit;
  s.midi = HS::midi_edit;
  return s;
}

PHZ_REMOTE_CODE bool AppQuadrants::HandleRemoteUi(char selector) {
  if (ii_change.Pending()) return false; // physical confirmation only
  // First remote slice is ii Maps ONLY. No route to preset/file/global menus.
  if (config_page != II_MAPS_PAGE || preset_cursor || clock_overlay ||
      HS::q_edit || HS::midi_edit || !IiReady() ||
      config_cursor < IIMAP1 || config_cursor > IIMAP32) return false;
  if (selector == '[' || selector == ']') {
    const int delta = selector == '[' ? -1 : 1;
    if (ii_edit) AdjustIiRange(delta);
    else config_cursor = constrain(config_cursor + delta, IIMAP1, IIMAP32);
    return true;
  }
  if (selector == 'A' || selector == 'B') {
    if (!ii_edit) return false;
    return HandleIiMapButton(UI::Event(UI::EVENT_BUTTON_DOWN,
      selector == 'A' ? OC::CONTROL_BUTTON_A : OC::CONTROL_BUTTON_B, 0, 0));
  }
  if (selector == 'l' || selector == 'r') {
    if (ii_edit) return HandleIiMapButton(UI::Event(UI::EVENT_BUTTON_DOWN,
      selector == 'l' ? OC::CONTROL_BUTTON_L : OC::CONTROL_BUTTON_R, 0, 0));
    ConfigButtonPush(selector == 'l' ? 0 : 1);
    return true;
  }
  return false;
}

FLASHMEM bool AppQuadrants::HandleIiMapButton(const UI::Event &event) {
  if (!ii_edit) return false;
  if (ii_change.Pending()) {
    if (event.type == UI::EVENT_BUTTON_DOWN) {
      if (event.control == OC::CONTROL_BUTTON_R) ConfirmIiDirection();
      else ii_change.Cancel();
      OC::ui.SetButtonIgnoreMask();
    }
    return true;
  }
  if (event.type == UI::EVENT_BUTTON_DOWN) {
    ii_field = 0;
    if (event.control == OC::CONTROL_BUTTON_A)
      config_cursor = constrain(config_cursor - 1, IIMAP1, IIMAP32);
    else if (event.control == OC::CONTROL_BUTTON_B)
      config_cursor = constrain(config_cursor + 1, IIMAP1, IIMAP32);
    else {
      ii_edit = false;
      HS::popup_tick = 0;
      select_mode = -1;
    }
    OC::ui.SetButtonIgnoreMask();
  }
  return true;
}

FLASHMEM void AppQuadrants::AdjustIiRange(int direction) {
  if (ii_change.Pending()) return;
  const size_t index = constrain(config_cursor - IIMAP1, 0, 31);
  int current;
  { phz::ii::InterruptLock lock;
    if (phz::ii::virtual_inputs.DirectionalMappings::Get(index).direction !=
        phz::ii::DirectionalMappings::Direction::Input) return;
    current = int(phz::ii::virtual_inputs.Get(index).range);
  }
  const int next = constrain(current + (direction > 0 ? 1 : direction < 0 ? -1 : 0), 1, 3);
  if (next == current) return;
  phz::ii::Transition transition(false);
  {
    phz::ii::InterruptLock lock;
    phz::ii::virtual_inputs.SetRange(index,
      static_cast<phz::ii::VirtualInputs::Range>(next));
  }
  if (IiReady()) transition.Complete();
}

FLASHMEM void AppQuadrants::DrawFullScreen() const {
      if (OutputEditorOpen()) { DrawOutputEditor(); return; }
      if (select_mode == zoom_slot) {
        showhide_cursor.Scroll(next_applet_index[zoom_slot] - showhide_cursor.cursor_pos());
        DrawAppletList(CursorBlink());
        // dotted screen border during applet select
        gfxFrame(0, 0, 128, 64, true);
        return;
      }

      active_applet[zoom_slot]->BaseView(true, zoom_cursor < 0);
      // Applets 3 and 4 get inverted titles
      if (zoom_slot > 1) gfxInvert(0 + (zoom_slot%2)*64, 0, 63, 10);

      // draw cursors for editing applet select and input maps
      if (zoom_cursor < 0) {
        gfxIcon(64 - 8*(zoom_slot & 1), 1, DOWN_ICON, true);
      } else if (0 == zoom_cursor) {
        if (select_mode != zoom_slot && CursorBlink())
          gfxIcon(64 - 8*(zoom_slot & 1), 1, (zoom_slot & 1)? RIGHT_ICON : LEFT_ICON, true);
      } else if (isEditing) {
        const int x = 64*((zoom_cursor-1)%2);
        const int y = 13 + 10*((zoom_cursor-1)/2);
        gfxInvert(x, y, zoom_cursor >= 7 ? 60 : 19, 9);
        gfxFrame(x, y, zoom_cursor >= 7 ? 60 : 19, 9, true);

        if (zoom_cursor <= 2) {
          // trigmap & clock multiplier
          const int io_chan = zoom_slot * 2 + zoom_cursor - 1;
          const int mult = HS::clock_m.GetMultiply(io_chan);

          graphics.clearRect(x, y - 9, 36, 8);
          graphics.drawBitmap8(x, y - 9, 8, RIGHT_ICON);
          graphics.drawBitmap8(x + 8, y - 9, 8, CLOCK_ICON);
          graphics.setPrintPos(x + 16, y - 9);
          graphics.print((mult >= 0) ? "x" : "/");
          graphics.print((mult >= 0) ? mult : 1 - mult);
        }
        if (zoom_cursor == 5 || zoom_cursor == 6) {
          gfxIcon(x + 18, y + 1, DOWN_ICON, true);

          graphics.clearRect(0, y + 10, 127, 20);
          gfxPrint(x, y+10, "Slew=");
          gfxPrint(HS::frame.output_slew[zoom_slot*2 + zoom_cursor-5]);
          gfxPrint("%");

          const int att = Atten(HS::frame.output_atten[zoom_slot*2 + zoom_cursor-5]);
          gfxPrint(x, y+20, "Lvl=");
          if (att < 0) gfxPrint("-");
          graphics.printf("%d.%d%%", abs(att) / 10, abs(att) % 10);
        }
      } else {
        if (CursorBlink()) {
          const int x = (zoom_cursor >= 7 ? 54 : 18) + 64*((zoom_cursor-1)%2);
          const int y = 14 + 10*((zoom_cursor-1)/2);
          gfxIcon(x, y, LEFT_ICON, true);
        }
      }

      gfxDisplayInputMapEditor();
    }

FLASHMEM void AppQuadrants::DelegateEncoderMovement(const UI::Event &event) {
        int increment = event.value;
        if (event.mask & (OC::CONTROL_BUTTON_L | OC::CONTROL_BUTTON_R)) {
          // push-and-turn for coarse adjustments
          // XXX: hopefully nothing breaks if event.value is larger than 1 or -1...
          OC::ui.SetButtonIgnoreMask();
          increment *= 10;
        }
        int h = (event.control == OC::CONTROL_ENCODER_L) ? LEFT_HEMISPHERE : RIGHT_HEMISPHERE;
        HEM_SIDE slot = HEM_SIDE(view_slot[h]*2 + h);
        if (OutputEditorOpen()) {
          AdjustOutputField(h == LEFT_HEMISPHERE, increment);
          return;
        }
        if (ii_edit) {
          AdjustIiField(h == LEFT_HEMISPHERE, increment);
          return;
        }
        if (HS::q_edit) {
          HS::QEditEncoderMove(h, increment);
          if (h == RIGHT_HEMISPHERE && increment)
            quantizer_dirty_ = true;
          return;
        }
        if (HS::midi_edit) {
          HS::MEditEncoderMove(h, increment);
          return;
        }

        if (clock_overlay) {
          if (h == LEFT_HEMISPHERE)
            ClockSetup_instance.OnLeftEncoderMove(increment);
          else
            ClockSetup_instance.OnEncoderMove(increment);
          return;
        }

        if (config_page > HIDE_CONFIG || preset_cursor) {
            ConfigEncoderAction(h, increment);
            return;
        }
        if (view_state == AUDIO_SETUP) {
          audio_app.HandleEncoderEvent(event);
          return;
        }

        if (view_state == OVERVIEW) {
          // TODO:
          return;
        }

        if (view_state == APPLET_FULLSCREEN) {
            if (select_mode == zoom_slot)
              ChangeApplet(zoom_slot, increment);
            else if (h == LEFT_HEMISPHERE && !isEditing)
              zoom_cursor = (event.value > 0)? 0 : -1;
            else if (zoom_cursor < 0)
              active_applet[zoom_slot]->OnEncoderMove(increment);
            else if (isEditing) { // enc changes value
              switch (zoom_cursor)
              {
                case 1:
                case 2:
                {
                  const int chan = zoom_slot*2 + zoom_cursor - 1;
                  if (h == LEFT_HEMISPHERE) {
                    clock_m.SetMultiply(clock_m.GetMultiply(chan) + increment, chan);
                  } else if (!EditSelectedInputMap(increment))
                    HS::trigmap[chan].ChangeSource(increment);
                  break;
                }
                case 3:
                case 4:
                  if (!EditSelectedInputMap(increment)) {
                    auto& input = HS::cvmap[zoom_slot*2 + zoom_cursor - 3];
                    if (h == LEFT_HEMISPHERE) input.ChangeSourceGroup(increment);
                    else input.ChangeSource(increment);
                  }
                  break;
                case 5:
                case 6:
                  AdjustOutputField(h == LEFT_HEMISPHERE, increment);
                  break;
                default:
                  isEditing = false;
                  break;
              }
            } else { // enc moves cursor
              zoom_cursor = constrain(zoom_cursor + increment, 0, 6);
              ResetCursor();
            }
        } else if (event.mask & (OC::CONTROL_BUTTON_X | OC::CONTROL_BUTTON_Y)) {
            // hold down X or Y to change applet with encoder
            ChangeApplet(slot, increment);
            SetApplet(slot, next_applet_index[slot]);
        } else {
            active_applet[slot]->OnEncoderMove(increment);
        }
    }

FLASHMEM void AppQuadrants::DelegateEncoderPush(const UI::Event &event) {
        int h = (event.control == OC::CONTROL_BUTTON_L) ? LEFT_HEMISPHERE : RIGHT_HEMISPHERE;
        HEM_SIDE slot = HEM_SIDE(view_slot[h]*2 + h);

        if (config_page > HIDE_CONFIG || preset_cursor) {
          ConfigButtonPush(h);
          return;
        }
        if (view_state == AUDIO_SETUP) {
          // audio_app.HandleButtonEvent(event);
          return;
        }
        if (view_state == APPLET_FULLSCREEN) {
          if (zoom_cursor == 5 || zoom_cursor == 6) {
            if (h == RIGHT_HEMISPHERE) {
              isEditing = !isEditing; output_field = 0; output_pending = false;
            }
            return;
          }
          switch (zoom_cursor) {
            case -1:
              active_applet[zoom_slot]->OnButtonPress();
              break;

            case 0:
              if (zoom_slot == select_mode) {
                SetApplet(zoom_slot, next_applet_index[zoom_slot]);
                ExitFullScreen();
              } else
                select_mode = zoom_slot;
              break;
            //// 0=select; 1,2=trigmap; 3,4=cvmap; 5,6=outmode
            case 3:
            case 4:
              if (CheckEditInputMapPress(
                    zoom_cursor,
                    IndexedInput(3, cvmap[zoom_slot*2]),
                    IndexedInput(4, cvmap[zoom_slot*2+1])
                  ))
                break;
            case 1:
            case 2:
              if (h == RIGHT_HEMISPHERE && CheckEditInputMapPress(
                    zoom_cursor,
                    IndexedInput(1, trigmap[zoom_slot*2]),
                    IndexedInput(2, trigmap[zoom_slot*2+1])
                  ))
                break;
            case 5:
            case 6:
            default:
              isEditing = !isEditing;
              break;
          }
          return;
        }

        if (view_state == OVERVIEW) {
          // TODO:
          return;
        }

        active_applet[slot]->OnButtonPress();
    }

FLASHMEM int AppQuadrants::OutputFieldAt(int row) const {
  for (int field = 0; field < 5; ++field) {
    if (field == 1 && !OC::global_settings.ii_address.enabled()) continue;
    if (field == 2 && !HS::frame.autoMIDIOut) continue;
    if (!row--) return field;
  }
  return -1;
}

FLASHMEM void AppQuadrants::DrawOutputEditor() const {
  static const char label_0[] PROGMEM = "Out ";
  static const char label_1[] PROGMEM = " routing";
  static const char label_4[] PROGMEM = " Out";
  static const char label_5[] PROGMEM = " owns it";
  static const char label_6[] PROGMEM = "L:Back R:Replace";
  static const char label_7[] PROGMEM = "Physical: ";
  static const char label_8[] PROGMEM = "ii: ";
  static const char label_9[] PROGMEM = "MIDI: ";
  static const char label_10[] PROGMEM = "Slew: ";
  static const char label_11[] PROGMEM = "Level: ";
  static const char label_12[] PROGMEM = "L:Out R:Field";
  static const char label_13[] PROGMEM = "L:Back R:Confirm";
  static const char label_14[] PROGMEM = "R click:edit";
  static const char label_15[] PROGMEM = "Preview: ";
  const int source = zoom_slot*2 + zoom_cursor-5;
  graphics.clearRect(0, 0, 128, 64);
  gfxPrint(0,0,label_0); gfxPrint(zoom_cursor-4); gfxPrint(label_1);
  if (output_pending && output_pending_field==0) {
    gfxPrint(0,16,label_15);
    gfxPrint(output_pending_target ? OC::Strings::capital_letters[output_pending_target-1] : "None");
    if (output_proposal.Conflict()) {
      gfxPrint(0,28,"Q"); gfxPrint(output_proposal.Owner()/2+1);
      gfxPrint(label_4); gfxPrint(output_proposal.Owner()%2+1); gfxPrint(label_5);
    }
    gfxPrint(0,56,output_proposal.Conflict() ? label_6 : label_13);
    return;
  }
  for (int row=0; row<5; ++row) {
    const int field = OutputFieldAt(row);
    if (field < 0) break;
    const bool preview=output_pending && output_pending_field==field;
    gfxPrint(0,11+9*row,row == output_field ? (preview ? "*" : ">") : " ");
    switch (field) {
      case 0: {
        const auto d = HS::frame.output_routes.Destination(source);
        gfxPrint(label_7); gfxPrint(d ? OC::Strings::capital_letters[d-1] : "None"); break;
      }
      case 1: {
        uint16_t d;
        { phz::ii::InterruptLock lock; d=phz::ii::virtual_inputs.OutputMappingFor(source+1); }
        if(preview)d=output_pending_target;
        gfxPrint(label_8); if (d) {gfxPrint("I");gfxPrint(int(d));} else gfxPrint("None"); break;
      }
      case 2: gfxPrint(label_9); gfxPrint((preview ? output_pending_target : HS::frame.output_routes.MidiEnabled(source)) ? "On" : "Off"); break;
      case 3: gfxPrint(label_10); gfxPrint(preview ? output_pending_target : int(HS::frame.output_slew[source]));gfxPrint("%");break;
      case 4: {
        const int level=Atten(preview ? output_pending_target : HS::frame.output_atten[source]);
        gfxPrint(label_11); if(level<0)gfxPrint("-");gfxPrint(abs(level)/10);
        gfxPrint(".");gfxPrint(abs(level)%10);gfxPrint("%");break;
      }
    }
  }
  // Footer alternates navigation/edit hints without consuming another row.
  gfxPrint(0,56,output_pending ? label_13 : label_12);
  if(!output_pending && OutputFieldAt(3)<0)gfxPrint(0,47,label_14);
}

FLASHMEM void AppQuadrants::AdjustOutputField(bool left, int direction) {
  if (!OutputEditorOpen() || !direction) return;
  if (left) {
    output_pending=false;
    zoom_cursor=zoom_cursor==5?6:5; // Two logical outputs, independent of jack routing.
    return;
  }
  if (!output_pending) {
    int count=0;while(OutputFieldAt(count)>=0)++count;
    output_field=(output_field+(direction>0?1:count-1))%count;
    return;
  }
  const int source=zoom_slot*2+zoom_cursor-5;
  if(output_pending_source!=source || OutputFieldAt(output_field)!=output_pending_field) {
    output_pending=false;return;
  }
  switch(output_pending_field) {
    case 0: {
      phz::ii::InterruptLock lock;
      output_pending_target=constrain(output_pending_target+(direction>0?1:-1),0,DAC_CHANNEL_COUNT);
      output_proposal=HS::frame.output_routes.Propose(source,output_pending_target);
      break;
    }
    case 1: {
      phz::ii::InterruptLock lock;
      output_pending_target=phz::ii::NextMapping(phz::ii::virtual_inputs,output_pending_target,direction,
        phz::ii::DirectionalMappings::Direction::Output,source+1);
      break;
    }
    case 2: output_pending_target=direction>0;break;
    case 3: output_pending_target=constrain(output_pending_target+constrain(direction,-100,100),0,100);break;
    case 4: output_pending_target=constrain(output_pending_target+constrain(direction,-254,254),-127,127);break;
  }
}

FLASHMEM bool AppQuadrants::HandleOutputButton(const UI::Event& event) {
  if (!OutputEditorOpen()) {output_pending=false;return false;}
  if (event.type != UI::EVENT_BUTTON_DOWN) return true;
  if (event.control == OC::CONTROL_BUTTON_A ||
      event.control == OC::CONTROL_BUTTON_B ||
      event.control == OC::CONTROL_BUTTON_X ||
      event.control == OC::CONTROL_BUTTON_Y) {
    // Navigation never commits a preview. Preserve the logical output and field.
    output_pending = false;
    const int slot = ButtonToSlot(event);
    if (slot >= 0 && slot < APPLET_SLOTS && active_applet[slot]) {
      zoom_slot = slot;
      PokePopup(HS::MESSAGE_POPUP, active_applet[slot]->applet_name());
    }
    OC::ui.SetButtonIgnoreMask();
    return true;
  }
  if (event.control!=OC::CONTROL_BUTTON_R) {
    output_pending=false;isEditing=false;
    if(event.control!=OC::CONTROL_BUTTON_L)return false; // native applet buttons
  } else {
    const int source=zoom_slot*2+zoom_cursor-5;
    const int field=OutputFieldAt(output_field);
    if (!output_pending) {
      output_pending_source=source;output_pending_field=field;
      {phz::ii::InterruptLock lock;
        switch(field) {
          case 0: output_pending_target=HS::frame.output_routes.Destination(source);
            output_proposal=HS::frame.output_routes.Propose(source,output_pending_target);break;
          case 1: output_pending_target=phz::ii::virtual_inputs.OutputMappingFor(source+1);break;
          case 2: output_pending_target=HS::frame.output_routes.MidiEnabled(source);break;
          case 3: output_pending_target=HS::frame.output_slew[source];break;
          case 4: output_pending_target=HS::frame.output_atten[source];break;
        }
      }
      output_pending=field>=0;
    } else {
      if(output_pending_source==source && field==output_pending_field) {
        switch(field) {
          case 0: {phz::ii::InterruptLock lock;HS::frame.output_routes.Commit(output_proposal,true);}break;
          case 1: {
            if(IiReady() && OC::global_settings.ii_address.enabled()) {
              phz::ii::Transition transition(false);bool changed;
              {phz::ii::InterruptLock lock;
                changed=phz::ii::virtual_inputs.RouteOutput(source+1,output_pending_target,2*APPLET_SLOTS);
              }
              if(changed && IiReady()) { RecallCapture(source/2,true); transition.Complete(); }
            }
            break;
          }
          case 2:
            HS::frame.MIDIState.StopAutoOutput(source/2);
            {phz::ii::InterruptLock lock;HS::frame.output_routes.SetMidiEnabled(source,output_pending_target!=0);}break;
          case 3: HS::frame.NudgeSlew(source,output_pending_target-HS::frame.output_slew[source]);break;
          case 4: HS::frame.NudgeAtten(source,output_pending_target-HS::frame.output_atten[source]);break;
        }
      }
      output_pending=false;
    }
  }
  OC::ui.SetButtonIgnoreMask();
  return true;
}

FLASHMEM void AppQuadrants::AdjustIiField(bool left, int direction) {
  if (!direction || ii_change.Pending()) return;
  if (left) {
    const int index = config_cursor - IIMAP1;
    if (!ii_edit || config_page != II_MAPS_PAGE || index < 0 || index >= 32) return;
    const auto type = phz::ii::virtual_inputs.DirectionalMappings::Get(index).direction;
    ii_field = type == phz::ii::DirectionalMappings::Direction::Input ?
      constrain(ii_field + (direction > 0 ? 1 : -1), 0, 1) : 0;
  } else if (ii_field == 0) AdjustIiDirection(direction);
  else AdjustIiRange(direction);
}

FLASHMEM void AppQuadrants::AdjustIiOutput(int direction) {
  if (!direction || !isEditing || !IiReady() || !OC::global_settings.ii_address.enabled() ||
      view_state != APPLET_FULLSCREEN || (zoom_cursor != 5 && zoom_cursor != 6) ||
      zoom_slot < 0 || zoom_slot >= APPLET_SLOTS || config_page > HIDE_CONFIG ||
      preset_cursor || clock_overlay || HS::q_edit || HS::midi_edit || ii_change.Pending()) return;
  const uint16_t producer = zoom_slot * 2 + zoom_cursor - 4;
  const auto current = phz::ii::virtual_inputs.OutputMappingFor(producer);
  const auto next = phz::ii::NextMapping(phz::ii::virtual_inputs, current, direction,
      phz::ii::DirectionalMappings::Direction::Output, producer);
  if (next == current) return;
  phz::ii::Transition transition(false);
  bool changed;
  { phz::ii::InterruptLock lock;
    changed = phz::ii::virtual_inputs.RouteOutput(producer, next, 2 * APPLET_SLOTS);
  }
  if (changed && IiReady()) transition.Complete();
}

FLASHMEM void AppQuadrants::AdjustIiDirection(int direction) {
  if (!direction || ii_change.Pending() || !IiReady() || preset_cursor ||
      clock_overlay || HS::q_edit || HS::midi_edit) return;
  const int index = config_cursor - IIMAP1;
  if (!ii_edit || config_page != II_MAPS_PAGE || index < 0 || index >= 32) return;
  const auto route = phz::ii::virtual_inputs.DirectionalMappings::Get(index);
  const int target = constrain(int(route.direction) + (direction > 0 ? 1 : -1), 0, 2);
  if (target == int(route.direction)) return;
  ii_from = unsigned(route.direction);
  ii_change.Begin(index, target, HS::CVInputMap::CountIiBindings(index), route.producer,
                  HS::CVInputMap::IiBindingIdentity(index));
  // Empty mappings need no destructive confirmation; retain the same guarded
  // commit/revalidation path so newly appearing bindings still require approval.
  if (!ii_change.Users() && !route.producer) ConfirmIiDirection();
}

FLASHMEM void AppQuadrants::ConfirmIiDirection() {
  if (!ii_change.Pending()) return;
  const int index = ii_change.Slot();
  if (!IiReady() || !ii_edit || config_page != II_MAPS_PAGE ||
      config_cursor != IIMAP1 + index || preset_cursor || clock_overlay || HS::q_edit || HS::midi_edit) {
    ii_change.Cancel(); return;
  }
  const auto route = phz::ii::virtual_inputs.DirectionalMappings::Get(index);
  const size_t count = HS::CVInputMap::CountIiBindings(index);
  if (unsigned(route.direction) == ii_change.Target()) { ii_change.Cancel(); return; }
  const uint64_t identity = HS::CVInputMap::IiBindingIdentity(index);
  if (ii_from != unsigned(route.direction) || !ii_change.Matches(count, route.producer, identity)) {
    ii_from = unsigned(route.direction);
    ii_change.Refresh(count, route.producer, identity); return;
  }
  const auto target = phz::ii::DirectionalMappings::Direction(ii_change.Target());
  ii_change.Cancel();
  ii_field = 0;
  phz::ii::Transition transition(false);
  HS::CVInputMap::VisitIiBindings(index, [](HS::CVInputMap& map) {
    phz::ii::InterruptLock lock; map.SetSource(0);
  });
  bool changed;
  { phz::ii::InterruptLock lock;
    if (route.producer) phz::ii::virtual_inputs.AssignOutput(index, 0, 2 * APPLET_CURSOR_COUNT);
    changed = phz::ii::virtual_inputs.SetDirection(index, target, 0);
  }
  if (changed && IiReady()) transition.Complete();
}

FLASHMEM void AppQuadrants::DrawIiMaps() const {
  static const char ii_text_0[] PROGMEM = "<    ii Maps      >";
  static const char ii_text_1[] PROGMEM = "Change ii direction?";
  static const char ii_text_2[] PROGMEM = "Clear inputs: ";
  static const char ii_text_3[] PROGMEM = "Unbind output too";
  static const char ii_text_4[] PROGMEM = "L:Cancel  R:Confirm";
  static const char ii_text_5[] PROGMEM = "A:Prev   I    B:Next";
  static const char ii_text_6[] PROGMEM = "L:field R:edit Push:X";
  static const char ii_text_7[] PROGMEM = "Units: TT CV";
  gfxHeader(ii_text_0);
  static const uint8_t icon[8] = {0, 0x7a, 0, 0, 0x7a, 0, 0, 0};
  for (int i = 0; i < 32; ++i) {
    const int x = 1 + (i % 8) * 16;
    const int y = 12 + (i / 8) * 13;
    phz::ii::VirtualInputs::Snapshot snap;
    { phz::ii::InterruptLock lock; snap = phz::ii::virtual_inputs.Get(i); }
    phz::ii::DirectionalMappings::Snapshot route;
    { phz::ii::InterruptLock lock; route = phz::ii::virtual_inputs.DirectionalMappings::Get(i); }
    if (route.direction != phz::ii::DirectionalMappings::Direction::Off) gfxIcon(x, y, icon);
    if (route.direction == phz::ii::DirectionalMappings::Direction::Output) gfxInvert(x, y, 9, 9);
    if (snap.received) {
      const int h = int(snap.code) * 8 / 65535;
      if (h > 0) gfxLine(x + 9, y + 8, x + 9, y + 8 - h);
    }
  }
  const int index = constrain(config_cursor - IIMAP1, 0, 31);
  gfxIcon(constrain(9 + (index % 8) * 16, 0, 120), 12 + (index / 8) * 13, LEFT_ICON);
  if (!ii_edit) return;
  if (ii_change.Pending()) {
    graphics.clearRect(0, 0, 128, 64);
    gfxHeader(ii_text_1);
    static const char* const choices[] = {"Off", "Input", "Output"};
    gfxPrint(2, 16, "To: "); gfxPrint(choices[ii_change.Target()]);
    gfxPrint(2, 29, ii_text_2); gfxPrint(int(ii_change.Users() > 999 ? 999 : ii_change.Users()));
    gfxPrint(2, 42, ii_text_3);
    gfxFooter(ii_text_4);
    return;
  }
  graphics.clearRect(0, 0, 128, 10);
  gfxHeader(ii_text_5);
  gfxPrint(60, 1, int(index + 1));
  gfxFooter(ii_text_6);
  graphics.clearRect(14, 17, 100, 37);
  graphics.drawFrame(15, 18, 98, 35);
  phz::ii::VirtualInputs::Snapshot selected;
  phz::ii::DirectionalMappings::Snapshot route;
  uint16_t output_code = 0;
  bool output_valid = false;
  { phz::ii::InterruptLock lock;
    selected = phz::ii::virtual_inputs.Get(index);
    route = phz::ii::virtual_inputs.DirectionalMappings::Get(index);
    output_valid = phz::ii::virtual_inputs.ReadVoltage(index + 1, output_code);
  }
  static const char* const directions[] = {"Off", "Input", "Output"};
  gfxPrint(19, 21, "Type: "); gfxPrint(directions[static_cast<unsigned>(route.direction)]);
  static const char* const ranges[] = {"Off", "0..5V", "-5..5V", "0..10V"};
  if (route.direction == phz::ii::DirectionalMappings::Direction::Output)
    gfxPrint(19, 32, ii_text_7);
  else {
    gfxPrint(19, 32, "Range: "); gfxPrint(ranges[static_cast<unsigned>(route.direction == phz::ii::DirectionalMappings::Direction::Off ? phz::ii::VirtualInputs::Range::Off : route.range)]);
  }
  gfxPrint(19, 44, "Code: ");
  if (output_valid) gfxPrint(output_code >= 32768 ? int(output_code) - 65536 : int(output_code));
  else if (selected.received) gfxPrint(int(selected.code)); else gfxPrint("--");
  gfxLine(19, ii_field == 1 && route.direction == phz::ii::DirectionalMappings::Direction::Input ? 41 : 30,
          107, ii_field == 1 && route.direction == phz::ii::DirectionalMappings::Direction::Input ? 41 : 30);
  gfxInvert(14, 17, 100, 37);
}

FLASHMEM void AppQuadrants::ConfigEncoderAction(int h, int dir) {
        if (!isEditing && !preset_cursor) {
          if (h == 0) { // change pages
            config_page += dir;
            config_page = constrain(config_page, LOADSAVE_POPUP, LAST_PAGE);

            const int cursorpos[] = { 0, LOAD_PRESET, TRIG_LENGTH, TRIGMAP1, QUANT1, MIDIMAP1, IIMAP1, SHOWHIDELIST };
            config_cursor = cursorpos[config_page];
          } else if (config_page == SHOWHIDE_APPLETS) {
            showhide_cursor.Scroll(dir);
          } else { // move cursor
            config_cursor = constrain(config_cursor + dir, LOAD_PRESET, MAX_CURSOR);

            SetConfigPageFromCursor();
          }
          ResetCursor();
          if (config_cursor > RANDOMIZE_PRESET) HS::popup_tick = 0;
          return;
        }

        if (preset_cursor && isEditing) {
          // bank select
          bank_num = constrain(bank_num + dir, 0, 99);
          return;
        }

        switch (config_cursor) {
        case TRIGMAP1:
        case TRIGMAP2:
        case TRIGMAP3:
        case TRIGMAP4:
            if (!EditSelectedInputMap(dir))
              HS::trigmap[config_cursor-TRIGMAP1].ChangeSource(dir);
            break;
        case CVMAP1:
        case CVMAP2:
        case CVMAP3:
        case CVMAP4:
            if (!EditSelectedInputMap(dir))
              HS::cvmap[config_cursor-CVMAP1].ChangeSource(dir);
            break;
        case TRIGMAP5:
        case TRIGMAP6:
        case TRIGMAP7:
        case TRIGMAP8:
            if (!EditSelectedInputMap(dir))
              HS::trigmap[config_cursor-TRIGMAP5 + 4].ChangeSource(dir);
            break;
        case CVMAP5:
        case CVMAP6:
        case CVMAP7:
        case CVMAP8:
            if (!EditSelectedInputMap(dir))
              HS::cvmap[config_cursor-CVMAP5 + 4].ChangeSource(dir);
            break;

        case TRIG_LENGTH:
            HS::trig_length = (uint32_t) constrain( int(HS::trig_length + dir), 1, 127);
            break;
        case PRESET_JUMP_TRIG:
            if (!EditSelectedInputMap(dir))
              jump_trig_.ChangeSource(dir);
            break;
        case MIDI_BEND_RANGE:
            HS::frame.MIDIState.bend_range =
              constrain(HS::frame.MIDIState.bend_range + dir, 0, 36);
            break;
        case MIDI_PC_CHANNEL:
            HS::frame.MIDIState.pc_channel =
              constrain(HS::frame.MIDIState.pc_channel + dir, 0, 17);
            break;
        case MIDI_POLY_MODE:
            HS::frame.MIDIState.poly_mode =
              constrain(HS::frame.MIDIState.poly_mode + dir, 0, HS::MIDIPolyMode::POLY_LAST);
            break;
        case SCREENSAVER_MODE:
            HS::screensaver_mode = constrain(HS::screensaver_mode + dir, 0, SCREENSAVER_MODE_COUNT - 1);
            break;

        case DELETE_PRESET:
        case LOAD_PRESET:
        case SAVE_PRESET:
            if (h == 0) {
              config_cursor = constrain(config_cursor + dir, DELETE_PRESET, SAVE_PRESET);
            } else {
              preset_cursor = constrain(preset_cursor + dir, 1, QUAD_PRESET_COUNT);
            }
            break;
        }
    }

// Display work runs from the UI loop, not the control/command critical path.
FLASHMEM void AppQuadrants::View() const {
        bool draw_applets = true;

        if (preset_cursor) {
          DrawPresetSelector();
          draw_applets = false;
        }
        else if (config_page > HIDE_CONFIG) {
          switch(config_page) {
          default:
          case LOADSAVE_POPUP:
            PokePopup(MENU_POPUP);
            // but still draw the applets
            break;

          case MIDI_MAPS_PAGE:
            DrawMidiMaps(config_cursor - MIDIMAP1);
            draw_applets = false;
            break;

          case II_MAPS_PAGE:
            DrawIiMaps();
            draw_applets = false;
            break;

          case INPUT_SETTINGS:
            DrawInputMappings();
            draw_applets = false;
            break;

          case QUANTIZER_SETTINGS:
            DrawQuantizerConfig();
            draw_applets = false;
            break;

          case CONFIG_SETTINGS:
            DrawConfigMenu();
            draw_applets = false;
            break;

          case SHOWHIDE_APPLETS:
            DrawAppletList();
            draw_applets = false;
            break;
          }
        }
        if (HS::q_edit)
          PokePopup(QUANTIZER_POPUP);
        else if (HS::midi_edit)
          PokePopup(MIDI_POPUP);

        if (draw_applets) {
          if (view_state == AUDIO_SETUP) {
            audio_app.View();

            draw_applets = false;
          }
        }

        if (draw_applets) {
          if (view_state == APPLET_FULLSCREEN) {
            DrawFullScreen();
          } else if (view_state == OVERVIEW) {
            DrawOverview();
          } else {
            // only two applets visible at a time
            for (int h = 0; h < 2; h++)
            {
                HEM_SIDE slot = HEM_SIDE(h + view_slot[h]*2);
                active_applet[slot]->BaseView();

                // Applets 3 and 4 get inverted titles
                if (slot > 1) gfxInvert(0 + h*64, 0, 63, 10);
            }

            // vertical separator
            graphics.drawLine(63, 0, 63, 63, 2);
          }
        }

        // Clock setup is an overlay
        if (clock_overlay) {
          ClockSetup_instance.View();
        } else {
          ClockSetup_instance.DrawIndicator(view_state == OVERVIEW);
        }

        // Overlay popup window last
        if (OC::CORE::ticks - HS::popup_tick < HEMISPHERE_CURSOR_TICKS * 4) {
          HS::DrawPopup(config_cursor, preset_id, CursorBlink());
        }
    }

// Preset serialization is main-loop/menu work, not DSP/ISR code.
FLASHMEM __attribute__((noinline))
void AppQuadrants::store_to_preset(int id) {
        for(int slot=0;slot<4;++slot)RecallCapture(slot);
        if(!RecallSave())return; // explicit save also flushes pending ii defaults
        preset_id = id;
        // preset id is upper 5 bits - 32 presets per bank
        uint16_t preset_key = id << 11;

        // clock data
        clock_data = ClockSetup_instance.OnDataRequest();
        PhzConfig::setValue(preset_key | CLOCK_DATA_KEY, clock_data);

        // vague globals
        global_data = ClockSetup_instance.GetGlobals();
        PhzConfig::setValue(preset_key | GLOBALS_KEY, global_data);

        uint64_t data = 0;
        // Input-only records remain legacy-compatible. Output routes are tagged;
        // neither received CV nor published output values are persisted.
        for (size_t i = 0; i < phz::ii::VirtualInputs::kCount; ++i) {
          uint64_t record = 0;
          { phz::ii::InterruptLock lock;
            if (!phz::ii::PresetMappingRecord(phz::ii::virtual_inputs, i, record)) return;
          }
          PhzConfig::setValue(preset_key | (phz::ii::kVirtualRangeKey + i),
                             record);
        }
        // Input Mappings
        for (size_t i = 0; i < ADC_CHANNEL_COUNT/4; ++i) {
          data = PackPackables(HS::trigmap[i*4], HS::trigmap[i*4+1]);
          PhzConfig::setValue(preset_key | (TRIGMAP_KEY + i*2), data);
          data = PackPackables(HS::trigmap[i*4+2], HS::trigmap[i*4+3]);
          PhzConfig::setValue(preset_key | (TRIGMAP_KEY + i*2 + 1), data);

          data = PackPackables(HS::cvmap[i*4], HS::cvmap[i*4+1], HS::cvmap[i*4+2], HS::cvmap[i*4+3]);
          PhzConfig::setValue(preset_key | (CVMAP_KEY + i), data);
        }
        PhzConfig::deleteKey(preset_key | OLD_TRIGMAP_KEY);
        PhzConfig::deleteKey(preset_key | (OLD_TRIGMAP_KEY + 1));

        data = 0;
        for (size_t i = 0; i < 8; ++i) {
          Pack(data, PackLocation{i*8, 8}, HS::frame.clockinskip[i]);
        }
        PhzConfig::setValue(preset_key | INSKIP_KEY, data);
        data = 0;
        for (size_t i = 0; i < 8; ++i) {
          Pack(data, PackLocation{i*8, 8}, HS::frame.clockoutskip[i]);
        }
        PhzConfig::setValue(preset_key | OUTSKIP_KEY, data);
        data = 0;
        for (size_t i = 0; i < 8; ++i) {
          Pack(data, PackLocation{i*8, 8}, HS::frame.output_slew[i]);
        }
        PhzConfig::setValue(preset_key | OUTSLEW_KEY, data);
        data = 0;
        for (size_t i = 0; i < 8; ++i) {
          Pack(data, PackLocation{i*8, 8}, static_cast<uint8_t>(HS::frame.output_atten[i]));
        }
        PhzConfig::setValue(preset_key | OUTATTEN_KEY, data);
        { phz::ii::InterruptLock lock; data = HS::frame.output_routes.Encode(); }
        PhzConfig::setValue(preset_key | phz::routing::kOutputRoutingKey, data);

        data = 0;
        for (size_t h = 0; h < APPLET_SLOTS; h++)
        {
            int index = active_applet_index[h];
            Pack(data, PackLocation{h*8,8}, HS::appletIds[index]);

            // applet data
            applet_data[h] = HS::get_applet(index, HEM_SIDE(h))->OnDataRequest();
            PhzConfig::setValue(preset_key | (APPLET_L1_DATA_KEY + h), applet_data[h]);
        }

        // applet ids, and maybe some other stuff?
        PhzConfig::setValue(preset_key | APPLET_METADATA_KEY, data);

        // applet filtering is actually just global
        PhzConfig::setValue(FILTERMASK1_KEY, HS::hidden_applets[0]);
        PhzConfig::setValue(FILTERMASK2_KEY, HS::hidden_applets[1]);

        data = PackPackables(
          HS::frame.MIDIState.pc_channel,
          HS::frame.MIDIState.bend_range,
          midi_thru_disable,
          midi_clkrx_disable,
          midi_clktx_disable,
          midi_msgrx_disable,
          midi_msgtx_disable
        );
        PhzConfig::setValue(MIDI_GLOBALS_KEY, data);

        data = PackPackables(jump_trig_);
        PhzConfig::setValue(PRESET_JUMP_KEY, data);

        // Global quantizer settings
        for (size_t qslot = 0; qslot < QUANT_CHANNEL_COUNT; ++qslot) {
          /*
            // XXX: fine-tuning stuff from Calibr8or that should also be global
            int8_t offset;
            int16_t scale_factor; // precision of 0.01% as an offset from 100%
            int8_t transpose; // in semitones
          */
          auto &q = q_engine[qslot];
          const uint8_t range_low =
              q.range.enabled() ? uint8_t(q.range.low() + 9) : 0;
          const uint8_t range_high =
              q.range.enabled() ? uint8_t(q.range.high() + 9) : 0;
          data = PackPackables(
              q.scale,
              q.octave,
              q.root_note,
              q.mask,
              range_low,
              range_high
              );
          PhzConfig::setValue(Q_ENGINE_KEY + qslot, data);
        }

        // Global MIDI Maps
        for (size_t midx = 0; midx < MIDIMAP_MAX; ++midx) {
          data = PackPackables(frame.MIDIState.mapping[midx]);
          PhzConfig::setValue(MIDI_MAPS_KEY + midx, data);
        }

        // User Patterns aka Sequences
        for (size_t i = 0; i < OC::Patterns::PATTERN_USER_COUNT; ++i) {
          data = 0;
          for (size_t step = 0; step < ARRAY_SIZE(OC::Pattern::notes); ++step) {
            Pack(data, PackLocation{(step & 0x3)*16, 16}, (uint16_t)OC::user_patterns[i].notes[step]);
            if ((step & 0x3) == 0x3) {
              PhzConfig::setValue(SEQUENCES_KEY + ((i << 2) | (step >> 2)), data);
              data = 0;
            }
          }
        }

        audio_app.SavePreset(id);

        // SavePreset may instantiate cached audio applets: collect AFTER it.
        // Include every live mapping, not only the visible applets.
        const uint64_t usage = phz::ii::IiUsageRecord(HS::CVInputMap::IiUsageMask());
        PhzConfig::setValue(preset_key | phz::ii::kIiUsageKey, usage);
        if (!phz::ii::ValidateIiUsage(
              [usage](uint16_t, uint64_t& value) { value = usage; return true; },
              phz::ii::virtual_inputs)) {
          PokePopup(HS::MESSAGE_POPUP, "ii map conflict");
          return; // no disk write or success popup; next save rebuilds records
        }

        bool success = false;
        if (SDcard_Ready)
          success = PhzConfig::save_config(bank_filename, SD);
        else
          success = PhzConfig::save_config(bank_filename);

        if (success) {
          quantizer_dirty_ = false;
          PokePopup(HS::MESSAGE_POPUP, HS::PRESET_SAVED);
        }
    }

FLASHMEM __attribute__((noinline))
phz::ii::HostActionResult AppQuadrants::load_from_preset(int id) {
    using Result = phz::ii::HostActionResult;
        if (id < 0 || id >= QUAD_PRESET_COUNT) return Result::Rejected;

        uint16_t preset_key = id << 11;
        uint64_t data;

        // applet ids + misc
        if (!PhzConfig::getValue(preset_key | APPLET_METADATA_KEY, data)) return Result::Rejected;
        if (!data) return Result::Rejected;

        uint64_t staged_output_routes;
        if (!phz::routing::StageOutputRouting<DAC_CHANNEL_COUNT>(
              [preset_key](uint16_t key, uint64_t& value) {
                return PhzConfig::getValue(preset_key | key, value);
              }, staged_output_routes)) {
          PokePopup(HS::MESSAGE_POPUP, "Invalid output routes");
          return Result::Rejected;
        }

        phz::ii::DirectionalMappings staged_ii;
        HS::CVInputMap staged_cv[ADC_CHANNEL_COUNT];
        if (!phz::ii::StageDirectionalPreset(
              [preset_key](uint16_t key, uint64_t& value) {
                return PhzConfig::getValue(preset_key | key, value);
              }, CVMAP_KEY, 2 * APPLET_CURSOR_COUNT,
              HS::cvmap, staged_cv, staged_ii)) return Result::Rejected;
        if (!phz::ii::ValidateDigitalPreset(
              [preset_key](uint16_t key, uint64_t& value) {
                return PhzConfig::getValue(preset_key | key, value);
              }, TRIGMAP_KEY, OLD_TRIGMAP_KEY, HS::trigmap, staged_ii)) return Result::Rejected;

        // Validate required records and identities before changing any occupant.
        int indices[APPLET_SLOTS];
        uint64_t values[APPLET_SLOTS], saved_clock;
        if (!PhzConfig::getValue(preset_key | CLOCK_DATA_KEY, saved_clock)) return Result::Rejected;
        for (size_t h = 0; h < APPLET_SLOTS; ++h) {
          const auto type = Unpack(data, PackLocation{h*8, 8});
          indices[h] = HS::get_applet_index_by_id(type);
          if (HS::appletIds[indices[h]] != type ||
              !PhzConfig::getValue(preset_key | (APPLET_L1_DATA_KEY + h), values[h])) return Result::Rejected;
        }
        phz::ii::Transition transition(true);
        struct RecallRestore {
          bool& flag; bool previous;
          RecallRestore(bool& f):flag(f),previous(f){flag=true;}
          ~RecallRestore(){flag=previous;}
        } recall_restore(ii_recall_restoring);
        // Prevent one DSP frame from mixing old routes with new occupants.
        // Do not mask USB/timer interrupts for the whole restore operation.
        struct PauseOutputProcessing {
          bool previous;
          PauseOutputProcessing() : previous(OC::CORE::app_isr_enabled) {
            OC::CORE::app_isr_enabled = false;
          }
          ~PauseOutputProcessing() { OC::CORE::app_isr_enabled = previous; }
        } pause_output_processing;
        HS::frame.MIDIState.StopAutoOutput();
        // Restorers must see the destination directions, not the old preset.
        // Ingress remains closed by the outer transition; failure stays closed.
        { phz::ii::InterruptLock lock; phz::ii::virtual_inputs = staged_ii; }
        for (size_t h = 0; h < APPLET_SLOTS; ++h) {
          SetApplet(HEM_SIDE(h), indices[h]);
          auto* target = active_applet[h];
          if (!target || active_applet_index[h] != indices[h]) return Result::Failed;
          applet_data[h] = values[h];
          target->OnDataReceive(applet_data[h]);
        }

        // clock data
        clock_data = saved_clock;
        ClockSetup_instance.OnDataReceive(clock_data);
        // if the first key exists, we are assuming the rest are present...

        // vague globals
        PhzConfig::getValue(preset_key | GLOBALS_KEY, global_data);
        ClockSetup_instance.SetGlobals(global_data);

        // Input Mappings
        if (PhzConfig::getValue(preset_key | TRIGMAP_KEY, data)) {
          for (size_t i = 0; i < ADC_CHANNEL_COUNT/2; ++i) {
            UnpackPackables(data, HS::trigmap[i*2], HS::trigmap[i*2+1]);
            if (!PhzConfig::getValue(preset_key | (TRIGMAP_KEY + i+1), data)) break;
          }
        } else if (PhzConfig::getValue(preset_key | OLD_TRIGMAP_KEY, data)) {
          // migrate from v1.x
          uint16_t mapdata[4];
          UnpackPackables(data, mapdata[0], mapdata[1], mapdata[2], mapdata[3]);
          HS::trigmap[0].Unpack(mapdata[0]);
          HS::trigmap[1].Unpack(mapdata[1]);
          HS::trigmap[2].Unpack(mapdata[2]);
          HS::trigmap[3].Unpack(mapdata[3]);
          PhzConfig::getValue(preset_key | (OLD_TRIGMAP_KEY + 1), data);
          UnpackPackables(data, mapdata[0], mapdata[1], mapdata[2], mapdata[3]);
          HS::trigmap[4].Unpack(mapdata[0]);
          HS::trigmap[5].Unpack(mapdata[1]);
          HS::trigmap[6].Unpack(mapdata[2]);
          HS::trigmap[7].Unpack(mapdata[3]);
        }

        for (size_t i = 0; i < ADC_CHANNEL_COUNT; ++i) HS::cvmap[i] = staged_cv[i];

        data = 0;
        PhzConfig::getValue(preset_key | INSKIP_KEY, data);
        for (size_t i = 0; i < 8; ++i) {
          HS::frame.clockinskip[i] = Unpack(data, PackLocation{i*8, 8});
        }

        PhzConfig::getValue(preset_key | OUTSKIP_KEY, data);
        for (size_t i = 0; i < 8; ++i)
        {
          HS::frame.clockoutskip[i] = Unpack(data, PackLocation{i*8, 8});
        }

        PhzConfig::getValue(preset_key | OUTSLEW_KEY, data);
        for (size_t i = 0; i < 8; ++i)
        {
          HS::frame.output_slew[i] = Unpack(data, PackLocation{i*8, 8});
        }

        const bool has_output_atten = PhzConfig::getValue(preset_key | OUTATTEN_KEY, data);
        for (size_t i = 0; i < 8; ++i)
        {
          HS::frame.output_atten[i] = has_output_atten ? Unpack(data, PackLocation{i*8, 8}) : 60;
        }

        //LoadGlobals();

        audio_app.LoadPreset(id);
        { phz::ii::InterruptLock lock;
          if (!HS::frame.output_routes.Restore(staged_output_routes)) return Result::Failed;
        }
        preset_id = id;
        PokePopup(PRESET_POPUP);
    if (IiReady()) {
      quantizer_dirty_ = false;
      RecallBaseline(); ii_loaded_setup.PresetLoaded(id);
      transition.Complete(); return Result::Applied;
    }
    return Result::Failed;
  }

// Executed only by the main-loop host-action adapter with app DSP paused.
FLASHMEM phz::ii::HostActionResult AppQuadrants::ApplyIiHostAction(const phz::ii::HostAction& action) {
  using Result = phz::ii::HostActionResult;
  // Do not interrupt a local editor, chooser, or pending preset operation.
  if (!IiReady() || config_page != HIDE_CONFIG || preset_cursor ||
      clock_overlay || HS::q_edit || HS::midi_edit || queued_preset >= 0 ||
      select_mode >= 0) return Result::Rejected;
  if (action.kind == phz::ii::HostActionKind::LoadPreset && isEditing) return Result::Rejected;
  Result result = Result::Rejected;
  if (action.kind == phz::ii::HostActionKind::SelectApplet) {
    if (action.target < 1 || action.target > 4 || !action.applet) return result;
    int index = -1;
    for (int i=0;i<HS::HEMISPHERE_AVAILABLE_APPLETS;++i)
      if (HS::appletIds[i] == action.applet) { index=i;break; }
    if (index < 0) return result; // Never use get-index fallback for unknown IDs.
    const auto slot = HEM_SIDE(action.target-1);
    if (!HS::get_applet(index,slot)) return result; // Allocate before live mutation.
    SetApplet(slot,index);
    result = active_applet_index[slot] == index && IiReady() ? Result::Applied : Result::Failed;
  } else if (action.kind == phz::ii::HostActionKind::LoadPreset) {
    if (action.target >= QUAD_PRESET_COUNT) return result;
    result = load_from_preset(action.target);
  }
  if (result == Result::Applied) {
    output_pending=false;ii_edit=false;ii_change.Cancel();
    isEditing=false;
    ClearEditInputMap();ExitFullScreen();
  }
  return result;
}

FLASHMEM __attribute__((noinline)) void AppQuadrants::SetApplet(HEM_SIDE hemisphere, int index) {
    phz::ii::Transition transition(false);
        if (hemisphere < 0 || hemisphere >= APPLET_SLOTS ||
            index < 0 || index >= HS::HEMISPHERE_AVAILABLE_APPLETS) return;
        HemisphereApplet* next_ = HS::get_applet(index, hemisphere);
        if (!next_) return;
        struct Pause { bool previous=OC::CORE::app_isr_enabled;
          Pause(){OC::CORE::app_isr_enabled=false;}
          ~Pause(){OC::CORE::app_isr_enabled=previous;} } pause;
        if (!ii_recall_restoring) RecallCapture(int(hemisphere));
        HS::frame.MIDIState.StopAutoOutput(int(hemisphere));
        { phz::ii::InterruptLock lock;
          if (!HS::frame.output_routes.InvalidateProposals()) return;
        }
        HemisphereApplet* old_ = active_applet[hemisphere];
        next_->BaseStart(hemisphere);
        // make sure we've called Start before changing the shared pointer
        active_applet[hemisphere] = next_;
        // unload previous applet after swapping
        if (old_ && old_ != next_) old_->Unload();
        next_applet_index[hemisphere] = active_applet_index[hemisphere] = index;
        if (!ii_recall_restoring) RecallSwitch(int(hemisphere),old_);

    transition.Complete();
  }

#include "QuadrantsRecall.h"
