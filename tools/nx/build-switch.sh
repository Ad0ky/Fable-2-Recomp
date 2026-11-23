#!/usr/bin/env bash
# Builds fable2-nx.nro (Fable II for the Nintendo Switch) on renut_nx_ultimate_base, in the Docker image
# fable2-switch-dev:3 (tools/nx/Dockerfile: devkitA64, libnx, deko3d, CMake, Ninja, Clang 20 for the host tools).
# The base's Windows steps (tools/build_host.bat, codegen.ps1, post_codegen.py, switch_shaders.ps1, build_switch.bat),
# in this order:
#   1. the host code generator (sdk/, Clang 20) -> out/build/host;
#   2. code generation (app/renut_manifest.toml, the image dump out/renut_image.bin), the post-codegen passes
#      (tools/post_codegen.py, every step, the machine copies last) and the per-game audits (tools/nx/codegen-audit.sh);
#   3. the shaders when app/out/renut_switch has none (tools/nx/build-shaders.sh);
#   4. the NRO (app/, the base's toolchain file tools/switch/cmake/switch-devkitA64.cmake): deko3d only, no NVK.
#   run-switch-build.bat starts it:  docker run --rm -v "%CD%:/work" -w /work fable2-switch-dev:3 bash tools/nx/build-switch.sh
# Inputs kept out of Git: app/assets/default.xex (run-switch-build.bat copies it from your game folder).
# Environment:
#   JOBS=N        parallel jobs (default: CPUs; lower it if the container runs out of memory)
#   CODEGEN=1     run the code generator again (and the post-codegen passes) even if app/generated exists
#   POSTCODEGEN=1 the post-codegen passes again on the existing tree (a new hot list: the machine copies)
#   CLEAN=1       delete the build folder first
#   SHADERS=1     only the shaders (build-shaders.sh), then stop
#   BUILD_DIR=... another build folder (default out/build/switch); CMAKE_EXTRA=... more configure options for a new one
#                 (docs/building.md: -DBANJO_PGO=generate|use -DBANJO_SWITCH_LTO=ON, -DSASRT_HOT_GUARD_FORMS=ON ...)
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
JOBS="${JOBS:-$(nproc)}"
build="${BUILD_DIR:-$root/out/build/switch}"
hostbuild="$root/out/build/host"
log() { printf '\n==== %s ====\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

log "Toolchain"
[ -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" ] || die "devkitA64 not found under $DEVKITPRO"
[ -f "$DEVKITPRO/libnx/lib/libnx.a" ] || die "libnx not found under $DEVKITPRO/libnx"
"$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" --version | head -n1
HOST_CC="${HOST_CC:-$(command -v clang-20 || command -v clang)}"
HOST_CXX="${HOST_CXX:-$(command -v clang++-20 || command -v clang++)}"
export HOST_CC HOST_CXX
"$HOST_CXX" --version | head -n1
cmake --version | head -n1
python3 -c "import xxhash" 2>/dev/null || pip3 install --break-system-packages -q xxhash 2>/dev/null || \
    pip3 install -q xxhash 2>/dev/null || echo "warning: python xxhash missing"
git config --global --add safe.directory '*' 2>/dev/null || true

if [ "${SHADERS:-0}" = 1 ]; then
    log "Shaders (tools/nx/build-shaders.sh)"
    JOBS="$JOBS" bash tools/nx/build-shaders.sh
    log "Done"
    exit 0
fi

# FFmpeg's generated headers (not in the SDK's tree): tools/nx/ffmpeg, copied over every time.
cp -f tools/nx/ffmpeg/config.h sdk/thirdparty/FFmpeg/config.h
cp -f tools/nx/ffmpeg/libavutil/avconfig.h tools/nx/ffmpeg/libavutil/ffversion.h sdk/thirdparty/FFmpeg/libavutil/

[ -f app/assets/default.xex ] || die "app/assets/default.xex missing (run-switch-build.bat copies it from your game folder)"

if [ "${CODEGEN:-0}" = 1 ] || [ ! -f app/generated/sources.cmake ]; then
    log "Host code generator (rexglue)"
    if [ -f "$hostbuild/CMakeCache.txt" ] && ! grep -q "^CMAKE_CXX_COMPILER:[A-Z]*=$HOST_CXX\$" "$hostbuild/CMakeCache.txt"; then
        rm -rf "$hostbuild"
    fi
    cmake -S sdk -B "$hostbuild" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$HOST_CC" -DCMAKE_CXX_COMPILER="$HOST_CXX" \
        -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF \
        -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2
    cmake --build "$hostbuild" --target rexglue -j "$JOBS"
    rexglue="$(find "$hostbuild" "$root/sdk/out" -type f -name rexglue -perm -u+x 2>/dev/null | head -n1)"
    [ -n "$rexglue" ] || die "rexglue was not built"
    echo "rexglue: $rexglue"
    mkdir -p out/audit
    gen() {  # gen <manifest> <log>
        # --ignore-stamp: the generator skips a module whose inputs are unchanged (codegen.stamp), which would leave
        # the post-codegen passes' tree in place for the audits below (they then read every rewritten prologue).
        (cd app && REXGLUE_DUMP_IMAGE="$root/out/renut_image.bin" "$rexglue" codegen --ignore-stamp "$1") > "$2" 2>&1 \
            || { tail -n 60 "$2"; die "codegen failed ($2)"; }
        tail -n 4 "$2"
    }
    # The base's register-diet audits, written into the config (tools/nx/codegen_lists.py), then the code. The
    # generation without non_argument_as_local comes first: every generation writes app/generated/rexglue.cmake (naming
    # its own output folder), so the last one must be the real one.
    log "Register audits: non_argument_as_local (a code generation without it -> out/audit/gen_noarg)"
    gen audit_noarg_manifest.toml out/audit/codegen_noarg.log
    keep_out="$(python3 tools/nx/codegen_lists.py keep out/audit/gen_noarg)"; echo "$keep_out"
    rm -rf out/audit/gen_noarg
    log "Code generation (rexglue codegen app/renut_manifest.toml)"
    gen renut_manifest.toml out/codegen.log
    [ -f app/generated/sources.cmake ] || die "codegen wrote no app/generated/sources.cmake"
    for round in 1 2 3; do
        log "Register audits: non_volatile_as_local (round $round)"
        share_out="$(python3 tools/nx/codegen_lists.py share app/generated)"; echo "$share_out"
        if echo "$share_out" | grep -q "NEW 0$"; then break; fi
        log "Code generation again with the lists"
        gen renut_manifest.toml out/codegen.log
    done
    log "Register audits on the final code (tools/nx/codegen-audit.sh raw)"
    bash tools/nx/codegen-audit.sh raw
    POSTCODEGEN=1
fi
if [ "${POSTCODEGEN:-0}" = 1 ]; then
    log "Post-codegen passes (tools/post_codegen.py)"
    REXSDK_DIR="$root/sdk" python3 tools/post_codegen.py 2>&1 | tee out/post_codegen.log | tail -n 40
    [ "${PIPESTATUS[0]}" = 0 ] || die "post_codegen.py failed (out/post_codegen.log)"
    log "Calling-convention census (tools/nx/codegen-audit.sh final)"
    bash tools/nx/codegen-audit.sh final
fi
touch app/generated/codegen.build.stamp
grep -q 'out/audit' app/generated/rexglue.cmake 2>/dev/null && die "app/generated/rexglue.cmake names the audit's folder: run with codegen"

if [ ! -f app/out/renut_switch/shader_cache_switch.cpp ] || [ ! -f app/out/renut_switch/deko3d_glsl_switch.cpp ]; then
    log "Shaders (tools/nx/build-shaders.sh)"
    JOBS="$JOBS" bash tools/nx/build-shaders.sh
fi
JOBS="$JOBS" bash tools/nx/build-shader-tools.sh

if [ "${CLEAN:-0}" = 1 ]; then rm -rf "$build"; fi
tcsum="$(sha256sum tools/switch/cmake/switch-devkitA64.cmake | cut -d' ' -f1)"
if [ -f "$build/CMakeCache.txt" ] && [ "$(cat "$build/.toolchain-sha" 2>/dev/null)" != "$tcsum" ]; then
    rm -f "$build/CMakeCache.txt"
fi
mkdir -p "$build" && echo "$tcsum" > "$build/.toolchain-sha"
# Mesa's NVK SDK (portlibs/switch): not linked (REXGLUE_SWITCH_NVK OFF, deko3d only), but plume takes the Vulkan headers
# from it and checks its libvulkan.a is there.
if [ -z "${NVK_LIBRARY:-}" ]; then
    NVK_LIBRARY="$(find "$DEVKITPRO" -name libvulkan.a -path '*switch*' 2>/dev/null | head -n1 || true)"
fi
[ -n "$NVK_LIBRARY" ] && [ -f "$NVK_LIBRARY" ] || die "mesa-switch libvulkan.a not found under $DEVKITPRO; set NVK_LIBRARY"
NVK_SDK="$(dirname "$(dirname "$NVK_LIBRARY")")"
echo "NVK SDK (headers only): $NVK_SDK"
log "Configure"
# Every time, with every option (a first configure that stopped leaves a cache without them).
if true; then
    # shellcheck disable=SC2086
    cmake -S app -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DREXGLUE_SWITCH_NVK_SDK="$NVK_SDK" \
        -DCMAKE_TOOLCHAIN_FILE="$root/tools/switch/cmake/switch-devkitA64.cmake" \
        -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
        -DDEVKITPRO="$DEVKITPRO" -DREXSDK_DIR="$root/sdk" -DREXGLUE_SWITCH_NVK=OFF \
        -DBANJO_SWITCH_RENDERER_LINES=ON \
        -DBANJO_DEKO3D_SPIRV_CROSS="$root/out/deko3d_tools/spirv-cross-build/spirv-cross" \
        -DBANJO_DEKO3D_UAM="$root/out/deko3d_tools/uam-build/uam" \
        -DBANJO_GLSLANG_VALIDATOR="$root/out/glslang-linux/StandAlone/glslangValidator" \
        -DPYTHON_EXECUTABLE="$(command -v python3)" -DPython3_EXECUTABLE="$(command -v python3)" ${CMAKE_EXTRA:-}
fi

log "Build ($JOBS jobs)"
# -k 0: keep going after a failure so one run reports every failing file.
cmake --build "$build" --target renut -j "$JOBS" -- -k 0

out="$root/out/switch"
mkdir -p "$out"
nro="$(find "$build" -maxdepth 3 -name renut.nro | head -n1)"
[ -n "$nro" ] || die "no renut.nro in $build"
cp "$nro" "$out/fable2-nx.nro"
elf="$(dirname "$nro")/renut.elf"
[ -f "$elf" ] || elf="$(dirname "$nro")/renut"
cp "$elf" "$out/fable2-nx.elf"
[ -f "$build/renut.map" ] && cp "$build/renut.map" "$out/fable2-nx.map" || true
log "Done"
echo "NRO: out/switch/fable2-nx.nro  (sdmc:/switch/fable2-nx/)"
echo "ELF: out/switch/fable2-nx.elf  (keep it: crash addresses and console profiles resolve against it)"
