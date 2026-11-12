# fable2-switch-dev: Lost Odyssey's toolchain image (lo-switch-dev: devkitA64, libnx, mesa-switch NVK, CMake, Ninja)
# plus a host Clang new enough for ReXGlue's code generator (Clang 18+; the base image has Debian's Clang 14).
# The SDK's Linux UI library (built with the host generator) also needs the X11-XCB and Wayland client headers.
# Built by run-switch-build.bat when its tag is missing (fable2-ultimate: the same image as fable2-nx).
#   docker build -t fable2-switch-dev:3 -f tools/nx/Dockerfile tools/nx
ARG BASE=lo-switch-dev
FROM ${BASE}
ARG LLVM=20
RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates wget gnupg; \
    . /etc/os-release; \
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/apt.llvm.org.gpg; \
    echo "deb [signed-by=/usr/share/keyrings/apt.llvm.org.gpg] http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-${LLVM} main" \
        > /etc/apt/sources.list.d/llvm.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends clang-${LLVM} lld-${LLVM} libx11-xcb-dev libwayland-dev zstd; \
    rm -rf /var/lib/apt/lists/*; \
    clang++-${LLVM} --version
ENV HOST_CC=/usr/bin/clang-20 HOST_CXX=/usr/bin/clang++-20
