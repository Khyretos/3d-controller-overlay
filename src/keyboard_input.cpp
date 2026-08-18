#include "keyboard_input.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#elif defined(__linux__)
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <unordered_set>
#endif

namespace GlobalKeyboard {
namespace {

std::array<std::atomic_bool, SDL_NUM_SCANCODES> g_keys{};
std::atomic_bool g_running{false};
std::mutex g_lifecycleMutex;
std::string g_status = "not initialized";

void clearKeys() {
  for (auto &key : g_keys)
    key.store(false, std::memory_order_relaxed);
}

void setKey(SDL_Scancode scancode, bool pressed) {
  if (scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES)
    g_keys[scancode].store(pressed, std::memory_order_relaxed);
}

#ifdef _WIN32

SDL_Scancode vkToScancode(DWORD vk, DWORD scanCode, DWORD flags) {
  switch (vk) {
  case 'A':
    return SDL_SCANCODE_A;
  case 'B':
    return SDL_SCANCODE_B;
  case 'C':
    return SDL_SCANCODE_C;
  case 'D':
    return SDL_SCANCODE_D;
  case 'E':
    return SDL_SCANCODE_E;
  case 'F':
    return SDL_SCANCODE_F;
  case 'G':
    return SDL_SCANCODE_G;
  case 'H':
    return SDL_SCANCODE_H;
  case 'I':
    return SDL_SCANCODE_I;
  case 'J':
    return SDL_SCANCODE_J;
  case 'K':
    return SDL_SCANCODE_K;
  case 'L':
    return SDL_SCANCODE_L;
  case 'M':
    return SDL_SCANCODE_M;
  case 'N':
    return SDL_SCANCODE_N;
  case 'O':
    return SDL_SCANCODE_O;
  case 'P':
    return SDL_SCANCODE_P;
  case 'Q':
    return SDL_SCANCODE_Q;
  case 'R':
    return SDL_SCANCODE_R;
  case 'S':
    return SDL_SCANCODE_S;
  case 'T':
    return SDL_SCANCODE_T;
  case 'U':
    return SDL_SCANCODE_U;
  case 'V':
    return SDL_SCANCODE_V;
  case 'W':
    return SDL_SCANCODE_W;
  case 'X':
    return SDL_SCANCODE_X;
  case 'Y':
    return SDL_SCANCODE_Y;
  case 'Z':
    return SDL_SCANCODE_Z;
  case '0':
    return SDL_SCANCODE_0;
  case '1':
    return SDL_SCANCODE_1;
  case '2':
    return SDL_SCANCODE_2;
  case '3':
    return SDL_SCANCODE_3;
  case '4':
    return SDL_SCANCODE_4;
  case '5':
    return SDL_SCANCODE_5;
  case '6':
    return SDL_SCANCODE_6;
  case '7':
    return SDL_SCANCODE_7;
  case '8':
    return SDL_SCANCODE_8;
  case '9':
    return SDL_SCANCODE_9;
  case VK_F1:
    return SDL_SCANCODE_F1;
  case VK_F2:
    return SDL_SCANCODE_F2;
  case VK_F3:
    return SDL_SCANCODE_F3;
  case VK_F4:
    return SDL_SCANCODE_F4;
  case VK_F5:
    return SDL_SCANCODE_F5;
  case VK_F6:
    return SDL_SCANCODE_F6;
  case VK_F7:
    return SDL_SCANCODE_F7;
  case VK_F8:
    return SDL_SCANCODE_F8;
  case VK_F9:
    return SDL_SCANCODE_F9;
  case VK_F10:
    return SDL_SCANCODE_F10;
  case VK_F11:
    return SDL_SCANCODE_F11;
  case VK_F12:
    return SDL_SCANCODE_F12;
  case VK_SPACE:
    return SDL_SCANCODE_SPACE;
  case VK_RETURN:
    return SDL_SCANCODE_RETURN;
  case VK_TAB:
    return SDL_SCANCODE_TAB;
  case VK_ESCAPE:
    return SDL_SCANCODE_ESCAPE;
  case VK_UP:
    return SDL_SCANCODE_UP;
  case VK_DOWN:
    return SDL_SCANCODE_DOWN;
  case VK_LEFT:
    return SDL_SCANCODE_LEFT;
  case VK_RIGHT:
    return SDL_SCANCODE_RIGHT;
  case VK_SHIFT:
    return (scanCode == 0x36) ? SDL_SCANCODE_RSHIFT : SDL_SCANCODE_LSHIFT;
  case VK_LSHIFT:
    return SDL_SCANCODE_LSHIFT;
  case VK_RSHIFT:
    return SDL_SCANCODE_RSHIFT;
  case VK_CONTROL:
    return (flags & LLKHF_EXTENDED) ? SDL_SCANCODE_RCTRL : SDL_SCANCODE_LCTRL;
  case VK_LCONTROL:
    return SDL_SCANCODE_LCTRL;
  case VK_RCONTROL:
    return SDL_SCANCODE_RCTRL;
  case VK_MENU:
    return (flags & LLKHF_EXTENDED) ? SDL_SCANCODE_RALT : SDL_SCANCODE_LALT;
  case VK_LMENU:
    return SDL_SCANCODE_LALT;
  case VK_RMENU:
    return SDL_SCANCODE_RALT;
  default:
    return SDL_SCANCODE_UNKNOWN;
  }
}

HHOOK g_hook = nullptr;
std::thread g_thread;

LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && lParam) {
    const auto *event = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    if (down || up) {
      SDL_Scancode sc =
          vkToScancode(event->vkCode, event->scanCode, event->flags);
      setKey(sc, down);
    }
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void windowsThread() {
  g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboardProc,
                             GetModuleHandleW(nullptr), 0);
  if (!g_hook) {
    g_status = "Windows low-level keyboard hook failed (" +
               std::to_string(GetLastError()) + ")";
    spdlog::error("Global keyboard: {}", g_status);
    g_running.store(false);
    return;
  }
  g_status = "Windows low-level keyboard hook";
  spdlog::info("Global keyboard backend: {}", g_status);

  MSG msg{};
  while (g_running.load()) {
    DWORD result =
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
    (void)result;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT)
        break;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
  }
}

