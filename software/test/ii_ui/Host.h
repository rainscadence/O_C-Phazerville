// Hardware boundary stubs for extracted, unchanged production method bodies.
#include "ii/AddressState.h"
#include "txi2c2midi/ModeState.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
using std::max;

namespace UI {
enum EventType { EVENT_ENCODER, EVENT_BUTTON_PRESS, EVENT_BUTTON_LONG_PRESS, EVENT_BUTTON_DOWN };
struct Event { EventType type; int control; int value = 0; };
struct Queue {
  std::deque<Event> events;
  unsigned idle = 0, pulled = 0;
  bool flood = false;
  bool available() const { return flood || !events.empty(); }
  Event PullEvent() {
    ++pulled;
    if (events.empty()) return {EVENT_ENCODER, 1, 0};
    auto event = events.front(); events.pop_front(); return event;
  }
  void Flush() { events.clear(); flood = false; }
  void Poke() { idle = 0; }
};
}
namespace menu {
constexpr int kIndentDx = 0, kMenuLineH = 10;
template<int N> struct ScreenCursor {
  int pos = 0, lo = 0, hi = 0;
  void Init(int l, int h) { lo = l; hi = h; pos = l; }
  void Scroll(int delta) { pos = std::clamp(pos + delta, lo, hi); }
  int cursor_pos() const { return pos; }
  int first_visible() const { return lo; }
  int last_visible() const { return hi; }
};
struct SettingsListItem {
  int x = 0, y = 0; bool selected = false;
  void SetPrintPos() {} void DrawCustom() {}
};
}
namespace weegfx { struct Graphics { static constexpr int kFixedFontW = 6; }; }
struct TestGraphics {
  int depth = 0, y = 0;
  std::map<int, std::string> lines;
  void begin() { assert(depth++ == 0); lines.clear(); }
  void end() { assert(--depth == 0); }
  void setPrintPos(int, int row) { y = row; }
  void movePrintPos(int, int) {}
  void print(const char* text) { lines[y] = text; }
  void drawBitmap8(int, int, int, int) {}
  void drawLine(int, int, int, int) {}
  void invertRect(int, int, int, int) {}
  bool contains(const char* text) const {
    for (const auto& row : lines) if (row.second.find(text) != std::string::npos) return true;
    return false;
  }
} graphics;
struct TestFrame {
  TestFrame() { graphics.begin(); }
  ~TestFrame() { graphics.end(); }
};
#define GRAPHICS_BEGIN_FRAME(wait) { TestFrame frame;
#define GRAPHICS_END_FRAME() }
#define APPS_SERIAL_PRINTLN(...) ((void)0)
constexpr int bitmap_indicator_4x8 = 0, ZAP_ICON = 0;
constexpr unsigned APP_SELECTION_TIMEOUT_MS = 30000, SETTINGS_SAVE_TIMEOUT_MS = 0;
unsigned MENU_REDRAW = 0;
void delay(int) {}
struct { void end() {} } FreqMeasure;

namespace OC {
enum { CONTROL_ENCODER_R = 1, CONTROL_BUTTON_R, CONTROL_BUTTON_L,
       CONTROL_BUTTON_UP, CONTROL_BUTTON_DOWN, CONTROL_ENCODER_L };
enum { APP_EVENT_RESUME };
namespace phz_settings_ui {
static const char app_menu[] = "Phazerville...";
static const char title[] = "Settings";
static const char update_hint[] = "Hold right to update";
static const char update_row[] = "Firmware Update";
static const char footer_save[] = "[BACK]        [SAVE]";
static const char footer_update[] = "[BACK]      [HOLD R]";
}
namespace CORE { bool app_isr_enabled = true; }
namespace DigitalInputs { void reInit() {} }
struct {
  int current_app_id = 0;
  bool encoders_enable_acceleration = false;
  phz::ii::AddressState ii_address;
  txi2c2midi::ModeState tx_mode;
} global_settings;
struct App {
  int id() const { return 0; }
  const char* name() const { return "app"; }
  void DispatchAppEvent(int) {}
};
struct {
  App app;
  int num_apps() const { return 3; }
  int IndexOfAppByID(int id) const { return id; }
  App& operator[](int) { return app; }
} app_container;
size_t AppMenuToContainerIndex(size_t index) { return index; }
size_t ContainerToAppMenuIndex(size_t index) { return index; }
struct {
  int changes = 0;
  App* current_app() { return &app_container.app; }
  void set_current_app(int) { ++changes; }
} app_switcher;
void SaveAppData() { assert(false && "ii page must not save/change the app"); }
void draw_save_message(int) {}
void EnterFirmwareUpdateMode() { assert(false && "test does not enter loader mode"); }
bool save_ok = true;
int save_count = 0;
bool SaveIiAddress() {
  ++save_count;
  global_settings.ii_address.Saved(save_ok);
  global_settings.tx_mode.Saved(save_ok);
  return save_ok;
}
class Ui {
 public:
  static constexpr size_t kEventQueueDepth = 16;
  UI::Queue event_queue_;
  bool ii_settings_open_ = false;
  int ii_settings_field_ = 0;
  const char* ii_settings_message_ = "";
  bool AppSettings(bool drawmenu);
  void IiSettings(@II_ARGS@);
  void SetButtonIgnoreMask() {}
  bool IgnoreEvent(const UI::Event&) { return false; }
  unsigned idle_time() const { return event_queue_.idle; }
  void encoders_enable_acceleration(bool) {}
  void DebugStats() { assert(false && "Back must not leak into parent DebugStats"); }
} ui;
}
