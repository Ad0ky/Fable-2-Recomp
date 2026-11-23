#!/usr/bin/env bash
# fable2-nx on renut_nx_ultimate_base: the host tools of the deko3d shader chain, for Linux (the Docker image
# fable2-switch-dev:3, Clang 20). The base's tools/deko3d_build_tools.sh does the same on Windows with MSYS2.
#   - DXC (HLSL -> SPIR-V): Microsoft's Linux release, pinned, into the layout Renut's XenosRecomp expects
#     (app/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/{bin/x64/dxc-linux, lib/x64/*.so}; not in Git);
#   - SPIRV-Cross (SPIR-V -> GLSL): KhronosGroup, pinned, into out/deko3d_tools/spirv-cross-build;
#   - uam (GLSL -> DKSH, with SwitchWakerHD's patches): app/thirdparty/uam, into out/deko3d_tools/uam-build;
#   - glslangValidator (the PM4 renderer's EDRAM shaders, app/CMakeLists.txt): sdk/thirdparty/glslang, into
#     out/glslang-linux.
# Once per tree: out/deko3d_tools/.stamp records the versions.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
DXC_TAG=v1.8.2407
DXC_ASSET=linux_dxc_2024_07_31.x86_64.tar.gz
SPIRV_CROSS_TAG=vulkan-sdk-1.4.357.0
JOBS="${JOBS:-$(nproc)}"
CC="${HOST_CC:-$(command -v clang-20 || command -v clang)}"
CXX="${HOST_CXX:-$(command -v clang++-20 || command -v clang++)}"
out="$root/out/deko3d_tools"
stamp="$out/.stamp"
want="dxc=$DXC_TAG spirv-cross=$SPIRV_CROSS_TAG uam=$(sha256sum app/thirdparty/uam/CMakeLists.txt | cut -c1-16) glslang=1"
dxc_dir="$root/app/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin"
if [ "$(cat "$stamp" 2>/dev/null || true)" = "$want" ] && [ -x "$dxc_dir/bin/x64/dxc-linux" ] \
    && [ -x "$root/out/glslang-linux/StandAlone/glslangValidator" ]; then
    echo "shader tools: up to date ($want)"
    exit 0
fi
mkdir -p "$out"

echo "==== DXC $DXC_TAG ===="
tmp="$out/tmp-dxc"; rm -rf "$tmp"; mkdir -p "$tmp"
wget -q -O "$tmp/dxc.tar.gz" "https://github.com/microsoft/DirectXShaderCompiler/releases/download/$DXC_TAG/$DXC_ASSET"
tar -xzf "$tmp/dxc.tar.gz" -C "$tmp"
dxc_bin="$(find "$tmp" -type f -name dxc -path '*bin*' | head -n1)"
[ -n "$dxc_bin" ] || { echo "error: no bin/dxc in $DXC_ASSET" >&2; exit 1; }
mkdir -p "$dxc_dir/bin/x64" "$dxc_dir/lib/x64"
cp "$dxc_bin" "$dxc_dir/bin/x64/dxc-linux"
find "$tmp" -name 'lib*.so*' -exec cp -P {} "$dxc_dir/lib/x64/" \;
chmod +x "$dxc_dir/bin/x64/dxc-linux"
rm -rf "$tmp"
LD_LIBRARY_PATH="$dxc_dir/lib/x64" "$dxc_dir/bin/x64/dxc-linux" --version | head -n1

echo "==== SPIRV-Cross $SPIRV_CROSS_TAG ===="
src="$out/SPIRV-Cross"
if [ ! -d "$src/.git" ]; then
    rm -rf "$src"
    git clone -q --depth 1 --branch "$SPIRV_CROSS_TAG" https://github.com/KhronosGroup/SPIRV-Cross.git "$src"
fi
cmake -S "$src" -B "$out/spirv-cross-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_SHARED=OFF \
    -DSPIRV_CROSS_ENABLE_C_API=OFF > /dev/null
cmake --build "$out/spirv-cross-build" --target spirv-cross -j "$JOBS"

echo "==== uam (app/thirdparty/uam) ===="
cmake -S app/thirdparty/uam -B "$out/uam-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" > /dev/null
cmake --build "$out/uam-build" --target uam_tool -j "$JOBS"
"$out/uam-build/uam" --version | head -n1

echo "==== glslangValidator (sdk/thirdparty/glslang) ===="
cmake -S sdk/thirdparty/glslang -B "$root/out/glslang-linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF \
    -DENABLE_GLSLANG_BINARIES=ON -DENABLE_HLSL=OFF -DGLSLANG_ENABLE_INSTALL=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    > /dev/null
cmake --build "$root/out/glslang-linux" --target glslangValidator -j "$JOBS"
ls -l "$root/out/glslang-linux/StandAlone/glslangValidator"

echo "$want" > "$stamp"
echo "shader tools: built ($want)"
