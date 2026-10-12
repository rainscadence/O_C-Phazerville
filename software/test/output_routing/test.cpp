#include "routing/OutputRouting.h"
#include "routing/OutputPreset.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>

template <size_t N> void Check() {
  using Routes = phz::routing::OutputRouting<N>;
  Routes routes;
  const uint64_t initial = routes.Encode();
  uint64_t staged = 123;
  assert(phz::routing::StageOutputRouting<N>([](uint16_t key, uint64_t&) {
    assert(key == 73); return false;
  }, staged));
  assert(staged == initial);
  assert(!phz::routing::StageOutputRouting<N>([](uint16_t, uint64_t& r) {
    r = 0; return true;
  }, staged));
  assert(staged == initial && routes.Encode() == initial);
  int32_t input[N]{}, output[N]{};
  for (size_t i = 0; i < N; ++i) {
    input[i] = i % 2 ? INT32_MIN + int(i) : INT32_MAX - int(i);
    assert(routes.Destination(i) == i + 1 && routes.MidiEnabled(i));
  }
  routes.Render(input, output);
  for (size_t i = 0; i < N; ++i) assert(input[i] == output[i]);
  assert(!routes.Propose(N, 1).Valid());
  assert(!routes.Propose(0, N+1).Valid());
  assert(!routes.Commit(routes.Propose(N, 0), true));
  assert(!routes.SetMidiEnabled(N, false));
  assert(!routes.MidiEnabled(N) && routes.Destination(N) == 0);
  assert(routes.Encode() == initial);

  // Cancellation = no commit; rejection never disconnects the current owner.
  if (N > 1) {
    const auto p = routes.Propose(0, 2);
    assert(p.Valid() && p.Conflict() && p.Owner() == 1);
    assert(!routes.Commit(p));
    assert(routes.Encode() == initial);
    Routes other;
    assert(!other.Commit(p, true));
    assert(routes.Commit(p, true));
    assert(routes.Destination(0) == 2 && routes.Destination(1) == 0);
    assert(!routes.Commit(p, true));
    routes.Render(input, output);
    assert(output[0] == 0 && output[1] == input[0]);
    assert(input[0] == INT32_MAX && input[1] == INT32_MIN + 1);
    // Alias regression: Qwen's first draft erased its input here.
    int32_t alias[N];
    for (size_t i = 0; i < N; ++i) alias[i] = input[i];
    routes.Render(alias, alias);
    for (size_t i = 0; i < N; ++i) assert(alias[i] == output[i]);
  }
  assert(routes.SetMidiEnabled(0, false));
  assert(!routes.MidiEnabled(0));
  assert(routes.Commit(routes.Propose(0, 0)));
  routes.Render(input, output);
  assert(routes.Destination(0) == 0 && routes.Owner(0) == -1);
  const uint64_t saved = routes.Encode();
  assert(phz::routing::StageOutputRouting<N>([saved](uint16_t, uint64_t& r) {
    r = saved; return true;
  }, staged));
  assert(staged == saved);
  assert(routes.Reset());
  assert(routes.Encode() == initial);
  assert(routes.Restore(saved) && routes.Encode() == saved);
  const auto stale = routes.Propose(0, 1);
  assert(routes.Reset() && routes.Restore(saved));
  assert(!routes.Commit(stale, true)); // ABA: same contents, newer lifecycle

  const uint64_t bad[] = {0, saved ^ (uint64_t(1) << 48),
    saved ^ (uint64_t(3) << 40), (saved & ~uint64_t(15)) | 15};
  for (auto record : bad) {
    assert(!routes.Restore(record));
    assert(routes.Encode() == saved);
  }
  if (N > 1) {
    assert(!routes.Restore((initial & ~uint64_t(255)) | 0x11));
    assert(routes.Encode() == saved);
  }
  if (N < 8) {
    assert(!routes.Restore(saved | (uint64_t(1) << (4*N))));
    assert(!routes.Restore(saved | (uint64_t(1) << (32+N))));
    assert(routes.Encode() == saved);
  }

  // Exercise every source/destination pair and assert global jack uniqueness.
  for (size_t s = 0; s < N; ++s) for (uint8_t d = 0; d <= N; ++d) {
    assert(routes.Reset());
    const auto p = routes.Propose(s, d);
    const uint64_t before = routes.Encode();
    if (p.Conflict()) { assert(!routes.Commit(p)); assert(routes.Encode() == before); }
    assert(routes.Commit(p, true));
    assert(routes.Destination(s) == d);
    routes.Render(input, output);
    for (uint8_t jack = 1; jack <= N; ++jack) {
      int owner = -1;
      for (size_t i = 0; i < N; ++i) if (routes.Destination(i) == jack) {
        assert(owner == -1); owner = int(i);
      }
      assert(output[jack-1] == (owner < 0 ? 0 : input[owner]));
    }
    const uint64_t encoded = routes.Encode();
    assert(routes.Restore(encoded) && routes.Encode() == encoded);
  }
}

int main() {
  Check<1>(); Check<2>(); Check<3>(); Check<4>();
  Check<5>(); Check<6>(); Check<7>(); Check<8>();
  puts("PASS: output routing, ownership, stale approvals, aliasing, codec, all 1..8-channel configurations");
}