#elif defined(__APPLE__)

CFMachPortRef g_eventTap = nullptr;
CFRunLoopRef g_runLoop = nullptr;
std::thread g_thread;

SDL_Scancode macKeycodeToScancode(CGKeyCode key) {
  switch (key) {
  case 0:
    return SDL_SCANCODE_A;
  case 1:
    return SDL_SCANCODE_S;
  case 2:
    return SDL_SCANCODE_D;
  case 3:
    return SDL_SCANCODE_F;
  case 4:
    return SDL_SCANCODE_H;
  case 5:
    return SDL_SCANCODE_G;
  case 6:
    return SDL_SCANCODE_Z;
  case 7:
    return SDL_SCANCODE_X;
  case 8:
    return SDL_SCANCODE_C;
  case 9:
    return SDL_SCANCODE_V;
  case 11:
    return SDL_SCANCODE_B;
  case 12:
    return SDL_SCANCODE_Q;
  case 13:
    return SDL_SCANCODE_W;
  case 14:
    return SDL_SCANCODE_E;
  case 15:
    return SDL_SCANCODE_R;
  case 16:
    return SDL_SCANCODE_Y;
  case 17:
    return SDL_SCANCODE_T;
  case 18:
    return SDL_SCANCODE_1;
  case 19:
    return SDL_SCANCODE_2;
  case 20:
    return SDL_SCANCODE_3;
  case 21:
    return SDL_SCANCODE_4;
  case 22:
    return SDL_SCANCODE_6;
  case 23:
    return SDL_SCANCODE_5;
  case 24:
    return SDL_SCANCODE_EQUALS;
  case 25:
    return SDL_SCANCODE_9;
  case 26:
    return SDL_SCANCODE_7;
  case 27:
    return SDL_SCANCODE_MINUS;
  case 28:
    return SDL_SCANCODE_8;
  case 29:
    return SDL_SCANCODE_0;
  case 30:
    return SDL_SCANCODE_RIGHTBRACKET;
  case 31:
    return SDL_SCANCODE_O;
  case 32:
    return SDL_SCANCODE_U;
  case 33:
    return SDL_SCANCODE_LEFTBRACKET;
  case 34:
    return SDL_SCANCODE_I;
  case 35:
    return SDL_SCANCODE_P;
  case 36:
    return SDL_SCANCODE_RETURN;
  case 37:
    return SDL_SCANCODE_L;
  case 38:
    return SDL_SCANCODE_J;
  case 39:
    return SDL_SCANCODE_APOSTROPHE;
  case 40:
    return SDL_SCANCODE_K;
  case 41:
    return SDL_SCANCODE_SEMICOLON;
  case 42:
    return SDL_SCANCODE_BACKSLASH;
  case 43:
    return SDL_SCANCODE_COMMA;
  case 44:
    return SDL_SCANCODE_SLASH;
  case 45:
    return SDL_SCANCODE_N;
  case 46:
    return SDL_SCANCODE_M;
  case 47:
    return SDL_SCANCODE_PERIOD;
  case 48:
    return SDL_SCANCODE_TAB;
  case 49:
    return SDL_SCANCODE_SPACE;
  case 50:
    return SDL_SCANCODE_GRAVE;
  case 51:
    return SDL_SCANCODE_BACKSPACE;
  case 53:
    return SDL_SCANCODE_ESCAPE;
  case 54:
    return SDL_SCANCODE_RGUI;
  case 55:
    return SDL_SCANCODE_LGUI;
  case 56:
    return SDL_SCANCODE_LSHIFT;
  case 57:
    return SDL_SCANCODE_CAPSLOCK;
  case 58:
    return SDL_SCANCODE_LALT;
  case 59:
    return SDL_SCANCODE_LCTRL;
  case 60:
    return SDL_SCANCODE_RSHIFT;
  case 61:
    return SDL_SCANCODE_RALT;
  case 62:
    return SDL_SCANCODE_RCTRL;
  case 63:
    return SDL_SCANCODE_F5;
  case 64:
    return SDL_SCANCODE_F17;
  case 65:
    return SDL_SCANCODE_KP_PERIOD;
  case 67:
    return SDL_SCANCODE_KP_MULTIPLY;
  case 69:
    return SDL_SCANCODE_KP_PLUS;
  case 71:
    return SDL_SCANCODE_NUMLOCKCLEAR;
  case 75:
    return SDL_SCANCODE_KP_DIVIDE;
  case 76:
    return SDL_SCANCODE_KP_ENTER;
  case 78:
    return SDL_SCANCODE_KP_MINUS;
  case 81:
    return SDL_SCANCODE_KP_EQUALS;
  case 82:
    return SDL_SCANCODE_KP_0;
  case 83:
    return SDL_SCANCODE_KP_1;
  case 84:
    return SDL_SCANCODE_KP_2;
  case 85:
    return SDL_SCANCODE_KP_3;
  case 86:
    return SDL_SCANCODE_KP_4;
  case 87:
    return SDL_SCANCODE_KP_5;
  case 88:
    return SDL_SCANCODE_KP_6;
  case 89:
    return SDL_SCANCODE_KP_7;
  case 91:
    return SDL_SCANCODE_KP_8;
  case 92:
    return SDL_SCANCODE_KP_9;
  case 96:
    return SDL_SCANCODE_F5;
  case 97:
    return SDL_SCANCODE_F6;
  case 98:
    return SDL_SCANCODE_F7;
  case 99:
    return SDL_SCANCODE_F3;
  case 100:
    return SDL_SCANCODE_F8;
  case 101:
    return SDL_SCANCODE_F9;
  case 103:
    return SDL_SCANCODE_F11;
  case 105:
    return SDL_SCANCODE_F13;
  case 106:
    return SDL_SCANCODE_F16;
  case 107:
    return SDL_SCANCODE_F14;
  case 109:
    return SDL_SCANCODE_F10;
  case 111:
    return SDL_SCANCODE_F12;
  case 113:
    return SDL_SCANCODE_F15;
  case 114:
    return SDL_SCANCODE_INSERT;
  case 115:
    return SDL_SCANCODE_HOME;
  case 116:
    return SDL_SCANCODE_PAGEUP;
  case 117:
    return SDL_SCANCODE_DELETE;
  case 118:
    return SDL_SCANCODE_F4;
  case 119:
    return SDL_SCANCODE_END;
  case 120:
    return SDL_SCANCODE_F2;
  case 121:
    return SDL_SCANCODE_PAGEDOWN;
  case 122:
    return SDL_SCANCODE_F1;
  case 123:
    return SDL_SCANCODE_LEFT;
  case 124:
    return SDL_SCANCODE_RIGHT;
  case 125:
    return SDL_SCANCODE_DOWN;
  case 126:
    return SDL_SCANCODE_UP;
  default:
    return SDL_SCANCODE_UNKNOWN;
  }
}

