#ifndef KEYBOARD_INPUT_H
#define KEYBOARD_INPUT_H

#include <SDL2/SDL.h>

namespace GlobalKeyboard {

// Starts the native, system-wide keyboard monitor for the current platform.
// On Linux this reads evdev keyboard devices; on Windows it uses a low-level
// keyboard hook; on macOS it uses a listen-only CGEventTap.
//
// Failure is non-fatal: the application can still run with controller input.
bool initialize();
void shutdown();

// Thread-safe snapshot of whether a physical key represented by SDL_Scancode
// is currently held down globally, even when another application has focus.
bool isPressed(SDL_Scancode scancode);

// Returns a short explanation of the current backend/status for diagnostics.
const char *backendName();

} // namespace GlobalKeyboard

#endif // KEYBOARD_INPUT_H
