// fable2-nx (2026-11-18, the game slowing down after the intro; THE FIX): run the GPU callbacks Fable writes into the
// ring itself, so the 3D Engine's clock follows the real time.
//
// The pacing probe (PacingProbe.cpp) of the run after the intro: the 3D Engine's "time to show" (it takes the game's
// next update only when that update's time is before it) moved by exactly 1/30 s a shown frame: 0.67 s a second at
// 20 frames a second, so after five minutes it was 156 s behind the real time, the ring full, and the game thread
// waiting for room: one game update every 2 s, with that much time in each.
//
// That clock is ProcessAndProcessAndProcess1107_82B69FA0: the time of the last frame shown (a record at 0x83491BD0,
// written by ProcessAndProcessAndProcess1267_82B89A78) plus a frame period (refresh 0x83360194 over interval
// 0x83360190: 1/30 s) for each frame submitted since (the count at 0x83496F44, +1 a Swap in
// ProcessAndProcessAndProcess1265_82B69488). The record is written by a GPU callback: at each Swap the 3D Engine calls
// ProcessAndProcessAndProcess952_821F5F20 (r3 the device, r4 the ring, r5 the type, r6 the callback, r7 its context),
// which writes register 0x57C (the callback and context) and a PM4 INTERRUPT (0x54) into the ring; on the console the
// GPU raises the interrupt there and D3D's handler calls the callback. reNut never reads the ring sink ("[renut] ring
// census": reg057C and op54, one a frame), so 82B89A78 never ran: the record stayed at the start, and the clock counted
// frames at 1/30 s each. At 30 frames a second that is the real time (the title and the intro were right); below 30
// it falls behind for good.
//
// Here the callback also goes to reNut's D3DDevice_InsertCallback queue (gpu/guest/callbacks.h): the render thread
// marks the point, and the game's next D3D call runs it, as reNut does for D3DDevice_InsertCallback. By default only the
// frame-time record (82B89A78); fable2_ring_callbacks_all = true runs the other callers' callbacks too (five more
// sites: 82B9C7F8, 8227F0B0, 821B9AD0, 82BA6F08, 82286D00; never run so far, a test). Each callback's first insertion
// is logged.
#include "renut_pch.h"  // fable2-nx: the generated project is "renut" (the base keeps its name)

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(BANJO_RENUT_RENDERER)
#include "renut_engine/gpu/banjo_renut_select.h"
#endif

REXCVAR_DEFINE_BOOL(fable2_frame_time_callback, true, "Fable2",
                    "Run the 3D Engine's frame-time GPU callback (82B89A78) that Fable writes into the ring, so its "
                    "clock follows the real time below 30 fps (fixes the game slowing down after the intro)");
// fable2-nx (2026-11-18, the GPU fault: three crashes in deko3d's present/flush with the callback queued through reNut,
// none in a run with fable2_frame_time_callback off; still with the fill flush skipped): by default the frame-time
// record now runs right here, on the 3D Engine thread at its Swap, instead of through reNut's callback queue (whose
// marker and fence gating the crashes follow). The record then holds the Swap's time, not the display's: the clock is
// the Swap's time plus a frame, which follows the real time all the same. true = through the queue, as before.
REXCVAR_DEFINE_BOOL(fable2_frame_time_callback_queued, false, "Fable2",
                    "Test: run the frame-time callback through reNut's GPU callback queue (crashed) instead of at the "
                    "3D Engine's Swap");
REXCVAR_DEFINE_BOOL(fable2_ring_callbacks_all, false, "Fable2",
                    "Test: run every GPU callback Fable writes into the ring (821F5F20), not only the frame-time one");

namespace renut::gpu {
void InsertCallback(uint32_t function, uint32_t context, bool flush_fills);  // gpu/guest/callbacks.h
void RunReadyCallbacks();
void FlushDeferredVertices(uint32_t device_va);  // gpu/backend/queued.h
}  // namespace renut::gpu

extern "C" void __imp__ProcessAndProcessAndProcess952_821F5F20(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess1267_82B89A78(PPCContext& __restrict ctx, uint8_t* base);

namespace {
constexpr uint32_t kFrameTimeCallback = 0x82B89A78u;
std::atomic<uint32_t> g_seen[16];
void NoteFirst(uint32_t callback, uint32_t context, bool run) {
  for (auto& s : g_seen) {
    uint32_t v = s.load(std::memory_order_relaxed);
    if (v == callback)
      return;
    if (v == 0 && s.compare_exchange_strong(v, callback)) {
      REXLOG_INFO("[fable2] ring GPU callback {:08X} (context {:08X}) first written: {}", callback, context,
                  run ? "run" : "not run (fable2_ring_callbacks_all is off)");
      return;
    }
    if (v == callback)
      return;
  }
}
}  // namespace

extern "C" void ProcessAndProcessAndProcess952_821F5F20(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t device = ctx.r3.u32, callback = ctx.r6.u32, context = ctx.r7.u32;
  __imp__ProcessAndProcessAndProcess952_821F5F20(ctx, base);
  if (!callback)
    return;
  static const bool frame_time = REXCVAR_GET(fable2_frame_time_callback);
  static const bool all = REXCVAR_GET(fable2_ring_callbacks_all);
  // fable2-nx (2026-11-25): with the SDK's emulated GPU (banjo_renderer = "emulated") the GPU itself raises the ring's
  // INTERRUPT and D3D's handler runs the callbacks; running them here as well hung the 3D Engine at its first frame.
#if defined(BANJO_RENUT_RENDERER)
  static const bool renut = ::banjo::renut_gpu::Active();
#else
  static const bool renut = false;
#endif
  const bool run = renut && (all || (frame_time && callback == kFrameTimeCallback));
  NoteFirst(callback, context, run);
  if (!run)
    return;
  static const bool queued = REXCVAR_GET(fable2_frame_time_callback_queued);
  if (callback == kFrameTimeCallback && !queued) {
    // The guest leaf writes r3-r10 back: the caller reads r3 (the ring position 821F5F20 returned).
    const PPCRegister r3 = ctx.r3, r4 = ctx.r4, r5 = ctx.r5, r6 = ctx.r6, r7 = ctx.r7, r8 = ctx.r8, r9 = ctx.r9,
                      r10 = ctx.r10;
    ctx.r3.u64 = context;
    ProcessAndProcessAndProcess1267_82B89A78(ctx, base);
    ctx.r3 = r3, ctx.r4 = r4, ctx.r5 = r5, ctx.r6 = r6, ctx.r7 = r7, ctx.r8 = r8, ctx.r9 = r9, ctx.r10 = r10;
    return;
  }
  renut::gpu::FlushDeferredVertices(device);
  renut::gpu::RunReadyCallbacks();
  // The frame-time record frees nothing the GPU reads: no fill flush for it (the Swap that follows flushes them).
  renut::gpu::InsertCallback(callback, context, callback != kFrameTimeCallback);
}
