// Hand-tuned override: goto-free rewrite of InterpolateFloatArray_82C62548.
// Jumps/labels from the recompiler output (fable_2_recomp.231.cpp) were
// replaced with nested if/else blocks and while loops; every guest
// load/store, call/return-address pair, compare, and exit path is otherwise
// identical to the generated body, with the same register side effects. The
// first line makes tools/make_hotfunc_overrides.py skip this file.
#include "renut_pch.h"  // fable2-nx: the generated project is "renut" (the base keeps its name)

#include <cmath>
#include <bit>
#include <cstring>

#include <rex/cvar.h>

// fable2-nx (2026-11-17, the town's CPU profile: one game audio thread at 44% of a core, 43% of it in this function).
// One sample of a game audio filter: the circular history X (+12) of N (+36) samples at position +40, dotted with the
// coefficients H (+16); while a count (+8) is above 0 also the adaptive step H[i] += G[i] (G at +20), the count
// decremented. The recompiled body keeps every pointer of its loops in the context (a load and a store per register per
// tap), and each fmadds is a double fma rounded to float and back, so the sum's chain is ~15 cycles a tap. The native
// body does the same taps in the same order with the PowerPC's own single-precision operations (fmadds: one fused
// rounding to float; fadds: a float add), the Xbox 360's results; the recompiled double-then-float rounding differs from
// them in the last bit in rare cases. false = the recompiled body.
REXCVAR_DEFINE_BOOL(fable2_native_audio_filter, true, "Fable2",
                    "The game's per-sample audio filter (InterpolateFloatArray_82C62548) as native code: same taps, "
                    "same order, the PowerPC's single-precision rounding (false: the recompiled body)");

