/**
 * @file    gpu/d3d/hooks/state.cpp
 * @brief   N&B's D3D state calls, hooked with the native ports (d3d_native.h).
 *
 * The render and sampler state setters are the ones N&B's state tables name
 * (D3D::InitializeApiState puts them into every device's dispatch table,
 * which the game's inline SetRenderState / SetSamplerState call through).
 * The rest are D3DDevice_Set* and the CDevice internals that write the ring.
 */
#include <cstring>

#include <rex/hook.h>

#include "renut_engine/gpu/banjo_renut_select.h"
#include <rex/types.h>

#include "renut_engine/gpu/d3d/d3d_native.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"  // blur-nx (2026-11-10, port 76; its key 213): OcclusionQueriesOn
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/render/surfaces.h"  // blur-nx (2026-11-10, port 76; its key 215): NoteSurfaceHeader
#include <algorithm>

using namespace renut::gpu;

namespace {

template <u32 State>
void SetRenderStateHook(u32 device, u32 value) {
  native::SetRenderState(native::DeviceAt(device), State, value);
}

template <u32 State>
void SetSamplerStateHook(u32 device, u32 sampler, u32 value) {
  native::SetSamplerState(native::DeviceAt(device), sampler, State, value);
}

template <u32 State>
u32 GetRenderStateHook(u32 device) {
  return native::GetRenderState(native::DeviceAt(device), State);
}

template <u32 State>
u32 GetSamplerStateHook(u32 device, u32 sampler) {
  return native::GetSamplerState(native::DeviceAt(device), sampler, State);
}

// The device's AddRef of a resource it hands out (D3DResource_AddRef,
// resource.cpp).
u32 AddRefResource(u32 resource) {
  return native::AddRefResource(resource);
}

// D3DSurface *GetRenderTarget(D3DDevice*, DWORD Index) [0x82636ED0]
u32 D3DDevice_GetRenderTarget_hook(u32 device, u32 index) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d && index < 4 ? d.W(native::off::RenderTargets + index * 4) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DSurface *GetDepthStencilSurface(D3DDevice*) [0x82636F10]
u32 D3DDevice_GetDepthStencilSurface_hook(u32 device) {
  const native::Device d = native::DeviceAt(device);
  const u32 surface = d ? d.W(native::off::DepthStencil) : 0;
  if (surface)
    AddRefResource(surface);
  return surface;
}

// D3DVertexBuffer *GetStreamSource(D3DDevice*, UINT StreamNumber,
//     UINT *pOffsetInBytes, UINT *pStride) [0x82636E40]: the offset is the
// stream's fetch constant address less the buffer's.
u32 D3DDevice_GetStreamSource_hook(u32 device, u32 stream, mapped_u32 offset, mapped_u32 stride) {
  const native::Device d = native::DeviceAt(device);
  if (!d || stream >= 16)
    return 0;
  const u32 buffer = d.W(native::off::StreamSources + stream * 4);
  u32 bytes = 0;
  if (buffer) {
    const u32 fetch = d.W(native::off::TextureFetch + (95 - stream) * 8);
    bytes = fetch - native::GpuAddress(GuestPtr<const be_u32>(buffer)[6]);
    AddRefResource(buffer);
  }
  if (offset)
    *offset = bytes;
  if (stride)
    *stride = u32(d.B(native::off::StreamStrides + stream)) << 2;
  return buffer;
}

// void D3DDevice_GetViewport(D3DDevice*, D3DVIEWPORT9*) [0x82244B18]
void D3DDevice_GetViewport_hook(u32 device, mapped_u32 viewport) {
  const native::Device d = native::DeviceAt(device);
  if (!d || !viewport)
    return;
  for (u32 i = 0; i < 4; ++i)  // fctidz, the low word
    viewport[i] = u32(native::TruncateToI64(d.F(0x3168 + i * 4)));  // fable2-nx (N&B TU3->Fable 2): 0x3170->0x3168
  viewport[4] = d.W(0x3178);  // fable2-nx (N&B TU3->Fable 2): 0x3180->0x3178
  viewport[5] = d.W(0x317C);  // fable2-nx (N&B TU3->Fable 2): 0x3184->0x317C
}

// void D3DDevice_SetClipPlane(D3DDevice*, DWORD Index, const float *pPlane) [0x826370E8]
void D3DDevice_SetClipPlane_hook(u32 device, u32 index, mapped_u32 plane) {
  native::Device d = native::DeviceAt(device);
  if (!d || !plane || index >= 6)
    return;
  for (u32 i = 0; i < 4; ++i)
    d.SetW(0x2820 + index * 16 + i * 4, plane[i]);
  d.Dirty(4, u64(1) << (54 - index));
}

// void D3DDevice_SetVertexFetchConstant(D3DDevice*, UINT VertexFetchRegister,
//     D3DVertexBuffer*, UINT Offset) [0x82637138]: a vertex fetch slot of the
// 3 in a fetch constant, made a vertex fetch constant first.
void D3DDevice_SetVertexFetchConstant_hook(u32 device, u32 reg, u32 vertex_buffer, u32 offset) {
  native::Device d = native::DeviceAt(device);
  const auto *vb = vertex_buffer ? GuestPtr<const be_u32>(vertex_buffer) : nullptr;
  if (!d || !vb || reg > 0x5F)
    return;
  const u32 slot = (0x5F - reg) / 3;
  const u32 part = (0x5F - reg) - slot * 3;
  const u32 constant = native::off::TextureFetch + slot * 24;
  const u32 type = d.W(constant) & 3;
  if (type == 0 || type == 2)
    std::memset(d.bytes + constant, 0, 24);
  d.TouchRange(constant, 24);  // banjo-nx (2026-10-26, banjo_renut_tags)
  d.SetW(constant, native::Insert(d.W(constant), 1, 0, 2));
  d.Dirty(3, u64(1) << (31 - slot));
  d.SetW(constant + part * 8, native::GpuAddress(u32(vb[6]) + offset));
  d.SetW(constant + part * 8 + 4, native::Insert(u32(vb[7]) - offset, 1, 28, 2));
}

// The boolean and integer shader constants (D3DDevice_Set{Vertex,Pixel}
// ShaderConstant{B,I}): bits from 0x2780 (vertex) / 0x2790 (pixel), a word
// of x, y, z bytes each from 0x27A0 / 0x27E0.
void SetShaderConstantsB(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i, ++start) {
    const u32 word = base + (start >> 5) * 4;
    const u32 bit = 1u << (start & 31);
    d.SetW(word, (d.W(word) & ~bit) | ((u32(data[i]) & 1) << (start & 31)));
  }
  d.Dirty(4, u64(1) << 56);
}

