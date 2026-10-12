#include <cassert>
#include <initializer_list>
#include "../src/txi2c2midi/PreviewSession.h"
struct Bus {
  bool active;
  int stops = 0, restores = 0;
  bool Registered() { return active; }
  void Stop() { ++stops; active = false; }
  void Restore() { ++restores; active = true; }
};
int main() {
  for (bool enabled : {false, true}) {
    Bus bus{enabled};
    txi2c2midi::PreviewSession session;
    session.Leave(bus);
    assert(bus.stops == 0 && bus.restores == 0);
    for (int i = 0; i < 32; ++i) {
      session.Enter(bus);
      session.Enter(bus);
      assert(!bus.active && bus.stops == i + 1);
      session.Leave(bus);
      session.Leave(bus);
      assert(bus.active == enabled);
      assert(bus.restores == (enabled ? i + 1 : 0));
    }
  }
}