CGEventRef macEventCallback(CGEventTapProxy, CGEventType type, CGEventRef event,
                            void *) {
  if (type == kCGEventTapDisabledByTimeout ||
      type == kCGEventTapDisabledByUserInput) {
    if (g_eventTap)
      CGEventTapEnable(g_eventTap, true);
    return event;
  }
  if (type != kCGEventKeyDown && type != kCGEventKeyUp)
    return event;
  CGKeyCode key = static_cast<CGKeyCode>(
      CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
  setKey(macKeycodeToScancode(key), type == kCGEventKeyDown);
  return event;
}

void macThread() {
  CGEventMask mask =
      CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
  g_eventTap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionListenOnly, mask,
                                macEventCallback, nullptr);
  if (!g_eventTap) {
    g_status = "macOS event tap unavailable; grant Accessibility/Input "
               "Monitoring permission";
    spdlog::error("Global keyboard: {}", g_status);
    g_running.store(false);
    return;
  }
  g_status = "macOS CGEventTap";
  spdlog::info("Global keyboard backend: {}", g_status);
  CFRunLoopSourceRef source =
      CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_eventTap, 0);
  g_runLoop = CFRunLoopGetCurrent();
  CFRunLoopAddSource(g_runLoop, source, kCFRunLoopCommonModes);
  CGEventTapEnable(g_eventTap, true);
  CFRunLoopRun();
  CFRunLoopRemoveSource(g_runLoop, source, kCFRunLoopCommonModes);
  CFRelease(source);
  if (g_eventTap) {
    CFRelease(g_eventTap);
    g_eventTap = nullptr;
  }
  g_runLoop = nullptr;
}