void SetShaderConstantsI(u32 device, u32 base, u32 start, mapped_u32 data, u32 count) {
  native::Device d = native::DeviceAt(device);
  if (!d || !data)
    return;
  for (u32 i = 0; i < count; ++i) {
    const u32 x = u32(data[i * 4]) & 0xFF, y = u32(data[i * 4 + 1]) & 0xFF;
    const u32 z = u32(data[i * 4 + 2]) & 0xFF;
    d.SetW(base + (start + i) * 4, (z << 16) | (y << 8) | x);
  }
  d.Dirty(4, u64(1) << 56);
}

// void D3DDevice_SetVertexShaderConstantB(D3DDevice*, UINT StartRegister,
//     const BOOL*, UINT BoolCount) [0x821F0B40], and the others alike.
void D3DDevice_SetVertexShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2780, start, data, count);
}
void D3DDevice_SetPixelShaderConstantB_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsB(device, 0x2790, start, data, count);
}
void D3DDevice_SetVertexShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27A0, start, data, count);
}
void D3DDevice_SetPixelShaderConstantI_hook(u32 device, u32 start, mapped_u32 data, u32 count) {
  SetShaderConstantsI(device, 0x27E0, start, data, count);
}

// void D3DDevice_EndIndexedVertices(D3DDevice*) [0x8263ED90]: the inline end
// of BeginIndexedVertices, back to the ring pointer it saved.
void D3DDevice_EndIndexedVertices_hook(u32 device) {
  native::Device d = native::DeviceAt(device);
  if (d)
    d.SetW(native::off::Ring, d.W(0x3484));  // fable2-nx (N&B TU3->Fable 2): 0x3484->0x3484
}

// void D3DDevice_SetTexture(D3DDevice*, DWORD Sampler, D3DBaseTexture*,
//     UINT64 PendingMask3) [0x821FE558]; the mask is the sampler's fetch
// constant bit, which the port derives from Sampler.
void D3DDevice_SetTexture_hook(u32 device, u32 sampler, u32 texture) {
  native::SetTexture(native::DeviceAt(device), sampler, texture);
}

// void D3DDevice_SetStreamSource(D3DDevice*, UINT StreamNumber, D3DVertexBuffer*,
//     UINT OffsetInBytes, UINT Stride, UINT64 PendingMask3) [0x821F9438]
void D3DDevice_SetStreamSource_hook(u32 device, u32 stream, u32 vertex_buffer, u32 offset,
                                    u32 stride) {
  native::SetStreamSource(native::DeviceAt(device), stream, vertex_buffer, offset, stride);
}

// void D3DDevice_SetIndices(D3DDevice*, D3DIndexBuffer*) [0x821DE438]
void D3DDevice_SetIndices_hook(u32 device, u32 index_buffer) {
  native::SetIndices(native::DeviceAt(device), index_buffer);
}

// void D3DDevice_SetVertexDeclaration(D3DDevice*, D3DVertexDeclaration*) [0x821D5060]
void D3DDevice_SetVertexDeclaration_hook(u32 device, u32 declaration) {
  native::SetVertexDeclaration(native::DeviceAt(device), declaration);
}

// void D3DDevice_SetViewport(D3DDevice*, const D3DVIEWPORT9*) [0x821F2090]
void D3DDevice_SetViewport_hook(u32 device, u32 viewport) {
  native::SetViewport(native::DeviceAt(device), viewport);
}

// void D3D::SetViewport(D3DDevice*, float X, float Y, float Width, float Height,
//     float MinZ, float MaxZ) [0x821F2110]
void D3D_SetViewport_hook(u32 device, float x, float y, float width, float height, float min_z,
                          float max_z) {
  native::SetViewport(native::DeviceAt(device), x, y, width, height, min_z, max_z);
}

// void D3DDevice_SetScissorRect(D3DDevice*, const RECT*) [0x821F2368]
void D3DDevice_SetScissorRect_hook(u32 device, mapped_u32 rect) {
  if (!rect)
    return;
  native::SetScissorRect(native::DeviceAt(device), i32(u32(rect[0])), i32(u32(rect[1])),
                         i32(u32(rect[2])), i32(u32(rect[3])));
}

// void D3D::SetSurfaceClip(CDevice*, int, int, int, int) [0x821F2460]: the
// window scissor and per-tile clip packets, nothing else.
void D3D_SetSurfaceClip_hook(u32 /*device*/, u32, u32, u32, u32) {}

// void D3DDevice_SetRenderTarget(D3DDevice*, DWORD Index, D3DSurface*) [0x82248BD0]
void D3DDevice_SetRenderTarget_hook(u32 device, u32 index, u32 surface) {
  NoteSurfaceHeader(surface);  // blur-nx (2026-11-10, port 76; its key 215: blur_renut_stack_surface_headers)
  native::SetRenderTarget(native::DeviceAt(device), index, surface);
}

// void D3DDevice_SetDepthStencilSurface(D3DDevice*, D3DSurface*) [0x82248FB0]
void D3DDevice_SetDepthStencilSurface_hook(u32 device, u32 surface) {
  NoteSurfaceHeader(surface);  // blur-nx (2026-11-10, port 76; its key 215: blur_renut_stack_surface_headers)
  native::SetDepthStencilSurface(native::DeviceAt(device), surface);
}

// void D3D::SetSurfaceInfo(CDevice*, D3DSurface*) [0x82248F20]
void D3D_SetSurfaceInfo_hook(u32 device, u32 surface) {
  native::SetSurfaceInfo(native::DeviceAt(device), surface);
}

// void D3DDevice_SetPredication(D3DDevice*, DWORD PredicationMask) [0x821F9A48]
void D3DDevice_SetPredication_hook(u32 device, u32 mask) {
  native::SetPredication(native::DeviceAt(device), mask);
}

