# Fable 2 NX on the ultimate base: the rebase plan

Started 2026-11-12 at the user's request: "fully rebase off this ultimate base, graft any unique features of your
native renderer onto this base, do codegen again, make machine copies again ... keep all the default settings on the
base on as they are". Decisions (asked 2026-11-12): a new folder (`D:\_Projects\Fable2Nx\fable2-ultimate`, a new local
repo from the base's files, `fable2-nx` stays as the reference), the build in the Docker image (as `fable2-nx`), machine
copies of the hot set as hand-and-machine-optimising-guide.md says (console profiles and PGO counts, not every function).

Base: ChanseyIsTheBest/renut_nx_ultimate_base 3182547d (ports 1-122), first commit unchanged.

## What has to move

| Part | From fable2-nx | Onto the base | Notes |
|---|---|---|---|
| Device layout | Fable = N&B base game (XDK 2.0.7645; 292 of 295 paired D3D functions with the same displacements) | Renut stands at Katamari's XDK 2.0.5632 (`tools/renut_device_offsets_katamari.py` zones from N&B TU3 2.0.7978) | Katamari -> TU3 (the zones, inverted, on the lines marked `katamari-nx`), then TU3 -> base game (renut-nx docs/title-update-3.md: +4 from 0x2AC0, +8 from 0x2E2C, the same from 0x3390); checked against fable2-nx's kDeviceOffset* (26 constants) |
| Guest data addresses | fable2-nx docs/nx/renut-import.md table | `DATA` of the same tool | state tables 83315D30 / 833161F0, format table 82006F28, VdGlobalDevice 820009AC, caps 820FA890, XDK qword 83321A70, HiZ owner 83491F80, full viewport 82001AA8, export skip words 83496F60/64, no-large-pages 83336BFC, XMemFree 822023F0 |
| Hook addresses | docs/nx/d3d-hookmap-fable2.tsv (N&B name -> Fable address) | the hook macros' comments name the D3D function | by name; the base's Bind is (shader, flags, decl, strides, pixel shader) as at later XDKs |
| Fable's own D3D hooks | CDevice::Initialize 82BA0220 (no Direct3D_CreateDevice), QueryBufferSpace 8217ECD8, IDirect3DQuery9::GetData 8220BE78, GpuLoadShaders 82221978, BeginIndexedVertices 82217EE8, the bank shaders 82B84550, LOAD_ALU_CONSTANT 8227CFC8, the ring's constant registers, disable MSAA 8238DE58 | the base's hooks where it has its own (occlusion queries, strip restarts, recorded binds), else grafted | each checked against the base's form first |
| Game layer | src/ (26k lines: app, input, lua, menu, diagnostics, hot functions), fable_2_manifest.toml (49k lines), recomp patches | replaces app/src/katamari_*, the Katamari tomls, lists and natives | the base's renut_app.h start-up (renderer, copies, PGO, the switch defaults toml) kept |
| SDK | himdo's runtime fixes (thirdparty/*.patch: xmemory alloc fixes, the main menu crash fix, audio, frame limiter) | the base's sdk/ (998 files differ from fable2-nx's) | only Fable's own fixes; the SDK's Xenos GPU path changes are not needed under Renut |
| Codegen | himdo's manifest keys | the base's renut_config.toml keys | the keys "with no per-game audit" kept; the audited ones (non_volatile_as_local, elide_lr_stores, non_argument_as_local) only after their audits on Fable |
| Build | tools/nx (Docker image fable2-switch-dev:3) | the base's .bat / .ps1 (Windows clang, VS, devkitPro) | ported to the Docker image |
| Machine copies | none | localize_native.py, switch_hot_bulk.py | after the first console profiles of this build |

## Order

1. Repo from the base (done). 2. Docker build. 3. Layout, data, hooks, Fable hooks. 4. Game layer and SDK fixes.
5. Codegen (calling-convention census), shaders, DKSH cache, first NRO for the console. 6. Console profiles -> machine
copies in place (guard build to verify), then PGO + LTO when the user says.