static void InterpolateFloatArray_82C62548_recomp(PPCContext& __restrict ctx, uint8_t* base) {
	REX_FUNC_PROLOGUE();
	PPCRegister temp{};
	// lwz r11,8(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 8);
	// cmpwi cr6,r11,0
	ctx.cr6.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
	// lis r11,-32256
	ctx.r11.s64 = -2113929216;
	// lfs f0,3084(r11)
	ctx.fpscr.disableFlushMode();
	temp.u32 = REX_LOAD_U32(ctx.r11.u32 + 3084);
	ctx.f0.f64 = double(temp.f32);
	// ble cr6,0x82c6266c
	if (!ctx.cr6.gt) {
		// lwz r10,36(r3)
		ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 36);
		// lwz r5,40(r3)
		ctx.r5.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
		// cmpw cr6,r5,r10
		ctx.cr6.compare<int32_t>(ctx.r5.s32, ctx.r10.s32, ctx.xer);
		// bge cr6,0x82c626bc
		if (ctx.cr6.lt) {
			// rotlwi r11,r10,0
			ctx.r11.u64 = __builtin_rotateleft32(ctx.r10.u32, 0);
			// lwz r9,16(r3)
			ctx.r9.u64 = REX_LOAD_U32(ctx.r3.u32 + 16);
			// rlwinm r8,r10,2,0,29
			ctx.r8.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 2) & 0xFFFFFFFC;
			// lwz r7,12(r3)
			ctx.r7.u64 = REX_LOAD_U32(ctx.r3.u32 + 12);
			// rlwinm r6,r5,2,0,29
			ctx.r6.u64 = __builtin_rotateleft64(ctx.r5.u32 | (ctx.r5.u64 << 32), 2) & 0xFFFFFFFC;
			// subf r11,r5,r11
			ctx.r11.u64 = ctx.r11.u64 - ctx.r5.u64;
			// add r9,r8,r9
			ctx.r9.u64 = ctx.r8.u64 + ctx.r9.u64;
			// add r8,r6,r7
			ctx.r8.u64 = ctx.r6.u64 + ctx.r7.u64;
			// subf r10,r11,r10
			ctx.r10.u64 = ctx.r10.u64 - ctx.r11.u64;
			while (true) {
				// lfs f13,0(r9)
				ctx.fpscr.disableFlushMode();
				temp.u32 = REX_LOAD_U32(ctx.r9.u32 + 0);
				ctx.f13.f64 = double(temp.f32);
				// addic. r11,r11,-1
				ctx.xer.ca = ctx.r11.u32 > 0;
				ctx.r11.s64 = ctx.r11.s64 + -1;
				ctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
				// lfs f12,0(r8)
				temp.u32 = REX_LOAD_U32(ctx.r8.u32 + 0);
				ctx.f12.f64 = double(temp.f32);
				// addi r9,r9,-4
				ctx.r9.s64 = ctx.r9.s64 + -4;
				// fmadds f0,f13,f12,f0
				ctx.f0.f64 = double(float(std::fma(ctx.f13.f64, ctx.f12.f64, ctx.f0.f64)));
				// addi r8,r8,4
				ctx.r8.s64 = ctx.r8.s64 + 4;
				// bne 0x82c626a0
				if (!ctx.cr0.eq) continue;
				break;
			}
		}
		// cmpwi cr6,r5,0
		ctx.cr6.compare<int32_t>(ctx.r5.s32, 0, ctx.xer);
		// ble cr6,0x82c626f4
		if (ctx.cr6.gt) {
			// lwz r8,16(r3)
			ctx.r8.u64 = REX_LOAD_U32(ctx.r3.u32 + 16);
			// rlwinm r10,r10,2,0,29
			ctx.r10.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 2) & 0xFFFFFFFC;
			// lwz r9,12(r3)
			ctx.r9.u64 = REX_LOAD_U32(ctx.r3.u32 + 12);
			// lwz r11,40(r3)
			ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
			// add r10,r10,r8
			ctx.r10.u64 = ctx.r10.u64 + ctx.r8.u64;
			while (true) {
				// lfs f13,0(r10)
				ctx.fpscr.disableFlushMode();
				temp.u32 = REX_LOAD_U32(ctx.r10.u32 + 0);
				ctx.f13.f64 = double(temp.f32);
				// addic. r11,r11,-1
				ctx.xer.ca = ctx.r11.u32 > 0;
				ctx.r11.s64 = ctx.r11.s64 + -1;
				ctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
				// lfs f12,0(r9)
				temp.u32 = REX_LOAD_U32(ctx.r9.u32 + 0);
				ctx.f12.f64 = double(temp.f32);
				// addi r10,r10,-4
				ctx.r10.s64 = ctx.r10.s64 + -4;
				// fmadds f0,f13,f12,f0
				ctx.f0.f64 = double(float(std::fma(ctx.f13.f64, ctx.f12.f64, ctx.f0.f64)));
				// addi r9,r9,4
				ctx.r9.s64 = ctx.r9.s64 + 4;
				// bne 0x82c626d8
				if (!ctx.cr0.eq) continue;
				break;
			}
		}
		// lwz r11,12(r3)
		ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 12);
		// rlwinm r10,r5,2,0,29
		ctx.r10.u64 = __builtin_rotateleft64(ctx.r5.u32 | (ctx.r5.u64 << 32), 2) & 0xFFFFFFFC;
		// stfsx f1,r10,r11
		ctx.fpscr.disableFlushMode();
		temp.f32 = float(ctx.f1.f64);
		REX_STORE_U32_C(ctx.r10.u32 + ctx.r11.u32, temp.u32);
		// lwz r9,36(r3)
		ctx.r9.u64 = REX_LOAD_U32(ctx.r3.u32 + 36);
		// lwz r11,40(r3)
		ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
		// addi r11,r11,1
		ctx.r11.s64 = ctx.r11.s64 + 1;
		// stw r11,40(r3)
		REX_STORE_U32_C(ctx.r3.u32 + 40, ctx.r11.u32);
		// cmpw cr6,r11,r9
		ctx.cr6.compare<int32_t>(ctx.r11.s32, ctx.r9.s32, ctx.xer);
		// blt cr6,0x82c62720
		if (!ctx.cr6.lt) {
			// li r11,0
			ctx.r11.s64 = 0;
			// stw r11,40(r3)
			REX_STORE_U32_C(ctx.r3.u32 + 40, ctx.r11.u32);
		}
		// lwz r11,16(r3)
		ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 16);
		// lfs f13,0(r11)
		ctx.fpscr.disableFlushMode();
		temp.u32 = REX_LOAD_U32(ctx.r11.u32 + 0);
		ctx.f13.f64 = double(temp.f32);
		// fmadds f1,f13,f1,f0
		ctx.f1.f64 = double(float(std::fma(ctx.f13.f64, ctx.f1.f64, ctx.f0.f64)));
		// blr 
		return;
	}
	// lwz r7,36(r3)
	ctx.r7.u64 = REX_LOAD_U32(ctx.r3.u32 + 36);
	// lwz r10,40(r3)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
	// cmpw cr6,r10,r7
	ctx.cr6.compare<int32_t>(ctx.r10.s32, ctx.r7.s32, ctx.xer);
	// bge cr6,0x82c625b8
	if (ctx.cr6.lt) {
		// rlwinm r11,r7,2,0,29
		ctx.r11.u64 = __builtin_rotateleft64(ctx.r7.u32 | (ctx.r7.u64 << 32), 2) & 0xFFFFFFFC;
		// rlwinm r9,r10,2,0,29
		ctx.r9.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 2) & 0xFFFFFFFC;
		while (true) {
			// lwz r8,16(r3)
			ctx.r8.u64 = REX_LOAD_U32(ctx.r3.u32 + 16);
			// addi r10,r10,1
			ctx.r10.s64 = ctx.r10.s64 + 1;
			// lwz r6,20(r3)
			ctx.r6.u64 = REX_LOAD_U32(ctx.r3.u32 + 20);
			// addi r7,r7,-1
			ctx.r7.s64 = ctx.r7.s64 + -1;
			// add r8,r8,r11
			ctx.r8.u64 = ctx.r8.u64 + ctx.r11.u64;
			// lwz r5,12(r3)
			ctx.r5.u64 = REX_LOAD_U32(ctx.r3.u32 + 12);
			// lfsx f13,r6,r11
			ctx.fpscr.disableFlushMode();
			temp.u32 = REX_LOAD_U32(ctx.r6.u32 + ctx.r11.u32);
			ctx.f13.f64 = double(temp.f32);
			// addi r11,r11,-4
			ctx.r11.s64 = ctx.r11.s64 + -4;
			// lfs f12,0(r8)
			temp.u32 = REX_LOAD_U32(ctx.r8.u32 + 0);
			ctx.f12.f64 = double(temp.f32);
			// lfsx f11,r5,r9
			temp.u32 = REX_LOAD_U32(ctx.r5.u32 + ctx.r9.u32);
			ctx.f11.f64 = double(temp.f32);
			// fadds f10,f13,f12
			ctx.f10.f64 = double(float(ctx.f13.f64 + ctx.f12.f64));
			// stfs f10,0(r8)
			temp.f32 = float(ctx.f10.f64);
			REX_STORE_U32_C(ctx.r8.u32 + 0, temp.u32);
			// lwz r4,36(r3)
			ctx.r4.u64 = REX_LOAD_U32(ctx.r3.u32 + 36);
			// fmadds f0,f11,f12,f0
			ctx.f0.f64 = double(float(std::fma(ctx.f11.f64, ctx.f12.f64, ctx.f0.f64)));
			// addi r9,r9,4
			ctx.r9.s64 = ctx.r9.s64 + 4;
			// cmpw cr6,r10,r4
			ctx.cr6.compare<int32_t>(ctx.r10.s32, ctx.r4.s32, ctx.xer);
			// blt cr6,0x82c62574
			if (ctx.cr6.lt) continue;
			break;
		}
	}
	// lwz r11,40(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
	// li r8,0
	ctx.r8.s64 = 0;
	// cmpwi cr6,r11,0
	ctx.cr6.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
	// ble cr6,0x82c62610
	if (ctx.cr6.gt) {
		// rlwinm r11,r7,2,0,29
		ctx.r11.u64 = __builtin_rotateleft64(ctx.r7.u32 | (ctx.r7.u64 << 32), 2) & 0xFFFFFFFC;
		// li r10,0
		ctx.r10.s64 = 0;
		while (true) {
			// lwz r9,16(r3)
			ctx.r9.u64 = REX_LOAD_U32(ctx.r3.u32 + 16);
			// addi r8,r8,1
			ctx.r8.s64 = ctx.r8.s64 + 1;
			// lwz r7,20(r3)
			ctx.r7.u64 = REX_LOAD_U32(ctx.r3.u32 + 20);
			// add r9,r9,r11
			ctx.r9.u64 = ctx.r9.u64 + ctx.r11.u64;
			// lwz r6,12(r3)
			ctx.r6.u64 = REX_LOAD_U32(ctx.r3.u32 + 12);
			// lfsx f13,r7,r11
			ctx.fpscr.disableFlushMode();
			temp.u32 = REX_LOAD_U32(ctx.r7.u32 + ctx.r11.u32);
			ctx.f13.f64 = double(temp.f32);
			// addi r11,r11,-4
			ctx.r11.s64 = ctx.r11.s64 + -4;
			// lfs f12,0(r9)
			temp.u32 = REX_LOAD_U32(ctx.r9.u32 + 0);
			ctx.f12.f64 = double(temp.f32);
			// lfsx f11,r6,r10
			temp.u32 = REX_LOAD_U32(ctx.r6.u32 + ctx.r10.u32);
			ctx.f11.f64 = double(temp.f32);
			// fadds f10,f13,f12
			ctx.f10.f64 = double(float(ctx.f13.f64 + ctx.f12.f64));
			// stfs f10,0(r9)
			temp.f32 = float(ctx.f10.f64);
			REX_STORE_U32_C(ctx.r9.u32 + 0, temp.u32);
			// lwz r5,40(r3)
			ctx.r5.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
			// fmadds f0,f11,f12,f0
			ctx.f0.f64 = double(float(std::fma(ctx.f11.f64, ctx.f12.f64, ctx.f0.f64)));
			// addi r10,r10,4
			ctx.r10.s64 = ctx.r10.s64 + 4;
			// cmpw cr6,r8,r5
			ctx.cr6.compare<int32_t>(ctx.r8.s32, ctx.r5.s32, ctx.xer);
			// blt cr6,0x82c625d0
			if (ctx.cr6.lt) continue;
			break;
		}
	}
	// lwz r11,40(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
	// lwz r10,12(r3)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 12);
	// rlwinm r9,r11,2,0,29
	ctx.r9.u64 = __builtin_rotateleft64(ctx.r11.u32 | (ctx.r11.u64 << 32), 2) & 0xFFFFFFFC;
	// stfsx f1,r9,r10
	ctx.fpscr.disableFlushMode();
	temp.f32 = float(ctx.f1.f64);
	REX_STORE_U32_C(ctx.r9.u32 + ctx.r10.u32, temp.u32);
	// lwz r8,36(r3)
	ctx.r8.u64 = REX_LOAD_U32(ctx.r3.u32 + 36);
	// lwz r11,40(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 40);
	// addi r11,r11,1
	ctx.r11.s64 = ctx.r11.s64 + 1;
	// stw r11,40(r3)
	REX_STORE_U32_C(ctx.r3.u32 + 40, ctx.r11.u32);
	// cmpw cr6,r11,r8
	ctx.cr6.compare<int32_t>(ctx.r11.s32, ctx.r8.s32, ctx.xer);
	// blt cr6,0x82c62640
	if (!ctx.cr6.lt) {
		// li r11,0
		ctx.r11.s64 = 0;
		// stw r11,40(r3)
		REX_STORE_U32_C(ctx.r3.u32 + 40, ctx.r11.u32);
	}
	// lwz r11,16(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 16);
	// lwz r10,20(r3)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 20);
	// lfs f13,0(r11)
	ctx.fpscr.disableFlushMode();
	temp.u32 = REX_LOAD_U32(ctx.r11.u32 + 0);
	ctx.f13.f64 = double(temp.f32);
	// lfs f12,0(r10)
	temp.u32 = REX_LOAD_U32(ctx.r10.u32 + 0);
	ctx.f12.f64 = double(temp.f32);
	// fmadds f1,f13,f1,f0
	ctx.f1.f64 = double(float(std::fma(ctx.f13.f64, ctx.f1.f64, ctx.f0.f64)));
	// fadds f11,f12,f13
	ctx.f11.f64 = double(float(ctx.f12.f64 + ctx.f13.f64));
	// stfs f11,0(r11)
	temp.f32 = float(ctx.f11.f64);
	REX_STORE_U32_C(ctx.r11.u32 + 0, temp.u32);
	// lwz r11,8(r3)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 8);
	// addi r9,r11,-1
	ctx.r9.s64 = ctx.r11.s64 + -1;
	// stw r9,8(r3)
	REX_STORE_U32_C(ctx.r3.u32 + 8, ctx.r9.u32);
	// blr 
	return;
}