// void D3DDevice_SetCommandBufferPredication(D3DDevice*, DWORD TilePredication,
//     DWORD RunPredication) [0x826411C0]
void D3DDevice_SetCommandBufferPredication_hook(u32 device, u32 tile_predication,
                                                u32 run_predication) {
  native::SetCommandBufferPredication(native::DeviceAt(device), tile_predication,
                                      run_predication);
}

// void D3DDevice_SetShaderGPRAllocation(D3DDevice*, DWORD Flags,
//     DWORD VertexShaderCount, DWORD PixelShaderCount) [0x821EFD38]
void D3DDevice_SetShaderGPRAllocation_hook(u32 device, u32 flags, u32 vertex_count,
                                           u32 pixel_count) {
  native::SetShaderGPRAllocation(native::DeviceAt(device), flags, vertex_count, pixel_count);
}

// void D3D::CleanupGprAllocationSetup(CDevice*) [0x82637208]
void D3D_CleanupGprAllocationSetup_hook(u32 device) {
  native::CleanupGprAllocationSetup(native::DeviceAt(device));
}

// void D3DDevice_SetScreenExtentQueryMode(D3DDevice*, D3DSCREENEXTENTQUERYMODE)
// [0x826414C0]
void D3DDevice_SetScreenExtentQueryMode_hook(u32 device, u32 mode) {
  native::SetScreenExtentQueryMode(native::DeviceAt(device), mode);
}

// void D3DDevice_UnsetAll(D3DDevice*) [0x82634C10]
void D3DDevice_UnsetAll_hook(u32 device) {
  native::UnsetAll(native::DeviceAt(device));
}

// void D3D::ResetAllState(CDevice*, BOOL) [0x82255C98]
void D3D_ResetAllState_hook(u32 device, u32 /*skip_packets*/) {
  native::ResetAllState(native::DeviceAt(device));
}

// void D3D::InvalidateConstants(D3DDevice*) [0x82634B90]
void D3D_InvalidateConstants_hook(u32 device) {
  native::InvalidateConstants(native::DeviceAt(device));
}

// void D3DDevice_BlockUntilIdle(D3DDevice*) [0x82640010] (IDA: the second
// D3D::InvalidateConstants): waits for the device's current fence (N&B calls
// it before releasing a device).
void D3DDevice_BlockUntilIdle_hook(u32 device) {
  native::InvalidateConstantsAndWait(native::DeviceAt(device));
}

} // namespace

// blur-nx (2026-11-10/11, port 76; its key 213: blur_renut_occlusion_queries): D3DQuery_GetData's answer. Renut runs no
// GPU packets, so the counts an occlusion query waits for never come: under Renut, Issue(END) leaves the sample blocks
// as they were (zeros, then whatever the memory held) and GetData said "not ready" to every query for ever, so whatever
// a game hides by a query was never drawn (Blur's light glows, the tail lights' red glow). AnswerOcclusionQuery, called
// before the game's own GetData runs, writes into every block of an issued occlusion query what the reference's GPU
// writes at the ZPASS event (the SDK's command_processor.cpp, ExecutePacketType3_EVENT_WRITE_ZPD): the block zeroed,
// Total_A and ZPass_A = 1000 samples passed (little-endian, as the GPU writes them), and clears the issue's fence word,
// which GetData compares with the GPU's read pointer: the counts are in.
// The query object as XDK 2.0.11164's GetData reads it (blur-nx, sub_8224CD70; D3DQuery_Issue is sub_8224CAE8, the two
// functions holding the 0xFFFFFEED sentinel): type 9 (occlusion) at +4, issued (bit 0x40) in the byte at +20, the fence
// at +24, up to 30 sample-block pointers from +28 and their number at +148, 64-byte blocks (END counts at +0, BEGIN at
// +32). A game at another XDK checks its own GetData against this before it registers it.
// Not hooked in the base: Beautiful Katamari's XDK 2.0.5632 image links no D3DQuery_Issue / GetData (no instruction or
// data word of it holds 0xFFFFFEED). A game whose image has GetData adds RENUT_D3DQUERY_GETDATA_HOOK(sub_<address>) to
// the hook list below (or calls AnswerOcclusionQuery from a GetData hook of its own), and runs post-codegen again
// (tools/post_codegen.py: direct_calls.py --redo) so GetData's callers go through the hooked entry.
namespace renut::gpu::native {
namespace {
constexpr u32 kQueryFakeSamples = 1000;  // the SDK's query_occlusion_fake_sample_count
u32 QueryBe(const u8 *p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | u32(p[3]); }
void QueryLe(u8 *p, u32 v) {
  p[0] = u8(v);
  p[1] = u8(v >> 8);
  p[2] = u8(v >> 16);
  p[3] = u8(v >> 24);
}
}  // namespace

// blur-nx (2026-11-11, port 76; its key 213): a query block's physical address as the game's GetData forms it from a
// pointer at query +28: (p & 0x1FFFFFFF) + (((p >> 20) + 0x200) & 0x1000), read through the 0xC0000000 mapping. A
// pointer in the 0xE0000000 range (Blur's blocks: E6D8DFC0) is a page further on (the console's 4 KB offset of that
// range). Answers written at the pointer's own page were never read: GetData read the sentinel there and said not ready.
u32 QueryBlockPhysical(u32 p) { return (p & 0x1FFFFFFFu) + ((((p >> 20) + 0x200u) & 0x1000u)); }

bool AnswerOcclusionQuery(u32 query) {
  if (!banjo::renut_gpu::OcclusionQueriesOn())
    return false;
  u8 *q = GuestPtr<u8>(query);
  if (!q || QueryBe(q + 4) != 9 || !(q[20] & 0x40))
    return false;
  const u32 blocks = std::min<u32>(QueryBe(q + 148), 30);
  bool answered = false;
  for (u32 i = 0; i < blocks; ++i) {
    const u32 pointer = QueryBe(q + 28 + 4 * i);
    if (!pointer)
      continue;
    // Every issued query, not only blocks holding the sentinel (under Renut they hold zeros or leftover memory): the
    // reference's answer for every query is 1000 passed.
    u8 *block = GuestPhysicalPtr<u8>(QueryBlockPhysical(pointer));
    std::memset(block, 0, 64);
    QueryLe(block + 0, kQueryFakeSamples);   // Total_A
    QueryLe(block + 16, kQueryFakeSamples);  // ZPass_A
    answered = true;
  }
  if (answered) {
    std::memset(q + 24, 0, 4);
    static uint32_t logged = 0;
    if (logged++ < 4)
      RGPU_INFO("[renut] occlusion query {:08X}: {} block(s) answered with {} samples passed, as the reference's GPU "
                "does (blur_renut_occlusion_queries)", query, blocks, kQueryFakeSamples);
  }
  return answered;
}
}  // namespace renut::gpu::native

