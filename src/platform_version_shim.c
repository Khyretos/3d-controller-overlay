/*
 * platform_version_shim.c
 *
 * osxcross (unlike real Xcode) does not ship Apple's libclang_rt.osx.a,
 * which normally provides the runtime helper Clang inserts for every
 * `@available(...)` / `__builtin_available(...)` check in Objective-C
 * code. SDL2's MFI/GameController joystick backend (SDL_mfijoystick.m)
 * uses such a check, which causes a link-time failure under osxcross:
 *
 *   Undefined symbols for architecture x86_64:
 *     "___isPlatformVersionAtLeast", referenced from:
 *         _IOS_AddJoystickDevice in libSDL2.a(SDL_mfijoystick.m.o)
 *
 * This file supplies a weak fallback definition so the link succeeds.
 * It's declared `weak` so that if a real implementation is ever linked
 * in (e.g. if you later switch to a native macOS toolchain or a
 * toolchain that does provide compiler-rt), that real one wins instead.
 *
 * Caveat: this stub always reports "yes, available" for every version
 * check. That's safe for feature checks against the app's own
 * deployment target (CMAKE_OSX_DEPLOYMENT_TARGET / -mmacosx-version-min,
 * currently 11.0) and above, which covers the vast majority of what
 * SDL2's GameController-based joystick backend checks for. It would
 * only be a problem if some code path checked for an OS version *newer*
 * than what's actually running and then called an API that genuinely
 * doesn't exist yet on that older system. Given this only affects the
 * optional MFI/GameController joystick backend (not core rendering or
 * app logic), that risk is low, but worth knowing about.
 */

#if defined(__APPLE__)

#include <stdbool.h>

__attribute__((weak))
bool __isPlatformVersionAtLeast(unsigned Platform, unsigned Major, unsigned Minor, unsigned Subminor)
{
    (void)Platform;
    (void)Major;
    (void)Minor;
    (void)Subminor;
    return true;
}

#endif /* __APPLE__ */