// The recompiled code's own accessors (renut_pch.h): the physical-window offset, and a store's card mark
// (banjo_renut_store_cards: Renut proves its cached copies of guest memory by them).
#define F2_LOAD_F(a) std::bit_cast<float>(uint32_t(REX_LOAD_U32(a)))
#define F2_STORE_F(a, f) REX_STORE_U32_C((a), std::bit_cast<uint32_t>(float(f)))

extern "C" void InterpolateFloatArray_82C62548(PPCContext& __restrict ctx, uint8_t* base) {
	static const bool native = REXCVAR_GET(fable2_native_audio_filter);
	if (!native) {
		InterpolateFloatArray_82C62548_recomp(ctx, base);
		return;
	}
	REX_FUNC_PROLOGUE();
	const uint32_t s = ctx.r3.u32;
	ctx.fpscr.disableFlushMode();
	float acc = F2_LOAD_F(0x82000000u + 3084u);  // lis r11,-32256; lfs f0,3084(r11)
	const float in = float(ctx.f1.f64);
	const int32_t count = int32_t(uint32_t(REX_LOAD_U32(s + 8)));
	const int32_t n = int32_t(uint32_t(REX_LOAD_U32(s + 36)));
	const int32_t pos = int32_t(uint32_t(REX_LOAD_U32(s + 40)));
	const uint32_t x = uint32_t(REX_LOAD_U32(s + 12));
	const uint32_t h = uint32_t(REX_LOAD_U32(s + 16));
	// The second loop's top index: pos after the first loop ran, N when it did not (r10 / r7 in the recompiled body).
	int32_t top = n;
	// ble cr6 (count <= 0): the plain filter; a positive count also adapts H and counts down.
	if (count <= 0) {
		if (pos < n) {
			// for k in 0 .. N - pos - 1: acc += H[N - k] * X[pos + k]
			uint32_t hp = h + uint32_t(n) * 4u, xp = x + uint32_t(pos) * 4u;
			for (int32_t k = n - pos; k != 0; --k, hp -= 4u, xp += 4u)
				acc = std::fmaf(F2_LOAD_F(hp), F2_LOAD_F(xp), acc);
			top = pos;
		}
		if (pos > 0) {
			// for j in 0 .. pos - 1: acc += H[top - j] * X[j]
			uint32_t hp = h + uint32_t(top) * 4u, xp = x;
			for (int32_t j = pos; j != 0; --j, hp -= 4u, xp += 4u)
				acc = std::fmaf(F2_LOAD_F(hp), F2_LOAD_F(xp), acc);
		}
		F2_STORE_F(x + uint32_t(pos) * 4u, in);
		const int32_t next = pos + 1;
		REX_STORE_U32_C(s + 40, next < n ? uint32_t(next) : 0u);
		const float out = std::fmaf(F2_LOAD_F(h), in, acc);
		ctx.f0.f64 = double(acc);
		ctx.f1.f64 = double(out);
		return;
	}
	const uint32_t g = uint32_t(REX_LOAD_U32(s + 20));
	if (pos < n) {
		// for idx = N down to pos + 1: old = H[idx]; H[idx] = G[idx] + old; acc += X[pos + N - idx] * old
		uint32_t off = uint32_t(n) * 4u, xp = x + uint32_t(pos) * 4u;
		for (int32_t i = pos; i < n; ++i, off -= 4u, xp += 4u) {
			const float old = F2_LOAD_F(h + off);
			F2_STORE_F(h + off, F2_LOAD_F(g + off) + old);
			acc = std::fmaf(F2_LOAD_F(xp), old, acc);
		}
		top = pos;
	}
	if (pos > 0) {
		uint32_t off = uint32_t(top) * 4u, xp = x;
		for (int32_t j = 0; j < pos; ++j, off -= 4u, xp += 4u) {
			const float old = F2_LOAD_F(h + off);
			F2_STORE_F(h + off, F2_LOAD_F(g + off) + old);
			acc = std::fmaf(F2_LOAD_F(xp), old, acc);
		}
	}
	F2_STORE_F(x + uint32_t(pos) * 4u, in);
	const int32_t next = pos + 1;
	REX_STORE_U32_C(s + 40, next < n ? uint32_t(next) : 0u);
	const float h0 = F2_LOAD_F(h), g0 = F2_LOAD_F(g);
	const float out = std::fmaf(h0, in, acc);
	F2_STORE_F(h, g0 + h0);
	REX_STORE_U32_C(s + 8, uint32_t(count - 1));
	ctx.f0.f64 = double(acc);
	ctx.f1.f64 = double(out);
}