// blur-nx (2026-11-10/11, port 76; its key 213): D3DQuery_GetData(query, data, size, flags) at the game's own address:
// the answer above, then the game's GetData (its __imp__), whose result the game gets. Under the emulated renderer the
// game's GetData alone (BANJO_RENUT_HOOK). One line in the hook list: RENUT_D3DQUERY_GETDATA_HOOK(sub_8224CD70);
// (blur-nx's address; the base registers none, see AnswerOcclusionQuery).
#define RENUT_D3DQUERY_GETDATA_HOOK(subroutine)                                                                   \
  REX_IMPORT(__imp__##subroutine, renut_query_get_data_original_##subroutine, u32(u32, u32, u32, u32));          \
  static u32 renut_query_get_data_##subroutine(u32 query, u32 data, u32 size, u32 flags) {                        \
    ::renut::gpu::native::AnswerOcclusionQuery(query);                                                            \
    return renut_query_get_data_original_##subroutine(query, data, size, flags);                                  \
  }                                                                                                               \
  BANJO_RENUT_HOOK(subroutine, renut_query_get_data_##subroutine)

// Render states, by their index in N&B's table (off_82E506F0).
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1018_822868E0, SetRenderStateHook<10>);  // D3DDevice_SetRenderState_ZEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1362_821BFFF0, SetRenderStateHook<11>);  // D3DDevice_SetRenderState_ZFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess950_8226B018, SetRenderStateHook<12>);  // D3DDevice_SetRenderState_ZWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1346_82188300, SetRenderStateHook<13>);  // D3DDevice_SetRenderState_FillMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess937_8220B5A8, SetRenderStateHook<14>);  // D3DDevice_SetRenderState_CullMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1067_821DC600, SetRenderStateHook<15>);  // D3DDevice_SetRenderState_AlphaBlendEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B908D0, SetRenderStateHook<16>);  // D3DDevice_SetRenderState_SeparateAlphaBlendEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1360_821A89E8, SetRenderStateHook<17>);  // D3DDevice_SetRenderState_BlendFactor  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess976_821DBCB8, SetRenderStateHook<18>);  // D3DDevice_SetRenderState_SrcBlend  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess968_821F0358, SetRenderStateHook<19>);  // D3DDevice_SetRenderState_DestBlend  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1381_822C0960, SetRenderStateHook<20>);  // D3DDevice_SetRenderState_BlendOp  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B907F0, SetRenderStateHook<21>);  // D3DDevice_SetRenderState_SrcBlendAlpha  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90860, SetRenderStateHook<22>);  // D3DDevice_SetRenderState_DestBlendAlpha  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90780, SetRenderStateHook<23>);  // D3DDevice_SetRenderState_BlendOpAlpha  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1041_822155B8, SetRenderStateHook<24>);  // D3DDevice_SetRenderState_AlphaTestEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1038_82225F28, SetRenderStateHook<25>);  // D3DDevice_SetRenderState_AlphaRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1059_821B1548, SetRenderStateHook<26>);  // D3DDevice_SetRenderState_AlphaFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1219_821E52B8, SetRenderStateHook<27>);  // D3DDevice_SetRenderState_StencilEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B18, SetRenderStateHook<28>);  // D3DDevice_SetRenderState_TwoSidedStencilMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1349_82188388, SetRenderStateHook<29>);  // D3DDevice_SetRenderState_StencilFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1350_821883B0, SetRenderStateHook<30>);  // D3DDevice_SetRenderState_StencilZFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1061_8229A778, SetRenderStateHook<31>);  // D3DDevice_SetRenderState_StencilPass  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1367_822A5F40, SetRenderStateHook<32>);  // D3DDevice_SetRenderState_StencilFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1365_822398E8, SetRenderStateHook<33>);  // D3DDevice_SetRenderState_StencilRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1377_822C08E0, SetRenderStateHook<34>);  // D3DDevice_SetRenderState_StencilMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1060_821B1568, SetRenderStateHook<35>);  // D3DDevice_SetRenderState_StencilWriteMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90BC0, SetRenderStateHook<36>);  // D3DDevice_SetRenderState_CCWStencilFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90BF8, SetRenderStateHook<37>);  // D3DDevice_SetRenderState_CCWStencilZFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C30, SetRenderStateHook<38>);  // D3DDevice_SetRenderState_CCWStencilPass  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B90, SetRenderStateHook<39>);  // D3DDevice_SetRenderState_CCWStencilFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C78, SetRenderStateHook<40>);  // D3DDevice_SetRenderState_CCWStencilRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C98, SetRenderStateHook<41>);  // D3DDevice_SetRenderState_CCWStencilMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90CB8, SetRenderStateHook<42>);  // D3DDevice_SetRenderState_CCWStencilWriteMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90CD8, SetRenderStateHook<43>);  // D3DDevice_SetRenderState_ClipPlaneEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1379_822C0900, SetRenderStateHook<44>);  // D3DDevice_SetRenderState_PointSize  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1359_8219CD68, SetRenderStateHook<45>);  // D3DDevice_SetRenderState_PointSizeMin  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1378_822C08F8, SetRenderStateHook<46>);  // D3DDevice_SetRenderState_PointSpriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1348_82188348, SetRenderStateHook<47>);  // D3DDevice_SetRenderState_PointSizeMax  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90D70, SetRenderStateHook<48>);  // D3DDevice_SetRenderState_MultiSampleAntiAlias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90DA0, SetRenderStateHook<49>);  // D3DDevice_SetRenderState_MultiSampleMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90D28, SetRenderStateHook<50>);  // D3DDevice_SetRenderState_ScissorTestEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1361_821A8A78, SetRenderStateHook<51>);  // D3DDevice_SetRenderState_SlopeScaleDepthBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1324_82289648, SetRenderStateHook<52>);  // D3DDevice_SetRenderState_DepthBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1053_821C9220, SetRenderStateHook<53>);  // D3DDevice_SetRenderState_ColorWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90DC8, SetRenderStateHook<54>);  // D3DDevice_SetRenderState_ColorWriteEnable1  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90E08, SetRenderStateHook<55>);  // D3DDevice_SetRenderState_ColorWriteEnable2  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90E48, SetRenderStateHook<56>);  // D3DDevice_SetRenderState_ColorWriteEnable3  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1366_8229A750, SetRenderStateHook<57>);  // D3DDevice_SetRenderState_TessellationMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B911E0, SetRenderStateHook<58>);  // D3DDevice_SetRenderState_MinTessellationLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1347_82188320, SetRenderStateHook<59>);  // D3DDevice_SetRenderState_MaxTessellationLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1376_822C08C0, SetRenderStateHook<60>);  // D3DDevice_SetRenderState_Wrap0  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1351_821883D8, SetRenderStateHook<61>);  // D3DDevice_SetRenderState_Wrap1  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1372_822C0820, SetRenderStateHook<62>);  // D3DDevice_SetRenderState_Wrap2  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1355_82188470, SetRenderStateHook<63>);  // D3DDevice_SetRenderState_Wrap3  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1371_822C07F8, SetRenderStateHook<64>);  // D3DDevice_SetRenderState_Wrap4  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1356_82188498, SetRenderStateHook<65>);  // D3DDevice_SetRenderState_Wrap5  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1370_822C07D0, SetRenderStateHook<66>);  // D3DDevice_SetRenderState_Wrap6  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1357_821884C0, SetRenderStateHook<67>);  // D3DDevice_SetRenderState_Wrap7  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1369_822C07B0, SetRenderStateHook<68>);  // D3DDevice_SetRenderState_Wrap8  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1358_821884E0, SetRenderStateHook<69>);  // D3DDevice_SetRenderState_Wrap9  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1375_822C0898, SetRenderStateHook<70>);  // D3DDevice_SetRenderState_Wrap10  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1352_82188400, SetRenderStateHook<71>);  // D3DDevice_SetRenderState_Wrap11  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1374_822C0870, SetRenderStateHook<72>);  // D3DDevice_SetRenderState_Wrap12  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1353_82188428, SetRenderStateHook<73>);  // D3DDevice_SetRenderState_Wrap13  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1373_822C0848, SetRenderStateHook<74>);  // D3DDevice_SetRenderState_Wrap14  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1354_82188450, SetRenderStateHook<75>);  // D3DDevice_SetRenderState_Wrap15  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90FC0, SetRenderStateHook<76>);  // D3DDevice_SetRenderState_ViewportEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess554_8221E540, SetRenderStateHook<77>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91018, SetRenderStateHook<78>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable1  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B910B0, SetRenderStateHook<79>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable2  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91148, SetRenderStateHook<80>);  // D3DDevice_SetRenderState_HighPrecisionBlendEnable3  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1052_82272E00, SetRenderStateHook<81>);  // D3DDevice_SetRenderState_HalfPixelOffset  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91248, SetRenderStateHook<82>);  // D3DDevice_SetRenderState_PrimitiveResetEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91278, SetRenderStateHook<83>);  // D3DDevice_SetRenderState_PrimitiveResetIndex  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1364_8220A5C8, SetRenderStateHook<84>);  // D3DDevice_SetRenderState_AlphaToMaskEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1363_82208158, SetRenderStateHook<85>);  // D3DDevice_SetRenderState_AlphaToMaskOffsets  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B912C8, SetRenderStateHook<86>);  // D3DDevice_SetRenderState_GuardBand_X  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B912F8, SetRenderStateHook<87>);  // D3DDevice_SetRenderState_GuardBand_Y  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91328, SetRenderStateHook<88>);  // D3DDevice_SetRenderState_DiscardBand_X  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91350, SetRenderStateHook<89>);  // D3DDevice_SetRenderState_DiscardBand_Y  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1368_822A5F60, SetRenderStateHook<90>);  // D3DDevice_SetRenderState_HiStencilEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1016_8226AE38, SetRenderStateHook<91>);  // D3DDevice_SetRenderState_HiStencilWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1015_821E07B8, SetRenderStateHook<92>);  // D3DDevice_SetRenderState_HiStencilFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1380_822C0948, SetRenderStateHook<93>);  // D3DDevice_SetRenderState_HiStencilRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913D8, SetRenderStateHook<94>);  // D3DDevice_SetRenderState_PresentInterval  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913E8, SetRenderStateHook<95>);  // D3DDevice_SetRenderState_PresentImmediateThreshold  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1345_8221D2C0, SetRenderStateHook<96>);  // D3DDevice_SetRenderState_HiZEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1019_8229F550, SetRenderStateHook<97>);  // D3DDevice_SetRenderState_HiZWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90A10, SetRenderStateHook<98>);  // D3DDevice_SetRenderState_LastPixel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90A48, SetRenderStateHook<99>);  // D3DDevice_SetRenderState_LineWidth  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90AB8, SetRenderStateHook<100>);  // D3DDevice_SetRenderState_Buffer2Frames  // fable2-nx: fable2-nx's table