#elif defined(__linux__)

struct LinuxDevice {
  int fd = -1;
  std::string path;
  std::unordered_set<int> pressed;
};
std::thread g_thread;

bool testBit(const unsigned long *bits, int bit) {
  return (bits[bit / (8 * sizeof(unsigned long))] >>
          (bit % (8 * sizeof(unsigned long)))) &
         1UL;
}

SDL_Scancode linuxKeyToScancode(int key) {
  if (key >= KEY_1 && key <= KEY_9)
    return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (key - KEY_1));
  if (key == KEY_0)
    return SDL_SCANCODE_0;
  switch (key) {
  // NOTE: Linux evdev KEY_* codes follow the physical AT-scancode keyboard
  // layout, not alphabetical order (e.g. KEY_A=30, KEY_S=31, KEY_D=32...,
  // while KEY_Q=16..KEY_P=25 sit in a completely separate block). They are
  // NOT safe to map with arithmetic offsets - each letter must be listed
  // explicitly.
  case KEY_A:
    return SDL_SCANCODE_A;
  case KEY_B:
    return SDL_SCANCODE_B;
  case KEY_C:
    return SDL_SCANCODE_C;
  case KEY_D:
    return SDL_SCANCODE_D;
  case KEY_E:
    return SDL_SCANCODE_E;
  case KEY_F:
    return SDL_SCANCODE_F;
  case KEY_G:
    return SDL_SCANCODE_G;
  case KEY_H:
    return SDL_SCANCODE_H;
  case KEY_I:
    return SDL_SCANCODE_I;
  case KEY_J:
    return SDL_SCANCODE_J;
  case KEY_K:
    return SDL_SCANCODE_K;
  case KEY_L:
    return SDL_SCANCODE_L;
  case KEY_M:
    return SDL_SCANCODE_M;
  case KEY_N:
    return SDL_SCANCODE_N;
  case KEY_O:
    return SDL_SCANCODE_O;
  case KEY_P:
    return SDL_SCANCODE_P;
  case KEY_Q:
    return SDL_SCANCODE_Q;
  case KEY_R:
    return SDL_SCANCODE_R;
  case KEY_S:
    return SDL_SCANCODE_S;
  case KEY_T:
    return SDL_SCANCODE_T;
  case KEY_U:
    return SDL_SCANCODE_U;
  case KEY_V:
    return SDL_SCANCODE_V;
  case KEY_W:
    return SDL_SCANCODE_W;
  case KEY_X:
    return SDL_SCANCODE_X;
  case KEY_Y:
    return SDL_SCANCODE_Y;
  case KEY_Z:
    return SDL_SCANCODE_Z;
  case KEY_ESC:
    return SDL_SCANCODE_ESCAPE;
  case KEY_SPACE:
    return SDL_SCANCODE_SPACE;
  case KEY_ENTER:
    return SDL_SCANCODE_RETURN;
  case KEY_TAB:
    return SDL_SCANCODE_TAB;
  case KEY_UP:
    return SDL_SCANCODE_UP;
  case KEY_DOWN:
    return SDL_SCANCODE_DOWN;
  case KEY_LEFT:
    return SDL_SCANCODE_LEFT;
  case KEY_RIGHT:
    return SDL_SCANCODE_RIGHT;
  case KEY_LEFTSHIFT:
    return SDL_SCANCODE_LSHIFT;
  case KEY_RIGHTSHIFT:
    return SDL_SCANCODE_RSHIFT;
  case KEY_LEFTCTRL:
    return SDL_SCANCODE_LCTRL;
  case KEY_RIGHTCTRL:
    return SDL_SCANCODE_RCTRL;
  case KEY_LEFTALT:
    return SDL_SCANCODE_LALT;
  case KEY_RIGHTALT:
    return SDL_SCANCODE_RALT;
  case KEY_F1:
    return SDL_SCANCODE_F1;
  case KEY_F2:
    return SDL_SCANCODE_F2;
  case KEY_F3:
    return SDL_SCANCODE_F3;
  case KEY_F4:
    return SDL_SCANCODE_F4;
  case KEY_F5:
    return SDL_SCANCODE_F5;
  case KEY_F6:
    return SDL_SCANCODE_F6;
  case KEY_F7:
    return SDL_SCANCODE_F7;
  case KEY_F8:
    return SDL_SCANCODE_F8;
  case KEY_F9:
    return SDL_SCANCODE_F9;
  case KEY_F10:
    return SDL_SCANCODE_F10;
  case KEY_F11:
    return SDL_SCANCODE_F11;
  case KEY_F12:
    return SDL_SCANCODE_F12;
  case KEY_BACKSPACE:
    return SDL_SCANCODE_BACKSPACE;
  case KEY_DELETE:
    return SDL_SCANCODE_DELETE;
  case KEY_HOME:
    return SDL_SCANCODE_HOME;
  case KEY_END:
    return SDL_SCANCODE_END;
  case KEY_PAGEUP:
    return SDL_SCANCODE_PAGEUP;
  case KEY_PAGEDOWN:
    return SDL_SCANCODE_PAGEDOWN;
  case KEY_INSERT:
    return SDL_SCANCODE_INSERT;
  case KEY_CAPSLOCK:
    return SDL_SCANCODE_CAPSLOCK;
  case KEY_MINUS:
    return SDL_SCANCODE_MINUS;
  case KEY_EQUAL:
    return SDL_SCANCODE_EQUALS;
  case KEY_LEFTBRACE:
    return SDL_SCANCODE_LEFTBRACKET;
  case KEY_RIGHTBRACE:
    return SDL_SCANCODE_RIGHTBRACKET;
  case KEY_SEMICOLON:
    return SDL_SCANCODE_SEMICOLON;
  case KEY_APOSTROPHE:
    return SDL_SCANCODE_APOSTROPHE;
  case KEY_GRAVE:
    return SDL_SCANCODE_GRAVE;
  case KEY_BACKSLASH:
    return SDL_SCANCODE_BACKSLASH;
  case KEY_COMMA:
    return SDL_SCANCODE_COMMA;
  case KEY_DOT:
    return SDL_SCANCODE_PERIOD;
  case KEY_SLASH:
    return SDL_SCANCODE_SLASH;
  case KEY_KPENTER:
    return SDL_SCANCODE_KP_ENTER;
  default:
    return SDL_SCANCODE_UNKNOWN;
  }
}

