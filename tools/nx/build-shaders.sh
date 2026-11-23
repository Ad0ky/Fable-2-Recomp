#!/usr/bin/env bash
# fable2-nx on renut_nx_ultimate_base: the game's shaders for Renut's deko3d renderer (the base's
# tools/switch_shaders.ps1, run on Linux in the Docker image). Its steps:
#   1. the host tools (build-shader-tools.sh) and Renut's XenosRecomp, with the host Clang;
#   2. the dump folder XenosRecomp reads (out/renut_shader_dump): every shader of the game's shader banks
#      (tools/fable2_shader_bank.py on app/assets/shaders/*.sbk, 2026-11-23), the containers stored in default.xex
#      (tools/renut_shader_dump.py on the generator's image dump, out/renut_image.bin) and the shaders the console
#      captured (out/shader_capture: copy the SD card's /switch/fable2-nx/shader_capture there; fable2-nx's captures
#      count too);
#   3. XenosRecomp twice: the pixel shaders' HLSL for the early alpha pass (tools/renut_early_alpha.py), then the cache
#      (out/renut_host/renut_gpu/shader_cache.cpp) and the deko3d SPIR-V (out/deko3d_shaders/spirv);
#   4. tools/renut_switch_shader_cache.py -> app/out/renut_switch/shader_cache_switch.cpp;
#   5. tools/deko3d_shaders.py, deko3d_glsl_opt.py (every pass, checked with uam), deko3d_shader_package.py
#      -> app/out/renut_switch/deko3d_glsl*.{bin,cpp};
#   6. the DKSH cache (tools/deko3d_variant_cache.py --generic): every shader's first variant compiled here, so the
#      console does not compile them while it draws (the base's README: build it before a console measurement).
# Everything here is game data: out/ and app/out/ only, never committed.
#   bash tools/nx/build-shaders.sh            (SHADER_CAPTURE_DIR=<folder> to read another one)
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
JOBS="${JOBS:-$(nproc)}"
CC="${HOST_CC:-$(command -v clang-20 || command -v clang)}"
CXX="${HOST_CXX:-$(command -v clang++-20 || command -v clang++)}"
captures="${SHADER_CAPTURE_DIR:-$root/out/shader_capture}"
X="$root/app/thirdparty/reblue-XenosRecomp"
common="$X/XenosRecomp/shader_common.h"
dxc_lib="$X/thirdparty/dxc-bin/lib/x64"
dump="$root/out/renut_shader_dump"
host="$root/out/renut_host"
deko="$root/out/deko3d_shaders"
switch_out="$root/app/out/renut_switch"
export DEKO3D_TOOLS="$root/out/deko3d_tools"
command -v zstd >/dev/null || { echo "error: zstd missing (the image's Dockerfile installs it)" >&2; exit 1; }

bash tools/nx/build-shader-tools.sh

echo "==== XenosRecomp (Renut) ===="
cmake -S "$X" -B "$root/out/xenos_recomp_host" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DXENOS_RECOMP_RENUT=ON -DXENOS_RECOMP_DXIL=OFF > /dev/null
cmake --build "$root/out/xenos_recomp_host" --target XenosRecomp -j "$JOBS"
xr="$root/out/xenos_recomp_host/XenosRecomp/XenosRecomp"

echo "==== the shaders ===="
mkdir -p "$dump"
if [ -f out/renut_image.bin ]; then
    python3 tools/renut_shader_dump.py --image out/renut_image.bin --out "$dump" || \
        echo "warning: renut_shader_dump.py failed; the captures only"
