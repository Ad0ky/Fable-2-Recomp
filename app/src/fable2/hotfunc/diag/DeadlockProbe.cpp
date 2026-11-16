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

#include <algorithm>
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
extern "C" void __imp__SetEvent_SdkRuntime_822C9380(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8236CBB8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8236CB48(PPCContext& __restrict ctx, uint8_t* base);

extern std::atomic<uint64_t> g_banjo_game_frames;  // the game's frames (D3DDevice_Swap_hook; app/src/katamari_keys.cpp)

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

// fable2-nx (2026-11-16, second round): the game's SetEvent (SetEvent_SdkRuntime_822C9380, r3 the handle): per handle the
// last call, its thread and the count, so a wait's report says when its event was last set. The first run showed the
// 3D Engine thread waiting on its wake event (auto reset, not signalled) from 4 s before the freeze.
struct SetRecord {
  std::atomic<uint32_t> handle{0};
  std::atomic<uint32_t> count{0};
  std::atomic<uint32_t> thread{0};
  std::atomic<int64_t> last{0};
};
constexpr int kSetRecords = 128;
SetRecord g_sets[kSetRecords];

SetRecord* SetRecordFor(uint32_t handle, bool create) {
  const uint32_t start = (handle * 2654435761u) >> 25;  // 0-127
  for (int i = 0; i < kSetRecords; ++i) {
    SetRecord& r = g_sets[(start + i) % kSetRecords];
    const uint32_t h = r.handle.load(std::memory_order_relaxed);
    if (h == handle)
      return &r;
    if (h == 0) {
      if (!create)
        return nullptr;
      uint32_t expected = 0;
      if (r.handle.compare_exchange_strong(expected, handle, std::memory_order_relaxed) || expected == handle)
        return &r;
    }
  }
  return nullptr;
}

// sub_8236CBB8: sets the 3D Engine's wake event under a lock and waits for its answer (event at its object + 460).
std::atomic<uint32_t> g_sync_thread{0};
std::atomic<int64_t> g_sync_since{0};
std::atomic<uint32_t> g_sync_count{0};
std::atomic<int64_t> g_sync_last_end{0};

// fable2-nx (2026-11-16, third round): the 3D Engine stops only when asked: sub_8236CB48 sets its pause flag (+192) and
// waits for its answer; it then waits on its wake event until sub_8236CBB8 (resume) sets it. The second run: paused
// at the demo's start, never resumed, while the main thread went on filling the ring the paused 3D Engine reads. Here:
// the pause's and the resume's calls (thread, time, the host code that called them: the generated caller), and while
// paused, which generated functions call the ring reservation (host return addresses; banjo_profiler.log's
// "module base" names them with fable2-nx.elf).
std::atomic<uint32_t> g_pause_count{0}, g_pause_thread{0}, g_resume_count{0};
std::atomic<int64_t> g_pause_last{0}, g_resume_last{0};
std::atomic<uintptr_t> g_pause_caller{0}, g_resume_caller{0};
std::atomic<bool> g_paused{false};
struct CallerCount {
  std::atomic<uintptr_t> address{0};
  std::atomic<uint32_t> count{0};
  std::atomic<uint64_t> bytes{0};
};
CallerCount g_paused_reservers[16];
void NotePausedReserver(uintptr_t address, uint32_t bytes) {
  for (CallerCount& c : g_paused_reservers) {
    uintptr_t a = c.address.load(std::memory_order_relaxed);
    if (a == 0) {
      uintptr_t expected = 0;
      if (!c.address.compare_exchange_strong(expected, address, std::memory_order_relaxed) && expected != address)
        continue;
      a = address;
    }
    if (a == address) {
      c.count.fetch_add(1, std::memory_order_relaxed);
      c.bytes.fetch_add(bytes, std::memory_order_relaxed);
      return;
    }
  }
}

// fable2-nx (2026-11-16, fourth round): the third run named the pause's caller: sub_8236CD68 builds the title screen's
// transition object, which pauses the 3D Engine until the demo ends or a button is pressed (sub_826C61B8,
// sub_826C67F8 and sub_82378868 set its + 244; sub_8236D0A8 then resumes). The main thread's frame loop
// (sub_82355460) goes on writing into the ring meanwhile. Whether the ring was already nearly full when the pause came
// (the 3D Engine behind) or the writes during the pause fill it decides the fix, so per pause: the main ring's fill at
// its start, the frames, reservations and bytes until its resume; and the bytes per frame while not paused.
struct Episode {
  std::atomic<int64_t> start{0}, end{0};
  std::atomic<uint32_t> ring{0}, write{0}, read{0}, reader{0}, mark{0}, capacity{0};
  std::atomic<uint64_t> frames_at_start{0}, frames_at_end{0}, reservations{0}, bytes{0};
};
Episode g_episodes[8];
std::atomic<uint32_t> g_episode_count{0};
std::atomic<uint32_t> g_main_ring{0};  // the ring the pausing thread last reserved in
std::atomic<uint64_t> g_unpaused_bytes{0}, g_unpaused_reservations{0};

uint32_t& LastRingOfThisThread() {
  thread_local uint32_t ring = 0;
  return ring;
}

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
  LastRingOfThisThread() = ctx.r3.u32;
  const uint32_t asked = ctx.r4.u32 + (ctx.r5.u32 ? ctx.r5.u32 - 1 : 0);
  if (g_paused.load(std::memory_order_relaxed)) {
    NotePausedReserver(reinterpret_cast<uintptr_t>(__builtin_return_address(0)), ctx.r4.u32);
    const uint32_t n = g_episode_count.load(std::memory_order_relaxed);
    if (n && n <= 8 && ctx.r3.u32 == g_episodes[n - 1].ring.load(std::memory_order_relaxed)) {
      g_episodes[n - 1].reservations.fetch_add(1, std::memory_order_relaxed);
      g_episodes[n - 1].bytes.fetch_add(asked, std::memory_order_relaxed);
    }
  } else if (ctx.r3.u32 == g_main_ring.load(std::memory_order_relaxed)) {
    g_unpaused_reservations.fetch_add(1, std::memory_order_relaxed);
    g_unpaused_bytes.fetch_add(asked, std::memory_order_relaxed);
  }
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

extern "C" void SetEvent_SdkRuntime_822C9380(PPCContext& __restrict ctx, uint8_t* base) {
  if (SetRecord* r = SetRecordFor(ctx.r3.u32, true)) {
    r->count.fetch_add(1, std::memory_order_relaxed);
    r->thread.store(CurrentThreadId(), std::memory_order_relaxed);
    r->last.store(NowMs(), std::memory_order_relaxed);
  }
  __imp__SetEvent_SdkRuntime_822C9380(ctx, base);
}

extern "C" void sub_8236CB48(PPCContext& __restrict ctx, uint8_t* base) {
  g_pause_caller.store(reinterpret_cast<uintptr_t>(__builtin_return_address(0)), std::memory_order_relaxed);
  g_pause_thread.store(CurrentThreadId(), std::memory_order_relaxed);
  g_pause_count.fetch_add(1, std::memory_order_relaxed);
  g_pause_last.store(NowMs(), std::memory_order_relaxed);
  // The main ring's positions as the pause begins (+8 size, +32 write, +36 read, +52 the wrap mark; see 8222AC38).
  const uint32_t ring = LastRingOfThisThread();
  g_main_ring.store(ring, std::memory_order_relaxed);
  const uint32_t n = g_episode_count.load(std::memory_order_relaxed);
  if (n < 8 && ring) {
    Episode& e = g_episodes[n];
    bool ok;
    e.ring.store(ring);
    e.capacity.store(ReadU32(base, ring + 8, ok));
    e.write.store(ReadU32(base, ring + 32, ok));
    e.read.store(ReadU32(base, ring + 36, ok));
    e.mark.store(ReadU32(base, ring + 52, ok));
    e.reader.store(ReadU32(base, ring + 64, ok));  // the 3D Engine's own position (822943E8), +36 is the writer's copy
    e.start.store(NowMs());
    e.frames_at_start.store(g_banjo_game_frames.load(std::memory_order_relaxed));
  }
  __imp__sub_8236CB48(ctx, base);
  if (n < 8 && ring)
    g_episode_count.store(n + 1, std::memory_order_relaxed);
  g_paused.store(true, std::memory_order_relaxed);
}

extern "C" void sub_8236CBB8(PPCContext& __restrict ctx, uint8_t* base) {
  g_resume_caller.store(reinterpret_cast<uintptr_t>(__builtin_return_address(0)), std::memory_order_relaxed);
  g_resume_count.fetch_add(1, std::memory_order_relaxed);
  g_resume_last.store(NowMs(), std::memory_order_relaxed);
  if (g_paused.load(std::memory_order_relaxed)) {
    const uint32_t n = g_episode_count.load(std::memory_order_relaxed);
    if (n && n <= 8 && !g_episodes[n - 1].end.load()) {
      g_episodes[n - 1].end.store(NowMs());
      g_episodes[n - 1].frames_at_end.store(g_banjo_game_frames.load(std::memory_order_relaxed));
    }
  }
  g_paused.store(false, std::memory_order_relaxed);
  g_sync_count.fetch_add(1, std::memory_order_relaxed);
  g_sync_since.store(NowMs(), std::memory_order_relaxed);
  g_sync_thread.store(CurrentThreadId(), std::memory_order_release);
  __imp__sub_8236CBB8(ctx, base);
  g_sync_thread.store(0, std::memory_order_release);
  g_sync_last_end.store(NowMs(), std::memory_order_relaxed);
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
    {
      const uint32_t handle = s.a.load();
      std::string set = "never set through SetEvent_SdkRuntime_822C9380";
      if (SetRecord* r = SetRecordFor(handle, false))
        set = fmt::format("set {} times through SetEvent_SdkRuntime_822C9380, the last {:.1f} s ago by thread {:X}",
                          r->count.load(), double(now - r->last.load()) / 1000.0, r->thread.load());
      REXLOG_WARN("[probe] thread {:X} waiting on object {:08X} (handle {:08X}: {}; {}), timeout argument {:X}, for "
                  "{:.1f} s",
                  thread, object, handle, HandleState(handle), set, s.b.load(), secs);
    }
  }
  {
    const uint32_t t = g_sync_thread.load(std::memory_order_acquire);
    REXLOG_WARN("[probe] sub_8236CBB8 (wake the 3D Engine and wait for its answer): {} calls; {}; the last ended {:.1f} s ago",
                g_sync_count.load(),
                t ? fmt::format("thread {:X} inside it for {:.1f} s", t, double(now - g_sync_since.load()) / 1000.0)
                  : std::string("no thread inside it"),
                g_sync_last_end.load() ? double(now - g_sync_last_end.load()) / 1000.0 : -1.0);
  }
  REXLOG_WARN("[probe] 3D Engine pause (sub_8236CB48): {} calls, the last {:.1f} s ago by thread {:X} from host {:#x}; "
              "resume (sub_8236CBB8): {} calls, the last {} from host {:#x}; paused now: {}",
              g_pause_count.load(), g_pause_last.load() ? double(now - g_pause_last.load()) / 1000.0 : -1.0,
              g_pause_thread.load(), g_pause_caller.load(), g_resume_count.load(),
              g_resume_last.load() ? fmt::format("{:.1f} s ago", double(now - g_resume_last.load()) / 1000.0)
                                   : std::string("never"),
              g_resume_caller.load(), g_paused.load() ? "yes" : "no");
  for (CallerCount& c : g_paused_reservers)
    if (const uintptr_t a = c.address.load())
      REXLOG_WARN("[probe]   while paused, the ring reservation was called {} times ({} bytes) from host {:#x}",
                  c.count.load(), c.bytes.load(), a);
  {
    const uint64_t frames = g_banjo_game_frames.load(std::memory_order_relaxed);
    uint64_t paused_frames = 0;
    const uint32_t n = std::min<uint32_t>(g_episode_count.load(), 8);
    for (uint32_t i = 0; i < n; ++i) {
      const Episode& e = g_episodes[i];
      const int64_t end = e.end.load();
      const uint64_t f_end = end ? e.frames_at_end.load() : frames;
      const uint64_t f = f_end - e.frames_at_start.load();
      paused_frames += f;
      const uint32_t w = e.write.load(), r = e.reader.load() + 1, mark = e.mark.load(), cap = e.capacity.load();
      // 8222AC38 waits while the write position is below the read position and the request does not fit before it.
      const uint32_t used = w >= r ? w - r : (mark > r ? mark - r : 0) + w;
      REXLOG_WARN("[probe] pause {}: {:.1f} s{}; ring {:08X} at its start: write {:X} read {:X} (its copy {:X}) wrap mark {:X} size {:X} "
                  "(about {} KiB of {} KiB waiting for the 3D Engine); during it {} frames, {} reservations, {} KiB "
                  "({} bytes a frame)",
                  i + 1, double((end ? end : now) - e.start.load()) / 1000.0, end ? "" : " (not resumed)", e.ring.load(),
                  w, r, e.read.load(), mark, cap, used / 1024, cap / 1024, f, e.reservations.load(), e.bytes.load() / 1024,
                  f ? e.bytes.load() / f : 0);
    }
    const uint64_t first = n ? g_episodes[0].frames_at_start.load() : frames;
    const uint64_t unpaused_frames = frames > first + paused_frames ? frames - first - paused_frames : 0;
    REXLOG_WARN("[probe] not paused (from the first pause on): {} frames, {} reservations, {} KiB in ring {:08X} ({} bytes a frame)",
                unpaused_frames, g_unpaused_reservations.load(), g_unpaused_bytes.load() / 1024, g_main_ring.load(),
                unpaused_frames ? g_unpaused_bytes.load() / unpaused_frames : 0);
  }
  for (Release& r : g_releases) {
    const uint32_t ring = r.ring.load(std::memory_order_relaxed);
    if (!ring)
      continue;
    REXLOG_WARN("[probe] ring {:08X}: released {} times, the last {:.1f} s ago by thread {:X}:{}", ring,
                r.count.load(), double(now - r.last.load()) / 1000.0, r.thread.load(), RingFields(base, ring));
  }
}