bool looksLikeKeyboard(int fd) {
  unsigned long bits[(KEY_MAX / (8 * sizeof(unsigned long))) + 1]{};
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
    return false;
  // Require ordinary keyboard keys. This rejects most mice/gamepads while
  // still accepting compact and laptop keyboards.
  return testBit(bits, KEY_A) && testBit(bits, KEY_Z) &&
         testBit(bits, KEY_SPACE) && testBit(bits, KEY_ENTER);
}

void removeDevice(std::vector<LinuxDevice> &devices, size_t index) {
  for (int key : devices[index].pressed) {
    setKey(linuxKeyToScancode(key), false);
  }
  close(devices[index].fd);
  devices.erase(devices.begin() + static_cast<std::ptrdiff_t>(index));
}

void scanDevices(std::vector<LinuxDevice> &devices) {
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator("/dev/input", ec)) {
    if (ec || !entry.is_character_file(ec))
      continue;
    const std::string path = entry.path().string();
    if (path.find("/event") == std::string::npos)
      continue;
    bool already = false;
    for (const auto &d : devices)
      if (d.path == path)
        already = true;
    if (already)
      continue;
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 || !looksLikeKeyboard(fd)) {
      if (fd >= 0)
        close(fd);
      continue;
    }
    devices.push_back({fd, path, {}});
    spdlog::info("Global keyboard: monitoring {}", path);
  }
}