// Sampler states, by their index in N&B's table (off_82E50BD0).
BANJO_RENUT_HOOK(SetBitField3_82202E48, SetSamplerStateHook<0>);  // D3DDevice_SetSamplerState_AddressU  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(SetBitField4_8222C5C0, SetSamplerStateHook<1>);  // D3DDevice_SetSamplerState_AddressV  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(SetBitField_82237430, SetSamplerStateHook<2>);  // D3DDevice_SetSamplerState_AddressW  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess795_8226B808, SetSamplerStateHook<3>);  // D3DDevice_SetSamplerState_BorderColor  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess663_82238440, SetSamplerStateHook<4>);  // D3DDevice_SetSamplerState_MagFilter  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess662_82238168, SetSamplerStateHook<5>);  // D3DDevice_SetSamplerState_MinFilter  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess740_8222C870, SetSamplerStateHook<6>);  // D3DDevice_SetSamplerState_MipFilter  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess794_821EC608, SetSamplerStateHook<7>);  // D3DDevice_SetSamplerState_MipMapLodBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2524_82B91778, SetSamplerStateHook<8>);  // D3DDevice_SetSamplerState_MaxMipLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess793_821E4E50, SetSamplerStateHook<9>);  // D3DDevice_SetSamplerState_MaxAnisotropy  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2521_82B91520, SetSamplerStateHook<10>);  // D3DDevice_SetSamplerState_MagFilterZ  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2520_82B91440, SetSamplerStateHook<11>);  // D3DDevice_SetSamplerState_MinFilterZ  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2522_82B915D8, SetSamplerStateHook<12>);  // D3DDevice_SetSamplerState_SeparateZFilterEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2525_82B917F8, SetSamplerStateHook<13>);  // D3DDevice_SetSamplerState_MinMipLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(SetBitField2_822398A8, SetSamplerStateHook<14>);  // D3DDevice_SetSamplerState_TrilinearThreshold  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2523_82B91690, SetSamplerStateHook<15>);  // D3DDevice_SetSamplerState_AnisotropyBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2526_82B918F8, SetSamplerStateHook<16>);  // D3DDevice_SetSamplerState_HGradientExpBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2527_82B91950, SetSamplerStateHook<17>);  // D3DDevice_SetSamplerState_VGradientExpBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2528_82B919A8, SetSamplerStateHook<18>);  // D3DDevice_SetSamplerState_WhiteBorderColorW  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess2529_82B91A00, SetSamplerStateHook<19>);  // D3DDevice_SetSamplerState_PointBorderEnable  // fable2-nx: fable2-nx's table

