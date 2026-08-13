#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

// --------------------------------------------------------------------------
// IMPORTANT: The following includes MUST be in this exact order.
// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <SDL2/SDL.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"
// clang-format on
// --------------------------------------------------------------------------

#include <filesystem>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <vector>

#include "controller_window.h"
#include "settings.h"

// ----------------------------------------------------------------------------
// Forward declarations & typedefs
// ----------------------------------------------------------------------------
typedef struct controller_window_struct controller_window;

typedef struct my_tab {
  unsigned ID;
  std::string title;
} window_tab;

// ----------------------------------------------------------------------------
// Function declarations
// ----------------------------------------------------------------------------
void createSettingsWindow();
GLFWwindow *getSettingsWindow();
void close_window(unsigned ID);
void removeTab(unsigned tab);
void saveTabs();
void loadTabs();
void removeSettingsWindow();
void drawSettingsWindow();
void settings_window_input(bool &quit);
void settings_sdl_events(SDL_Event *event);
void settings_framebuffer_size_callback(GLFWwindow *window, int width,
                                        int height);
void glfw_error_callback(int error, const char *description);
void GetOpenGLVersionInfo();
void OsOpenInShell(const char *path);
const GLFWvidmode *get_vid_mode();
bool check_filename_valid(const char *name);
std::string get_top_folder(std::string path);
std::string get_first_model();
std::vector<std::string>
get_current_mapping(SDL_GameController *sdl_controller);
std::vector<std::string> get_binding(std::string b);

extern bool g_log_buttons;
extern std::string g_loaded_mapping_name;

#endif