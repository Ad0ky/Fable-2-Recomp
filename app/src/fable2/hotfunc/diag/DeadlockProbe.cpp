// fable2-nx (2026-11-16): a probe for the freeze at the idle demo and after pressing A on the title screen.
//
// The stall watchdog's dump of that freeze (banjo_profiler.log) shows the game's main thread in
// ProcessAndProcessAndProcess503_8222AC38, which reserves space in a ring buffer and, while the ring is full, loops on
// Sleep(0) until the reader's position (ring + 64, written by ProcessAndProcessAndProcess543_822943E8 under the
// critical section at ring + 72) moves; and the game's "3D Engine" thread, the ring's reader, waiting in
// ProcessAndWaitMapStatus_82B49B50 (the handle at object + 128) for work. Nothing else runs.
//
// The three functions are overridden here (strong definitions over the generated weak ones; each calls its generated
// body unchanged) to record, at no change to what the game does:
//   - each thread inside 8222AC38: the ring, the size and alignment asked, since when;
//   - each call of 822943E8: the ring and the time (the last 8 rings);
//   - each thread inside 82B49B50: the object, its handle, the timeout, since when.
// (XEvent::Query at the report takes and gives back a pending signal: only at a freeze, from the watchdog.)
// Fable2DeadlockProbeReport() (the renderer's [freeze] watchdog calls it) logs them with the rings' fields and, for
// each wait, the kernel object's type and signal state. A signalled event with a thread still waiting on it is the
// SDK's fault; an unsignalled one means the game never signalled it.
#include "renut_pch.h"  // fable2-nx: the generated project is "renut" (the base keeps its name)

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>

#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xobject.h>
#include <rex/system/xthread.h>