BANJO_RENUT_HOOK(ProcessAndProcessAndProcess510_821B7020, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture  // fable2-nx: fable2-nx's table
BANJO_RENUT_UNHOOKED_HOOK(sub_824A3D40, D3DDevice_SetTexture_hook);  // D3DDevice_SetTexture_Inline  // fable2-nx: not hooked: fable2-nx's table (unhooked there)
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess642_821B6DA0, D3DDevice_SetStreamSource_hook);  // D3DDevice_SetStreamSource  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1064_8219CCD8, D3DDevice_SetIndices_hook);  // D3DDevice_SetIndices  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_822225C8, D3DDevice_SetVertexDeclaration_hook);  // D3DDevice_SetVertexDeclaration  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess603_821F9E28, D3DDevice_SetViewport_hook);  // D3DDevice_SetViewport  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess602_821F9BD0, D3D_SetViewport_hook);  // D3D_SetViewport  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess600_821F9AD8, D3DDevice_SetScissorRect_hook);  // D3DDevice_SetScissorRect  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess568_821F97E8, D3D_SetSurfaceClip_hook);  // D3D_SetSurfaceClip  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess567_82286508, D3DDevice_SetRenderTarget_hook);  // D3DDevice_SetRenderTarget  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess811_82264550, D3DDevice_SetDepthStencilSurface_hook);  // D3DDevice_SetDepthStencilSurface  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess733_82286850, D3D_SetSurfaceInfo_hook);  // D3D_SetSurfaceInfo  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess791_822655B0, D3DDevice_SetPredication_hook);  // D3DDevice_SetPredication  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B9B5D0, D3DDevice_SetCommandBufferPredication_hook);  // D3DDevice_SetCommandBufferPredication  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess905_8219CE10, D3DDevice_SetShaderGPRAllocation_hook);  // D3DDevice_SetShaderGPRAllocation  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess906_82B92790, D3D_CleanupGprAllocationSetup_hook);  // D3D_CleanupGprAllocationSetup  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess965_822AF3A0, D3DDevice_SetScreenExtentQueryMode_hook);  // D3DDevice_SetScreenExtentQueryMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B9C3D0, D3DDevice_UnsetAll_hook);  // D3DDevice_UnsetAll  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess1046_822A6318, D3D_ResetAllState_hook);  // D3D_ResetAllState  // fable2-nx: fable2-nx's table
BANJO_RENUT_UNHOOKED_HOOK(sub_82634B90, D3D_InvalidateConstants_hook);  // D3D_InvalidateConstants  // fable2-nx: not hooked: fable2-nx's table (unhooked there)
BANJO_RENUT_HOOK(sub_82B9B970, D3DDevice_BlockUntilIdle_hook);  // D3DDevice_BlockUntilIdle  // fable2-nx: fable2-nx's table
// Through a thunk (j_D3DDevice_SetRenderTarget 0x82636EC8).
BANJO_RENUT_UNHOOKED_HOOK(sub_82636EC8, D3DDevice_SetRenderTarget_hook);  // D3DDevice_SetRenderTarget_Thunk  // fable2-nx: not hooked: fable2-nx's table (unhooked there)
BANJO_RENUT_HOOK(sub_82B91A60, D3DDevice_GetRenderTarget_hook);  // D3DDevice_GetRenderTarget  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91AA0, D3DDevice_GetDepthStencilSurface_hook);  // D3DDevice_GetDepthStencilSurface  // fable2-nx: fable2-nx's table
BANJO_RENUT_UNHOOKED_HOOK(sub_82636E40, D3DDevice_GetStreamSource_hook);  // D3DDevice_GetStreamSource  // fable2-nx: not hooked: fable2-nx's table (unhooked there)
BANJO_RENUT_UNHOOKED_HOOK(sub_82244B18, D3DDevice_GetViewport_hook);  // D3DDevice_GetViewport  // fable2-nx: not hooked: fable2-nx's table (unhooked there)
BANJO_RENUT_UNHOOKED_HOOK(sub_826370E8, D3DDevice_SetClipPlane_hook);  // D3DDevice_SetClipPlane  // fable2-nx: not hooked: fable2-nx's table (unhooked there)
BANJO_RENUT_UNHOOKED_HOOK(sub_82637138, D3DDevice_SetVertexFetchConstant_hook);  // D3DDevice_SetVertexFetchConstant  // fable2-nx: not hooked: fable2-nx's table (unhooked there)
BANJO_RENUT_HOOK(ProcessAndProcessAndProcess979_821C6778, D3DDevice_SetVertexShaderConstantB_hook);  // D3DDevice_SetVertexShaderConstantB  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_8226E0D0, D3DDevice_SetPixelShaderConstantB_hook);  // D3DDevice_SetPixelShaderConstantB  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B92A40, D3DDevice_SetVertexShaderConstantI_hook);  // D3DDevice_SetVertexShaderConstantI  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B92A98, D3DDevice_SetPixelShaderConstantI_hook);  // D3DDevice_SetPixelShaderConstantI  // fable2-nx: fable2-nx's table
BANJO_RENUT_UNHOOKED_HOOK(sub_8263ED90, D3DDevice_EndIndexedVertices_hook);  // D3DDevice_EndIndexedVertices  // fable2-nx: not hooked: fable2-nx's table (unhooked there)

