// fable2-nx (2026-11-26, the brazier flames that blink below 30 fps and are gone at 5): a particle system's render
// object keeps the data the game last handed it.
//
// The game sends each particle system's render object (vtable 0x820F64B0) a message every step (1/30 s), through the
// render ring. Its handler, ProcessAndProcessAndProcess714_821D8B60 (the object's slot 4), takes the messages whose
// type method answers 6 or 36 (0x8269B5C0, 0x82A3F770): it copies their particle count (+28 to the object's +176),
// bounds and drawing parameters, then calls the data setter of the system's type (sub_82293290 for type 1, the flames'
// trails; sub_822CCAB8, 2; sub_82A6FD40, 3). With the shared data mode on (0x8335F638 bit 0x10, Fable's), the setter
// takes the message's data holder (+16) as the object's (+180) and releases the one it had (adds 1 to its first
// word). The game reuses a holder only once released, from a pool of 5 per system: when every holder is still out it
// sends the message with no holder (+16 = 0, +20 = 0), and the object is left with none. The scene walk adds an
// object to the frame only with a holder (sub_82A6FEC8: +176 and +180 not 0).
//
// At 30 fps the 3D engine takes each message before the next is sent and the game never runs out. Below 30 it takes
// several steps' messages in one frame (5 at 6 fps): the game ran out within them, and when the frame's last message
// had no holder the system was not drawn. Measured on the PC, the systems ending a 3D engine frame without data: none
// at 30 fps, 20 % at 13 (the Switch's rate), 33 % at 6, 54 % at 5; with this, none at 13, 6 and 5, the brazier drawn as
// at 30 fps.
//
// So a message with no holder, for an object that has one, is not applied at all: the object keeps its holder (not
// released: the game cannot write into it) with the count, bounds and parameters that came with it, until a message
// with a holder. Only the setter left out, the empty message's other fields (copied by the handler first) drew the
// trail as a tall bright column that 30 fps never shows.
#include "renut_pch.h"  // fable2-nx: the generated project is "renut"

#include <cstdint>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(fable2_particle_keep_data, true, "Fable2",
                    "A particle system's render object keeps its data when the game's message brings none (the "
                    "brazier flames blinked below 30 fps)");

extern "C" void __imp__ProcessAndProcessAndProcess714_821D8B60(PPCContext& __restrict ctx, uint8_t* base);

namespace {

constexpr uint32_t kSharedDataMode = 0x8335F638;  // bit 0x10: the holders are shared with the game
constexpr uint32_t kTypeIs6 = 0x8269B5C0, kTypeIs36 = 0x82A3F770;  // the data messages' type methods

// true: the message is left out (the object keeps its holder and everything that came with it).
bool LeaveOut(PPCContext& __restrict ctx, uint8_t* base) {
  static const bool on = REXCVAR_GET(fable2_particle_keep_data);
  const uint32_t object = ctx.r3.u32, message = ctx.r4.u32;
  if (!on || !object || !message)
    return false;
  const uint32_t type_method = REX_LOAD_U32(REX_LOAD_U32(message) + 4);
  if (type_method != kTypeIs6 && type_method != kTypeIs36)
    return false;
  // An empty message (shared mode, no holder) of the object's own type and with particles, for an object holding data.
  return (REX_LOAD_U32(kSharedDataMode) & 0x10) && !REX_LOAD_U32(message + 16) && !REX_LOAD_U32(message + 20) &&
         REX_LOAD_U32(message + 28) && REX_LOAD_U32(object + 180) &&
         REX_LOAD_U32(object + 240) == REX_LOAD_U32(message + 8);
}

}  // namespace

extern "C" void ProcessAndProcessAndProcess714_821D8B60(PPCContext& __restrict ctx, uint8_t* base) {
  if (!LeaveOut(ctx, base))
    __imp__ProcessAndProcessAndProcess714_821D8B60(ctx, base);
}