extern "C" void __imp__ProcessAndProcessAndProcess503_8222AC38(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__ProcessAndProcessAndProcess543_822943E8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__ProcessAndWaitMapStatus_82B49B50(PPCContext& __restrict ctx, uint8_t* base);

namespace {

std::atomic<uint8_t*> g_base{nullptr};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

uint32_t CurrentThreadId() {
  auto* t = rex::system::XThread::GetCurrentThread();
  return t ? t->thread_id() : 0;
}

// One slot per thread that is inside one of the functions (kind 1: the ring reservation, kind 2: the wait).
struct Slot {
  std::atomic<uint32_t> thread{0};
  std::atomic<uint32_t> kind{0};
  std::atomic<uint32_t> object{0};
  std::atomic<uint32_t> a{0};
  std::atomic<uint32_t> b{0};
  std::atomic<int64_t> since{0};
};
constexpr int kSlots = 64;
Slot g_slots[kSlots];

Slot* SlotForThisThread() {
  thread_local Slot* mine = nullptr;
  if (mine)
    return mine;
  const uint32_t id = CurrentThreadId();
  for (Slot& s : g_slots) {
    uint32_t expected = 0;
    if (s.thread.load(std::memory_order_relaxed) == id ||
        s.thread.compare_exchange_strong(expected, id, std::memory_order_relaxed)) {
      mine = &s;
      return mine;
    }
  }
  return nullptr;
}

// The rings the reader released, the last 8.
struct Release {
  std::atomic<uint32_t> ring{0};
  std::atomic<uint32_t> count{0};
  std::atomic<int64_t> last{0};
  std::atomic<uint32_t> thread{0};
};
Release g_releases[8];

uint32_t ReadU32(uint8_t* base, uint32_t address, bool& ok) {
  ok = false;
  if (!base || address < 0x40000000u || address >= 0xA0000000u || (address & 3))
    return 0;
  size_t length = 4;
  rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
  if (!rex::memory::QueryProtect(base + address, length, access) ||
      (int(access) & int(rex::memory::PageAccess::kReadOnly)) == 0)
    return 0;
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  ok = true;
  return __builtin_bswap32(v);
}

std::string RingFields(uint8_t* base, uint32_t ring) {
  static constexpr uint32_t kOffsets[] = {4, 8, 12, 16, 20, 24, 28, 32, 36, 52, 60, 64, 68};
  std::string out;
  for (uint32_t o : kOffsets) {
    bool ok;
    const uint32_t v = ReadU32(base, ring + o, ok);
    out += ok ? fmt::format(" +{}={:X}", o, v) : fmt::format(" +{}=?", o);
  }
  // The critical section at + 72: lock count (+ 88), recursion (+ 92), owning thread (+ 96).
  // (the SDK keeps the lock count in the host's byte order: xboxkrnl_rtl.cpp X_RTL_CRITICAL_SECTION)
  bool ok1, ok2, ok3;
  const uint32_t lock = __builtin_bswap32(ReadU32(base, ring + 72 + 16, ok1)), rec = ReadU32(base, ring + 72 + 20, ok2),
                 owner = ReadU32(base, ring + 72 + 24, ok3);
  if (ok1 && ok2 && ok3)
    out += fmt::format("; its lock: count {} recursion {} owner {:08X}", int32_t(lock), int32_t(rec), owner);
  return out;
}

std::string HandleState(uint32_t handle) {
  auto* kernel = rex::system::kernel_state();
  if (!kernel)
    return "?";
  auto object = kernel->object_table()->LookupObject<rex::system::XObject>(handle);
  if (!object)
    return "no such handle";
  std::string out = fmt::format("type {}", uint32_t(object->type()));
  if (object->type() == rex::system::XObject::Type::Event) {
    uint32_t type = 0, state = 0;
    static_cast<rex::system::XEvent*>(object.get())->Query(&type, &state);
    out += fmt::format(" (event, {} reset, signal state {})", type ? "auto" : "manual", state);
  }
  return out;
}

}  // namespace

extern "C" void ProcessAndProcessAndProcess503_8222AC38(PPCContext& __restrict ctx, uint8_t* base) {
  g_base.store(base, std::memory_order_relaxed);
  Slot* s = SlotForThisThread();
  if (s) {
    s->object.store(ctx.r3.u32, std::memory_order_relaxed);
    s->a.store(ctx.r4.u32, std::memory_order_relaxed);
    s->b.store(ctx.r5.u32, std::memory_order_relaxed);
    s->since.store(NowMs(), std::memory_order_relaxed);
    s->kind.store(1, std::memory_order_release);
  }
  __imp__ProcessAndProcessAndProcess503_8222AC38(ctx, base);
  if (s)
    s->kind.store(0, std::memory_order_release);
}

extern "C" void ProcessAndProcessAndProcess543_822943E8(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t ring = ctx.r3.u32;
  Release* r = nullptr;
  for (Release& x : g_releases)
    if (x.ring.load(std::memory_order_relaxed) == ring) {
      r = &x;
      break;
    }
  if (!r) {
    // Replace the oldest.
    r = &g_releases[0];
    for (Release& x : g_releases)
      if (x.last.load(std::memory_order_relaxed) < r->last.load(std::memory_order_relaxed))
        r = &x;
    r->ring.store(ring, std::memory_order_relaxed);
    r->count.store(0, std::memory_order_relaxed);
  }
  r->count.fetch_add(1, std::memory_order_relaxed);
  r->last.store(NowMs(), std::memory_order_relaxed);
  r->thread.store(CurrentThreadId(), std::memory_order_relaxed);
  __imp__ProcessAndProcessAndProcess543_822943E8(ctx, base);
}

extern "C" void ProcessAndWaitMapStatus_82B49B50(PPCContext& __restrict ctx, uint8_t* base) {
  g_base.store(base, std::memory_order_relaxed);
  Slot* s = SlotForThisThread();
  const uint32_t object = ctx.r3.u32;
  if (s) {
    bool ok;
    s->object.store(object, std::memory_order_relaxed);
    s->a.store(ReadU32(base, object + 128, ok), std::memory_order_relaxed);
    s->b.store(ctx.r4.u32, std::memory_order_relaxed);
    s->since.store(NowMs(), std::memory_order_relaxed);
    s->kind.store(2, std::memory_order_release);
  }
  __imp__ProcessAndWaitMapStatus_82B49B50(ctx, base);
  if (s)
    s->kind.store(0, std::memory_order_release);
}

extern "C" void Fable2DeadlockProbeReport() {
  uint8_t* base = g_base.load(std::memory_order_relaxed);
  const int64_t now = NowMs();
  for (Slot& s : g_slots) {
    const uint32_t kind = s.kind.load(std::memory_order_acquire), thread = s.thread.load(std::memory_order_relaxed);
    if (!kind || !thread)
      continue;
    const uint32_t object = s.object.load(std::memory_order_relaxed);
    const double secs = double(now - s.since.load(std::memory_order_relaxed)) / 1000.0;
    if (kind == 1)
      REXLOG_WARN("[probe] thread {:X} reserving {} bytes (alignment {}) in ring {:08X} for {:.1f} s:{}", thread,
                  s.a.load(), s.b.load(), object, secs, RingFields(base, object));
    else
      REXLOG_WARN("[probe] thread {:X} waiting on object {:08X} (handle {:08X}: {}), timeout argument {:X}, for "
                  "{:.1f} s",
                  thread, object, s.a.load(), HandleState(s.a.load()), s.b.load(), secs);
  }
  for (Release& r : g_releases) {
    const uint32_t ring = r.ring.load(std::memory_order_relaxed);
    if (!ring)
      continue;
    REXLOG_WARN("[probe] ring {:08X}: released {} times, the last {:.1f} s ago by thread {:X}:{}", ring,
                r.count.load(), double(now - r.last.load()) / 1000.0, r.thread.load(), RingFields(base, ring));
  }
}
