// fable2_renut_yield.h - Fable's own overrides that reNut's renderer also hooks (Fable 2 NX).
//
// Three of Fable's overrides are Direct3D functions reNut hooks (docs/nx/d3d-hookmap-fable2.tsv):
//   MainRenderLoop_82B9CD68    = D3DDevice_Swap          (src/diagnostics/fps_meter.h: trace window, F5 Lua,
//                                                          console, mod menu, then the original)
//   FrameLimiterWait_82242628  = D3D CDevice::BlockOnFence (src/core/hotfunc/vblank)
//   GpuProgressCheck_82B9BF90  = D3D CBlocker::Check      (src/core/hotfunc/unsorted)
// reNut defines renut_native_<function> for these (BANJO_RENUT_HOOK_SHARED). With reNut built in (FABLE2_RENUT,
// BANJO_RENUT_RENDERER) and chosen, the override hands the call to it; otherwise this is nothing and the override runs
// as before.
#pragma once

#include <cstdint>

#if defined(BANJO_RENUT_RENDERER)
#include "renut_engine/gpu/banjo_renut_select.h"
extern "C" void renut_native_MainRenderLoop_82B9CD68(PPCContext& ctx, uint8_t* base);
extern "C" void renut_native_FrameLimiterWait_82242628(PPCContext& ctx, uint8_t* base);
extern "C" void renut_native_GpuProgressCheck_82B9BF90(PPCContext& ctx, uint8_t* base);
#define FABLE2_RENUT_YIELD(function)        \
  if (::banjo::renut_gpu::Active()) {       \
    renut_native_##function(ctx, base);     \
    return;                                 \
  }
#else
#define FABLE2_RENUT_YIELD(function)
#endif
