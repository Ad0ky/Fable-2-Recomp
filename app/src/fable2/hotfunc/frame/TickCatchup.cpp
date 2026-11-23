// fable2-nx (2026-11-17, gameplay at the wrong speed): the setting behind tools/fable2_tick_catchup.py, and a count of
// the game's steps.
//
// Fable 2's main loop (sub_82355460) advances the game in steps of 1/30 s and runs at most 2 a pass; with more due it
// runs one and drops the rest of the time. fable2_tick_catchup sets that limit (2 = the game's own). The user's test
// with 4: the cutscene's animations no faster, only choppier; without the snow (fable2_diag_skip_ps) the cutscene ran
// at its speed at about the same frame rate. So the default stays the game's 2, and the steps are counted: every
// 10 s, how many steps the game ran (30 a second = the game at full speed), to see whether the snow slows the game's
// clock or something else.
#include "renut_pch.h"  // fable2-nx: the generated project is "renut"

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(fable2_tick_catchup, 2, "Fable2",
                     "Game steps (1/30 s each) the main loop may catch up in one pass before it drops time "
                     "(2 = the game's own)");

extern "C" int32_t fable2_tick_catchup_max() {
  static const int32_t n = [] {
    const int32_t v = REXCVAR_GET(fable2_tick_catchup);
    return v < 1 ? 1 : (v > 8 ? 8 : v);
  }();
  return n;
}

extern "C" void __imp__ProcessAndProcessAndProcess1292_821CCB20(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82355460(PPCContext& __restrict ctx, uint8_t* base);
extern std::atomic<uint64_t> g_banjo_game_frames;  // frames shown (D3DDevice_Swap_hook; katamari_keys.cpp)

namespace {
std::atomic<uint64_t> g_steps{0}, g_other_calls{0};
}

// The first call of each of the main loop's steps (sub_82355460, loc_823556F8); its other callers counted apart.
extern "C" void ProcessAndProcessAndProcess1292_821CCB20(PPCContext& __restrict ctx, uint8_t* base) {
  const uintptr_t from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  const uintptr_t loop = reinterpret_cast<uintptr_t>(&__imp__sub_82355460);
  if (from > loop && from < loop + 0x10000)
    g_steps.fetch_add(1, std::memory_order_relaxed);
  else
    g_other_calls.fetch_add(1, std::memory_order_relaxed);
  using clock = std::chrono::steady_clock;
  static clock::time_point last = clock::now();
  static uint64_t last_steps = 0, last_frames = 0;
  const clock::time_point now = clock::now();
  if (now - last >= std::chrono::seconds(10)) {
    const double secs = std::chrono::duration<double>(now - last).count();
    const uint64_t steps = g_steps.load(), frames = g_banjo_game_frames.load();
    REXLOG_INFO("[fable2] game clock: {} steps in {:.1f} s ({:.1f} a second, 30 = full speed), {} frames shown ({:.1f} "
                "a second); {} calls from elsewhere so far",
                steps - last_steps, secs, double(steps - last_steps) / secs, frames - last_frames,
                double(frames - last_frames) / secs, g_other_calls.load());
    last = now;
    last_steps = steps;
    last_frames = frames;
  }
  __imp__ProcessAndProcessAndProcess1292_821CCB20(ctx, base);
}
