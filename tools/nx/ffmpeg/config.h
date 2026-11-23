/*
 * Fable 2 NX: FFmpeg's platform dispatcher. katamari-nx's sdk/thirdparty/FFmpeg (which the build fetches) does not
 * carry config.h, libavutil/avconfig.h and libavutil/ffversion.h (generated files, left out of its repository), so
 * tools/nx/build-switch.sh copies these in. Upstream xenia-project/FFmpeg 0604b464's dispatcher, plus:
 *   - the Switch first (devkitA64 defines __SWITCH__ and none of the others): config_switch_aarch64.h;
 *   - the fork's WMV3 decoder after each platform's header (config_masseffect_wmv3.h says so; the SDK's CMake
 *     compiles the WMV3 sources on every platform).
 */
#if defined(__SWITCH__)
#include "config_switch_aarch64.h"
#elif defined(WIN32) || defined(_WIN32)
#if defined(__aarch64__) || defined(_M_ARM64)
#include "config_windows_aarch64.h"
#else
#include "config_windows_x86_64.h"
#endif
#elif defined(__APPLE__) && defined(__MACH__)
#if defined(__aarch64__) || defined(__arm64__)
#include "config_macos_aarch64.h"
#else
#include "config_macos_x86_64.h"
#endif
#elif defined(__ANDROID__)
#if defined(__aarch64__)
#include "config_android_aarch64.h"
#elif defined(__amd64__) || defined(__x86_64__)
#include "config_android_x86_64.h"
#else
#error "android arch unsupported"
#endif
#elif defined(__gnu_linux__) || defined(__linux__)
#if defined(__aarch64__)
#include "config_linux_aarch64.h"
#else
#include "config_linux_x86_64.h"
#endif
#else
#error "no config"
#endif
#include "config_masseffect_wmv3.h"
