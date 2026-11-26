// fable2_disable_msaa.h - render Fable II without its 2x MSAA (Fable 2 NX).
//
// From FableDroid (github.com/hardsilinder/FableDroid, MIT; src/core/hotfunc/unsorted/RenderConfigInit_8238DE58.cpp),
// itself the Xenia "Disable MSAA" patch (be8 0x8238DF3F = 0x01). sub_8238DE58 sets up the renderer's back-buffer
// description; its tail stores the main render size (1120x720, 960 wide when a flag is clear) and the multisample
// mode (li r9,2 at 0x8238DF3C, stw r9,156(r31): 2 = 2x MSAA, 1 = none). The same code is in this build (GOTY
// USA/Europe 1.5.0, checked in generated/default/fable_2_recomp.41.cpp). The original runs, then the stored mode is
// overwritten (the field is written only there).
//
// Why on the Switch by default: without MSAA the 1120x720 colour and depth fit one 10 MB EDRAM tile, so the game no
// longer replays the scene once per predicated tile (Begin/EndTiling, SetPredication: reNut's first frames on deko3d
// were black but for the bloom, with the tiling path barely tested on Fable), and the main pass has half the samples
// (FableDroid: the single largest GPU cost, 21.7 -> 26.9 fps on an Adreno 740). fable2_disable_msaa = false restores it.
//
// Fable 2 NX (2026-11-26): off on the Switch too. Without the game's MSAA a false shadow came and went with the camera
// on the roof lit by the lamp in Bowerstone Old Town, a piece of the lamp's light cut out in the shape of the house to
// its left, and lights and shadows on the walls there flickered the same way; the PC (MSAA on) never had them, and with
// fable2_disable_msaa = false on the console they were gone. renut's fable2_msaa_level (1 on the Switch) still decides
// the samples drawn, so the cost stays small (13-16 fps at that spot, as before).
#pragma once

#include <rex/cvar.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(fable2_disable_msaa, false, "GPU",
                    "Render Fable II without its 2x MSAA (Xenia 'Disable MSAA' patch; a false lamp shadow on the "
                    "roofs with it on the Switch)");

extern "C" void __imp__sub_8238DE58(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8238DE58(PPCContext& ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_8238DE58(ctx, base);
  if (REXCVAR_GET(fable2_disable_msaa)) {
    constexpr uint32_t kMultisampleModeOffset = 156, kMultisampleNone = 1;
    uint8_t* p = base + self + kMultisampleModeOffset;
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = uint8_t(kMultisampleNone);  // big-endian word
  }
}