void linuxThread() {
  std::vector<LinuxDevice> devices;
  g_status = "Linux evdev (/dev/input/event*)";
  scanDevices(devices);
  if (devices.empty()) {
    spdlog::warn("Global keyboard: no readable keyboard event devices found. "
                 "On Linux this usually means the user lacks permission to "
                 "read /dev/input/event*.");
  } else {
    spdlog::info("Global keyboard backend: {}", g_status);
  }

  auto lastScan = std::chrono::steady_clock::now();
  while (g_running.load()) {
    if (std::chrono::steady_clock::now() - lastScan > std::chrono::seconds(2)) {
      scanDevices(devices);
      lastScan = std::chrono::steady_clock::now();
    }
    std::vector<pollfd> pfds;
    pfds.reserve(devices.size());
    for (const auto &d : devices)
      pfds.push_back({d.fd, POLLIN | POLLERR | POLLHUP, 0});
    if (pfds.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      continue;
    }
    int result = poll(pfds.data(), pfds.size(), 250);
    if (result <= 0)
      continue;
    for (size_t i = devices.size(); i-- > 0;) {
      if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        removeDevice(devices, i);
        continue;
      }
      if (!(pfds[i].revents & POLLIN))
        continue;
      input_event event{};
      while (read(devices[i].fd, &event, sizeof(event)) == sizeof(event)) {
        if (event.type != EV_KEY)
          continue;
        SDL_Scancode sc = linuxKeyToScancode(event.code);
        if (sc == SDL_SCANCODE_UNKNOWN)
          continue;
        if (event.value == 1) {
          if (devices[i].pressed.insert(event.code).second)
            setKey(sc, true);
        } else if (event.value == 0) {
          devices[i].pressed.erase(event.code);
          setKey(sc, false);
        }
      }
    }
  }
  for (auto &d : devices) {
    for (int key : d.pressed)
      setKey(linuxKeyToScancode(key), false);
    close(d.fd);
  }
}

#else

std::thread g_thread;
void unsupportedThread() {
  g_status = "unsupported platform";
  spdlog::warn("Global keyboard backend: unsupported platform");
}

#endif

} // namespace

bool initialize() {
  std::lock_guard<std::mutex> lock(g_lifecycleMutex);
  if (g_running.load())
    return true;
  clearKeys();
  g_running.store(true);

#ifdef _WIN32
  g_thread = std::thread(windowsThread);
#elif defined(__APPLE__)
  g_thread = std::thread(macThread);
#elif defined(__linux__)
  g_thread = std::thread(linuxThread);
#else
  g_thread = std::thread(unsupportedThread);
#endif

  // Give the backend a moment to install its hook/tap or scan devices. The
  // worker remains asynchronous; failure is reported through the logger.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  return true;
}

void shutdown() {
  std::lock_guard<std::mutex> lock(g_lifecycleMutex);
  if (!g_running.load()) {
    if (g_thread.joinable())
      g_thread.join();
    clearKeys();
    return;
  }
  g_running.store(false);

#ifdef _WIN32
  // The hook thread polls its running flag and exits within 100 ms.
#elif defined(__APPLE__)
  if (g_runLoop)
    CFRunLoopStop(g_runLoop);
#endif

  if (g_thread.joinable())
    g_thread.join();
  clearKeys();
}

bool isPressed(SDL_Scancode scancode) {
  if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES)
    return false;
  return g_keys[scancode].load(std::memory_order_relaxed);
}

const char *backendName() {
#ifdef _WIN32
  return "Windows low-level keyboard hook";
#elif defined(__APPLE__)
  return "macOS CGEventTap";
#elif defined(__linux__)
  return "Linux evdev";
#else
  return "unsupported platform";
#endif
}

} // namespace GlobalKeyboard