// Render state getters, by their index in N&B's table (off_82E506F0, field 0).
BANJO_RENUT_HOOK(sub_82B90AE8, GetRenderStateHook<10>);  // D3DDevice_GetRenderState_ZEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B00, GetRenderStateHook<11>);  // D3DDevice_GetRenderState_ZFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90AF0, GetRenderStateHook<12>);  // D3DDevice_GetRenderState_ZWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90720, GetRenderStateHook<13>);  // D3DDevice_GetRenderState_FillMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90710, GetRenderStateHook<14>);  // D3DDevice_GetRenderState_CullMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90740, GetRenderStateHook<15>);  // D3DDevice_GetRenderState_AlphaBlendEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90958, GetRenderStateHook<16>);  // D3DDevice_GetRenderState_SeparateAlphaBlendEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B909A0, GetRenderStateHook<17>);  // D3DDevice_GetRenderState_BlendFactor  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90760, GetRenderStateHook<18>);  // D3DDevice_GetRenderState_SrcBlend  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90770, GetRenderStateHook<19>);  // D3DDevice_GetRenderState_DestBlend  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90750, GetRenderStateHook<20>);  // D3DDevice_GetRenderState_BlendOp  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90850, GetRenderStateHook<21>);  // D3DDevice_GetRenderState_SrcBlendAlpha  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B908C0, GetRenderStateHook<22>);  // D3DDevice_GetRenderState_DestBlendAlpha  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B907E0, GetRenderStateHook<23>);  // D3DDevice_GetRenderState_BlendOpAlpha  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90730, GetRenderStateHook<24>);  // D3DDevice_GetRenderState_AlphaTestEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90968, GetRenderStateHook<25>);  // D3DDevice_GetRenderState_AlphaRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90990, GetRenderStateHook<26>);  // D3DDevice_GetRenderState_AlphaFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B10, GetRenderStateHook<27>);  // D3DDevice_GetRenderState_StencilEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B40, GetRenderStateHook<28>);  // D3DDevice_GetRenderState_TwoSidedStencilMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B60, GetRenderStateHook<29>);  // D3DDevice_GetRenderState_StencilFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B70, GetRenderStateHook<30>);  // D3DDevice_GetRenderState_StencilZFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B80, GetRenderStateHook<31>);  // D3DDevice_GetRenderState_StencilPass  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90B50, GetRenderStateHook<32>);  // D3DDevice_GetRenderState_StencilFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C60, GetRenderStateHook<33>);  // D3DDevice_GetRenderState_StencilRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C68, GetRenderStateHook<34>);  // D3DDevice_GetRenderState_StencilMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C70, GetRenderStateHook<35>);  // D3DDevice_GetRenderState_StencilWriteMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90BE8, GetRenderStateHook<36>);  // D3DDevice_GetRenderState_CCWStencilFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C20, GetRenderStateHook<37>);  // D3DDevice_GetRenderState_CCWStencilZFail  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C50, GetRenderStateHook<38>);  // D3DDevice_GetRenderState_CCWStencilPass  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90BB0, GetRenderStateHook<39>);  // D3DDevice_GetRenderState_CCWStencilFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90C90, GetRenderStateHook<40>);  // D3DDevice_GetRenderState_CCWStencilRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90CB0, GetRenderStateHook<41>);  // D3DDevice_GetRenderState_CCWStencilMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90CD0, GetRenderStateHook<42>);  // D3DDevice_GetRenderState_CCWStencilWriteMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90D18, GetRenderStateHook<43>);  // D3DDevice_GetRenderState_ClipPlaneEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90E90, GetRenderStateHook<44>);  // D3DDevice_GetRenderState_PointSize  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90EA0, GetRenderStateHook<45>);  // D3DDevice_GetRenderState_PointSizeMin  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90E88, GetRenderStateHook<46>);  // D3DDevice_GetRenderState_PointSpriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90EB0, GetRenderStateHook<47>);  // D3DDevice_GetRenderState_PointSizeMax  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90D90, GetRenderStateHook<48>);  // D3DDevice_GetRenderState_MultiSampleAntiAlias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90DB8, GetRenderStateHook<49>);  // D3DDevice_GetRenderState_MultiSampleMask  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90D38, GetRenderStateHook<50>);  // D3DDevice_GetRenderState_ScissorTestEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90D40, GetRenderStateHook<51>);  // D3DDevice_GetRenderState_SlopeScaleDepthBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90D60, GetRenderStateHook<52>);  // D3DDevice_GetRenderState_DepthBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90DC0, GetRenderStateHook<53>);  // D3DDevice_GetRenderState_ColorWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90E00, GetRenderStateHook<54>);  // D3DDevice_GetRenderState_ColorWriteEnable1  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90E40, GetRenderStateHook<55>);  // D3DDevice_GetRenderState_ColorWriteEnable2  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90E80, GetRenderStateHook<56>);  // D3DDevice_GetRenderState_ColorWriteEnable3  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91228, GetRenderStateHook<57>);  // D3DDevice_GetRenderState_TessellationMode  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91208, GetRenderStateHook<58>);  // D3DDevice_GetRenderState_MinTessellationLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91218, GetRenderStateHook<59>);  // D3DDevice_GetRenderState_MaxTessellationLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90EC0, GetRenderStateHook<60>);  // D3DDevice_GetRenderState_Wrap0  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90ED0, GetRenderStateHook<61>);  // D3DDevice_GetRenderState_Wrap1  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90EE0, GetRenderStateHook<62>);  // D3DDevice_GetRenderState_Wrap2  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90EF0, GetRenderStateHook<63>);  // D3DDevice_GetRenderState_Wrap3  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F00, GetRenderStateHook<64>);  // D3DDevice_GetRenderState_Wrap4  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F10, GetRenderStateHook<65>);  // D3DDevice_GetRenderState_Wrap5  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F20, GetRenderStateHook<66>);  // D3DDevice_GetRenderState_Wrap6  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F30, GetRenderStateHook<67>);  // D3DDevice_GetRenderState_Wrap7  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F40, GetRenderStateHook<68>);  // D3DDevice_GetRenderState_Wrap8  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F50, GetRenderStateHook<69>);  // D3DDevice_GetRenderState_Wrap9  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F60, GetRenderStateHook<70>);  // D3DDevice_GetRenderState_Wrap10  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F70, GetRenderStateHook<71>);  // D3DDevice_GetRenderState_Wrap11  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F80, GetRenderStateHook<72>);  // D3DDevice_GetRenderState_Wrap12  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90F90, GetRenderStateHook<73>);  // D3DDevice_GetRenderState_Wrap13  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90FA0, GetRenderStateHook<74>);  // D3DDevice_GetRenderState_Wrap14  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90FB0, GetRenderStateHook<75>);  // D3DDevice_GetRenderState_Wrap15  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91000, GetRenderStateHook<76>);  // D3DDevice_GetRenderState_ViewportEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91010, GetRenderStateHook<77>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B910A8, GetRenderStateHook<78>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable1  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91140, GetRenderStateHook<79>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable2  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B911D8, GetRenderStateHook<80>);  // D3DDevice_GetRenderState_HighPrecisionBlendEnable3  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91238, GetRenderStateHook<81>);  // D3DDevice_GetRenderState_HalfPixelOffset  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91268, GetRenderStateHook<82>);  // D3DDevice_GetRenderState_PrimitiveResetEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91298, GetRenderStateHook<83>);  // D3DDevice_GetRenderState_PrimitiveResetIndex  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B912A0, GetRenderStateHook<84>);  // D3DDevice_GetRenderState_AlphaToMaskEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B912B0, GetRenderStateHook<85>);  // D3DDevice_GetRenderState_AlphaToMaskOffsets  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B912F0, GetRenderStateHook<86>);  // D3DDevice_GetRenderState_GuardBand_X  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91320, GetRenderStateHook<87>);  // D3DDevice_GetRenderState_GuardBand_Y  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91348, GetRenderStateHook<88>);  // D3DDevice_GetRenderState_DiscardBand_X  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91378, GetRenderStateHook<89>);  // D3DDevice_GetRenderState_DiscardBand_Y  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913A0, GetRenderStateHook<90>);  // D3DDevice_GetRenderState_HiStencilEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913B0, GetRenderStateHook<91>);  // D3DDevice_GetRenderState_HiStencilWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913C0, GetRenderStateHook<92>);  // D3DDevice_GetRenderState_HiStencilFunc  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913D0, GetRenderStateHook<93>);  // D3DDevice_GetRenderState_HiStencilRef  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913E0, GetRenderStateHook<94>);  // D3DDevice_GetRenderState_PresentInterval  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B913F8, GetRenderStateHook<95>);  // D3DDevice_GetRenderState_PresentImmediateThreshold  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91380, GetRenderStateHook<96>);  // D3DDevice_GetRenderState_HiZEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91390, GetRenderStateHook<97>);  // D3DDevice_GetRenderState_HiZWriteEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90A38, GetRenderStateHook<98>);  // D3DDevice_GetRenderState_LastPixel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90A88, GetRenderStateHook<99>);  // D3DDevice_GetRenderState_LineWidth  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B90AD8, GetRenderStateHook<100>);  // D3DDevice_GetRenderState_Buffer2Frames  // fable2-nx: fable2-nx's table

