#if defined(__linux__)
#elif __FreeBSD__
#elif __ANDROID__
#elif __APPLE__
#elif _WIN32
#define SDL_MAIN_HANDLED
#else
#endif

#include <iostream>
#include "settings_window.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

bool gQuit = false;

void InitializeProgram() {
  // Set up logging: rotate at 5 MB, keep 3 files
  try {
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/3dco.log", 5 * 1024 * 1024, 3);
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(
        "3dco", spdlog::sinks_init_list{file_sink, console_sink});
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug); // Change to 'info' for release
    spdlog::info("3D Controller Overlay starting...");
  } catch (const spdlog::spdlog_ex &ex) {
    std::cerr << "Log initialization failed: " << ex.what() << std::endl;
  }

  // Init SDL with joystick, gamecontroller, and sensor subsystems
  if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR) <
      0) {
    spdlog::critical("SDL_Init failed: {}", SDL_GetError());
    exit(1);
  }
  spdlog::info("SDL initialized");

  createSettingsWindow();
  loadTabs();
}

void Input() {
  glfwPollEvents();

  settings_window_input(gQuit);
  controller_window_input();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    settings_sdl_events(&event);
    controller_sdl_events(&event);
  }
}

void Draw() {
  drawSettingsWindow();
  drawControllerWindows();
}

void MainLoop() {
  while (!gQuit) {
    Input();
    Draw();
  }
}

void Cleanup() {
  saveTabs();
  removeSettingsWindow();
  destroyWindows();
  SDL_Quit();
  glfwTerminate();
  spdlog::info("Shutdown complete.");
}

int main() {
  InitializeProgram();
  MainLoop();
  Cleanup();
  return 0;
}