fi
shopt -s nullglob
caps=("$captures"/*.bin)
# fable2-nx (2026-11-23): every shader of the game's shader banks (data/Shaders/*.sbk, which run-switch-build.bat copies
# to app/assets/shaders), not only the ones a run drew; and the dump's files not named by their content's hash removed
# (fable2-nx's old captures: the same shaders again, under keys no draw looks up).
banks=(app/assets/shaders/*.sbk)
if [ ${#banks[@]} -gt 0 ]; then
    python3 tools/fable2_shader_bank.py --bank "${banks[@]}" --out "$dump" || echo "warning: fable2_shader_bank.py failed"
else
    echo "banks: none in app/assets/shaders (run-switch-build.bat copies the game's data/Shaders/*.sbk there)"
fi
if [ ${#caps[@]} -gt 0 ]; then
    cp -n "${caps[@]}" "$dump/"
    echo "captures: ${#caps[@]} in $captures"
else
    echo "captures: none in $captures (copy the SD card's /switch/fable2-nx/shader_capture there)"
fi
python3 tools/fable2_shader_bank.py --out "$dump" --prune --bank || true
n=$(ls "$dump"/*.bin 2>/dev/null | wc -l)
[ "$n" -gt 0 ] || { echo "error: no shaders in $dump" >&2; exit 1; }
echo "dump: $n shaders in $dump"

echo "==== XenosRecomp: early alpha pass ===="
mkdir -p "$host/renut_gpu"
ea="$host/renut_gpu/early_alpha_hlsl"
rm -rf "$ea"; mkdir -p "$ea"
BANJO_HLSL_DUMP_ONLY=1 LD_LIBRARY_PATH="$dxc_lib" "$xr" "$dump" "$host/renut_gpu/early_alpha_pass1.cpp" "$common" "$ea" \
    > "$host/xenos_recomp_pass1.log" 2>&1 || true
python3 tools/renut_early_alpha.py "$ea"

echo "==== XenosRecomp: shader cache and deko3d SPIR-V ===="
rm -rf "$deko/spirv"
set +e
BANJO_HLSL_OVERRIDE="$ea" BANJO_DEKO3D_SPIRV_DIR="$deko/spirv" LD_LIBRARY_PATH="$dxc_lib" \
    "$xr" "$dump" "$host/renut_gpu/shader_cache.cpp" "$common" > "$host/xenos_recomp.log" 2>&1
rc=$?
set -e
grep -A40 "Recompile failures" "$host/xenos_recomp.log" | grep "hash=" || true
[ -f "$host/renut_gpu/shader_cache.cpp" ] || { echo "error: XenosRecomp wrote no cache (rc $rc), see $host/xenos_recomp.log" >&2; exit 1; }
echo "XenosRecomp: rc $rc, $(ls "$deko/spirv" | grep -c '\.spv$') SPIR-V"

echo "==== Switch shader cache ===="
python3 tools/renut_switch_shader_cache.py --pc "$host" --out-dir "$switch_out"

echo "==== deko3d GLSL ===="
python3 tools/deko3d_shaders.py --spirv "$deko/spirv" --out "$deko" --jobs "$JOBS"
python3 tools/deko3d_glsl_opt.py --in "$deko/glsl" --out "$deko/glsl_opt" --check --jobs "$JOBS"
python3 tools/deko3d_shader_package.py --glsl "$deko/glsl_opt" --out "$switch_out"

echo "==== DKSH cache (generic variants) ===="
# fable2-nx (2026-11-17): with the console's pipeline list (run-switch-build.bat copies it to out/console), every listed
# pipeline's variants too, so the console does not compile them while playing (it drew with generic pipelines until
# then); the console's own cache (out/console/deko3d_shaders.bin) merged in, its entries kept.
extra=()
[ -f out/console/pipeline_list.bin ] && extra+=(--list out/console/pipeline_list.bin)
[ -f out/console/deko3d_shaders.bin ] && extra+=(--merge out/console/deko3d_shaders.bin)
echo "variant cache: ${extra[*]:-generic variants only}"
python3 tools/deko3d_variant_cache.py --generic --glsl "$deko/glsl_opt" --out "$switch_out/deko3d_shaders.bin" \
    --jobs "$JOBS" "${extra[@]}"
echo "shaders: done ($switch_out)"