// Sampler state getters (off_82E50BD0, field 0).
BANJO_RENUT_HOOK(sub_82B91898, GetSamplerStateHook<0>);  // D3DDevice_GetSamplerState_AddressU  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B918B0, GetSamplerStateHook<1>);  // D3DDevice_GetSamplerState_AddressV  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B918C8, GetSamplerStateHook<2>);  // D3DDevice_GetSamplerState_AddressW  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91878, GetSamplerStateHook<3>);  // D3DDevice_GetSamplerState_BorderColor  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B914E8, GetSamplerStateHook<4>);  // D3DDevice_GetSamplerState_MagFilter  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91408, GetSamplerStateHook<5>);  // D3DDevice_GetSamplerState_MinFilter  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B915C0, GetSamplerStateHook<6>);  // D3DDevice_GetSamplerState_MipFilter  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91738, GetSamplerStateHook<7>);  // D3DDevice_GetSamplerState_MipMapLodBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B917E8, GetSamplerStateHook<8>);  // D3DDevice_GetSamplerState_MaxMipLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91680, GetSamplerStateHook<9>);  // D3DDevice_GetSamplerState_MaxAnisotropy  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B915B0, GetSamplerStateHook<10>);  // D3DDevice_GetSamplerState_MagFilterZ  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B914D8, GetSamplerStateHook<11>);  // D3DDevice_GetSamplerState_MinFilterZ  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91670, GetSamplerStateHook<12>);  // D3DDevice_GetSamplerState_SeparateZFilterEnable  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91868, GetSamplerStateHook<13>);  // D3DDevice_GetSamplerState_MinMipLevel  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B918E0, GetSamplerStateHook<14>);  // D3DDevice_GetSamplerState_TrilinearThreshold  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B916F0, GetSamplerStateHook<15>);  // D3DDevice_GetSamplerState_AnisotropyBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91938, GetSamplerStateHook<16>);  // D3DDevice_GetSamplerState_HGradientExpBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91990, GetSamplerStateHook<17>);  // D3DDevice_GetSamplerState_VGradientExpBias  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B919E8, GetSamplerStateHook<18>);  // D3DDevice_GetSamplerState_WhiteBorderColorW  // fable2-nx: fable2-nx's table
BANJO_RENUT_HOOK(sub_82B91A48, GetSamplerStateHook<19>);  // D3DDevice_GetSamplerState_PointBorderEnable  // fable2-nx: fable2-nx's table
