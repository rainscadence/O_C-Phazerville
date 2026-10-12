using namespace OC;
int background_visits = 0;
void Tick() {
  // Match Main.cpp: render in its frame, dispatch menu events, then USB service.
  { TestFrame frame; assert(ui.AppSettings(true)); }
  assert(ui.AppSettings(false));
  assert(graphics.depth == 0);
  ++background_visits;
}
void Send(UI::EventType type, int control, int value = 0) {
  ui.event_queue_.events.push_back({type, control, value}); Tick();
}
void Open() {
  Send(UI::EVENT_ENCODER, CONTROL_ENCODER_R, 99);
  Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R);
  Tick();
  assert(graphics.contains("Settings"));
  assert(save_count == 0);
  Send(UI::EVENT_ENCODER, CONTROL_ENCODER_R, 1); // select address field
  assert(ui.ii_settings_field_ == 1);
}
int main(int argc, char** argv) {
  assert(argc == 2);
  global_settings.ii_address.Boot(true, true, 0x145); // legacy enabled installation
  global_settings.tx_mode.Boot(true, false, 0);
  const std::string test = argv[1];
  Open();
  auto& address = global_settings.ii_address;
  if (test == "idle-capture") {
    const int before = background_visits;
    for (int i = 0; i < 100; ++i) {
      // The child page remains open beyond the parent menu's idle timeout.
      ui.event_queue_.idle = APP_SELECTION_TIMEOUT_MS + 100;
      Tick();
      assert(graphics.contains("Active: 0x45"));
      assert(graphics.contains("Pending: 0x45"));
    }
    assert(background_visits == before + 100);
    assert(address.active() == 0x45 && address.pending() == 0x45);
  } else if (test == "edit-save-back") {
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_L, 1); Tick();
    assert(graphics.contains("Pending: 0x46 *"));
    assert(address.active() == 0x45 && save_count == 0);
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R); Tick();
    assert(save_count == 1 && address.active() == 0x45);
    assert(address.reboot_required() && !address.dirty());
    assert(graphics.contains("Saved. Reboot to use"));
    ui.event_queue_.events.push_back({UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_L});
    ui.event_queue_.events.push_back({UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R});
    Tick(); Tick();
    assert(!graphics.contains("Settings"));
    assert(save_count == 1 && app_switcher.changes == 0);
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R); Tick();
    assert(graphics.contains("Reboot required"));
  } else if (test == "failure-reopen") {
    save_ok = false;
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_L, 2);
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R); Tick();
    assert(graphics.contains("SAVE FAILED"));
    assert(address.dirty() && !address.reboot_required());
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_L); Tick();
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R); Tick();
    assert(graphics.contains("Pending: 0x47 *"));
    assert(!graphics.contains("SAVE FAILED") && address.active() == 0x45);
  } else if (test == "bounds-and-budget") {
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_L, -999);
    assert(address.pending() == 8);
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_L, 999);
    assert(address.pending() == 0x77);
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_R, -999);
    Send(UI::EVENT_BUTTON_DOWN, CONTROL_BUTTON_R);
    Send(UI::EVENT_BUTTON_LONG_PRESS, CONTROL_BUTTON_R);
    assert(address.pending() == 0x77 && save_count == 0);
    ui.event_queue_.flood = true;
    const auto before = ui.event_queue_.pulled;
    Tick();
    assert(ui.event_queue_.pulled - before <= Ui::kEventQueueDepth);
  } else if (test == "disabled") {
    address.Boot(false, false, 0); Tick();
    assert(graphics.contains("Active: FAULT"));
    assert(!address.enabled() && save_count == 0);
  } else if (test == "opt-in") {
    address.FactoryDefaults(); Tick();
    assert(graphics.contains("Active: OFF") && graphics.contains("Enable: Off"));
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_R, -1);
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_L, 1); Tick();
    assert(graphics.contains("Enable: On *") && !address.enabled());
    save_ok = false;
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R); Tick();
    assert(graphics.contains("SAVE FAILED") && address.dirty() && !address.reboot_required());
    save_ok = true;
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R); Tick();
    assert(!address.enabled() && address.reboot_required() && !address.dirty());
    uint64_t record;
    assert(address.PendingRecord(record)); address.Boot(true, true, record); Tick();
    assert(address.enabled() && !address.reboot_required());
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_L, -1);
    Send(UI::EVENT_BUTTON_PRESS, CONTROL_BUTTON_R);
    assert(address.enabled());
    assert(address.PendingRecord(record)); address.Boot(true, true, record); Tick();
    assert(!address.enabled() && !address.faulted() && !address.reboot_required());
  } else if (test == "tx-mode") {
    auto& mode = global_settings.tx_mode;
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_R, 1);
    Send(UI::EVENT_ENCODER, CONTROL_ENCODER_L, 1); Tick();
    assert(mode.pending()==txi2c2midi::Mode::Legacy && mode.active()==txi2c2midi::Mode::Single);
    assert(graphics.contains("TX Mode: Legacy *"));
    save_ok=false; Send(UI::EVENT_BUTTON_PRESS,CONTROL_BUTTON_R);
    assert(mode.dirty() && !mode.reboot_required());
    save_ok=true; Send(UI::EVENT_BUTTON_PRESS,CONTROL_BUTTON_R);
    assert(!mode.dirty() && mode.reboot_required());
    assert(address.active()==0x45 && address.pending()==0x45);
    mode.Boot(true,true,mode.PendingRecord()); Tick();
    assert(mode.active()==txi2c2midi::Mode::Legacy && !mode.reboot_required());
  } else { assert(false); }
  assert(CORE::app_isr_enabled);
  printf("PASS: %s\n", argv[1]);
}
