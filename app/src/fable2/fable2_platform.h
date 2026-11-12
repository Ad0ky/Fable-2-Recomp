// fable2_platform.h - the few platform differences of the app layer (Fable 2 NX).
//
// The app and its diagnostics were written for Windows. The Switch build (devkitA64 GCC, libnx) has no SEH, no
// VirtualQuery, no Win32 threads or input. These helpers keep one source for both:
//
//   FABLE2_SEH_TRY { ... } FABLE2_SEH_EXCEPT { ... }
//       Windows: __try / __except(EXCEPTION_EXECUTE_HANDLER), so a probe that reads a freed guest page skips it.
//       Elsewhere: the try block runs and the except block never does. The probes that rely on it are diagnostics,
//       enabled by environment variables only (the Switch has none), so nothing reaches them there by default.
//   fable2::CpuRelax()
//       The spin-wait pause: PAUSE on x86 (YieldProcessor), YIELD on ARM64.
//   FABLE2_NOINLINE
//       __declspec(noinline) / __attribute__((noinline)).
//   fable2::DetachThread(std::thread(...))
//       std::thread::detach() throws std::system_error on the Switch: libnx has no thread detach (newlib's
//       pthread_detach calls __syscall_thread_detach, which libnx does not implement; see the SDK's
//       threading_posix.cpp). There the thread object is kept alive instead (never joined, never destroyed: these
//       threads run until the process ends). Elsewhere it is detached as before.
//   fable2::GuestHostBase()
//       The host address of guest address 0. The PC build maps the guest arena at 0x100000000 (the probes assumed
//       that); the Switch build maps it wherever the SDK found room (0xED8600000 in the first console run). Set from
//       Memory::virtual_membase() at the start of Fable2App::OnPostSetup; 0x100000000 until then. The physical
//       mirror (guest addresses >= 0xE0000000) is 0x100000000 above it on both.
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define FABLE2_SEH_TRY __try
#define FABLE2_SEH_EXCEPT __except (EXCEPTION_EXECUTE_HANDLER)
#define FABLE2_NOINLINE __declspec(noinline)
#else
#define FABLE2_SEH_TRY if (true)
#define FABLE2_SEH_EXCEPT else
#define FABLE2_NOINLINE __attribute__((noinline))
#endif

#include <atomic>
#include <cstdint>
#include <thread>

namespace fable2 {

inline void DetachThread(std::thread&& t) {
#if defined(__SWITCH__)
  new std::thread(std::move(t));
#else
  t.detach();
#endif
}

inline std::atomic<uintptr_t>& GuestHostBaseStore() {
  static std::atomic<uintptr_t> base{0x100000000ull};
  return base;
}
inline uintptr_t GuestHostBase() { return GuestHostBaseStore().load(std::memory_order_relaxed); }
inline void SetGuestHostBase(const void* base) {
  GuestHostBaseStore().store(reinterpret_cast<uintptr_t>(base), std::memory_order_relaxed);
}

inline void CpuRelax() {
#if defined(_WIN32)
  YieldProcessor();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#elif defined(__x86_64__)
  __builtin_ia32_pause();
#endif
}

}  // namespace fable2
