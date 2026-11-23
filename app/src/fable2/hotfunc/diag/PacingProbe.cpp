// fable2-nx (2026-11-18): a probe for the game slowing down after the intro (logging only, fable2_pacing_report).
//
// The ring report (DeadlockProbe.cpp) of the run after the intro: the game writes about 40 KB a game update all along,
// the 3D Engine shows ~21 frames a second all along, yet it takes fewer game updates a frame as time goes on (2 a frame
// at first, 1 in 50 after three minutes), and the game waits for room in the full ring. So the 3D Engine chooses not
// to read. Its reader, ProcessAndProcessAndProcess1408_822E0508 (f1 = the time to show, r4 = the ring's index), takes
// the next packet only while the packet's time (the double at 0x8335F610 + 8 x index) is before the time to show.
// ProcessAndProcessAndProcess1389_8236C520 (the 3D Engine's frame) sets the time to show: now (the timebase less the
// double at 0x83496EC0, over the double at 0x83496EC8) less a latency (1 / the double at 0x83319510, two frames of
// the refresh at 0x83360194 over the interval at 0x83360190, and one refresh), or now less its object's + 596 when that
// is not negative; at most the object's + 160 less 1 ms.
//
// Every 2 s: the time to show, now, the packet times, and the frame's inputs, so the log shows which side runs off:
// the packets' times ahead of now (the game's clock) or the time to show behind (the 3D Engine's).
#include "renut_pch.h"  // fable2-nx: the generated project is "renut" (the base keeps its name)

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(fable2_pacing_report, false, "Fable2",
                    "Log every 2 s the 3D Engine's time to show against the game packets' times (the game slowing)");

extern "C" void __imp__ProcessAndProcessAndProcess1408_822E0508(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__ProcessAndProcessAndProcess1389_8236C520(PPCContext& __restrict ctx, uint8_t* base);

namespace {

double LoadF64(uint8_t* base, uint32_t address) {
  uint64_t v;
  std::memcpy(&v, REX_RAW_ADDR(address), 8);
  v = __builtin_bswap64(v);
  double d;
  std::memcpy(&d, &v, 8);
  return d;
}
float LoadF32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, REX_RAW_ADDR(address), 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
uint32_t LoadU32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, REX_RAW_ADDR(address), 4);
  return __builtin_bswap32(v);
}

double GuestNow(uint8_t* base) {
  const double tb = double(int64_t(REX_QUERY_TIMEBASE()));
  return (tb - LoadF64(base, 0x83496EC0u)) / LoadF64(base, 0x83496EC8u);
}

bool Enabled() {
  static const bool on = REXCVAR_GET(fable2_pacing_report);
  return on;
}

std::mutex g_mutex;
struct Window {
  uint64_t calls = 0, blocked = 0;
  double min_ahead = 1e30, max_ahead = -1e30;  // packet time less the time to show
  double last_show = 0, last_packet = 0, last_now = 0;
  uint32_t last_index = 0;
  // the 3D Engine's frame
  uint64_t frames = 0;
  double obj160 = 0, latency_const = 0;
  float obj596 = 0, refresh = 0;
  int32_t interval = 0;
} g_w;
int64_t g_last_report_ms = 0;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MaybeReport() {
  const int64_t now = NowMs();
  if (g_last_report_ms == 0) {
    g_last_report_ms = now;
    return;
  }
  if (now - g_last_report_ms < 2000)
    return;
  const double secs = double(now - g_last_report_ms) / 1000.0;
  g_last_report_ms = now;
  const Window w = g_w;
  g_w = Window{};
  REXLOG_INFO(
      "[pacing] {:.1f} s: {} 3D frames, {} reader calls ({} found the next packet not due); now {:.3f}, time to show "
      "{:.3f} (now less {:.3f} s), ring {} packet time {:.3f} (now {:+.3f} s); packet less time to show {:+.3f} to "
      "{:+.3f} s; frame inputs: +160 {:.3f} (now {:+.3f}), +596 {:.3f}, 1/latency {:.3f}, refresh {:.1f}, interval {}",
      secs, w.frames, w.calls, w.blocked, w.last_now, w.last_show, w.last_now - w.last_show, w.last_index,
      w.last_packet, w.last_packet - w.last_now, w.calls ? w.min_ahead : 0.0, w.calls ? w.max_ahead : 0.0, w.obj160,
      w.obj160 - w.last_now, double(w.obj596), w.latency_const, double(w.refresh), w.interval);
}

}  // namespace

extern "C" void ProcessAndProcessAndProcess1408_822E0508(PPCContext& __restrict ctx, uint8_t* base) {
  if (Enabled()) {
    const uint32_t index = ctx.r4.u32;
    const double show = ctx.f1.f64;
    const double packet = LoadF64(base, 0x8335F610u + 8u * index);
    const double now = GuestNow(base);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_w.calls++;
    if (!(ctx.r5.u32 & 0xFF) && packet >= show)
      g_w.blocked++;
    const double ahead = packet - show;
    if (ahead < g_w.min_ahead)
      g_w.min_ahead = ahead;
    if (ahead > g_w.max_ahead)
      g_w.max_ahead = ahead;
    g_w.last_show = show;
    g_w.last_packet = packet;
    g_w.last_now = now;
    g_w.last_index = index;
    MaybeReport();
  }
  __imp__ProcessAndProcessAndProcess1408_822E0508(ctx, base);
}

extern "C" void ProcessAndProcessAndProcess1389_8236C520(PPCContext& __restrict ctx, uint8_t* base) {
  if (Enabled()) {
    const uint32_t obj = ctx.r3.u32;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_w.frames++;
    if (obj) {
      g_w.obj160 = LoadF64(base, obj + 160);
      g_w.obj596 = LoadF32(base, obj + 596);
    }
    g_w.latency_const = LoadF64(base, 0x83319510u);
    g_w.refresh = LoadF32(base, 0x83360194u);
    g_w.interval = int32_t(LoadU32(base, 0x83360190u));
  }
  __imp__ProcessAndProcessAndProcess1389_8236C520(ctx, base);
}
