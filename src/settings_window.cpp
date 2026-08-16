#if defined(__linux__)
#elif __FreeBSD__
#elif __ANDROID__
#elif __APPLE__
#elif _WIN32
#include <windows.h>
#define SDL_MAIN_HANDLED
#else // some other operating system
#endif

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "model.h"
#include "settings.h"
#include "settings_window.h"
#include "strings.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <spdlog/spdlog.h>
#include <stdio.h>

extern std::vector<controller_window> windows;
extern std::string button_names[21];

static bool HasTouchpadFinger(controller_window *w, int touchpadIdx,
                              int fingerIdx) {
  if (!w || !w->is_gamecontroller || !w->sdl_controller)
    return false;
  int numTouchpads = SDL_GameControllerGetNumTouchpads(w->sdl_controller);
  if (touchpadIdx >= numTouchpads)
    return false;
  int numFingers =
      SDL_GameControllerGetNumTouchpadFingers(w->sdl_controller, touchpadIdx);
  return fingerIdx < numFingers;
}

bool g_log_buttons = false;
std::string g_loaded_mapping_name = "";
static int last_logged_device_index = -1;

std::string getBindingDescription(const std::string &binding) {
  if (binding.empty())
    return "unbound";
  if (binding[0] == 'b') {
    int num = std::stoi(binding.substr(1));
    if (num >= 0 && num < 21) {
      return "Button " + std::to_string(num) + " (" + button_names[num] + ")";
    } else {
      return "Button " + std::to_string(num);
    }
  }
  if (binding[0] == 'h') {
    size_t dot = binding.find('.');
    if (dot != std::string::npos) {
      int hatIdx = std::stoi(binding.substr(1, dot - 1));
      int dir = std::stoi(binding.substr(dot + 1));
      const char *dirNames[8] = {"Up",   "Right-Up",  "Right", "Right-Down",
                                 "Down", "Left-Down", "Left",  "Left-Up"};
      return "Hat " + std::to_string(hatIdx) + " " +
             (dir >= 0 && dir < 8 ? dirNames[dir] : "?");
    }
    return binding;
  }
  if (binding[0] == 'a') {
    if (binding.back() == '+') {
      int num = std::stoi(binding.substr(1, binding.size() - 2));
      return "Axis " + std::to_string(num) + " Positive";
    } else if (binding.back() == '-') {
      int num = std::stoi(binding.substr(1, binding.size() - 2));
      return "Axis " + std::to_string(num) + " Negative";
    } else {
      int num = std::stoi(binding.substr(1));
      return "Axis " + std::to_string(num);
    }
  }
  if (binding[0] == 't') {
    // Parse tX_fY_z
    size_t pos1 = binding.find('_');
    if (pos1 != std::string::npos) {
      size_t pos2 = binding.find('_', pos1 + 1);
      if (pos2 != std::string::npos) {
        std::string touchStr = binding.substr(1, pos1 - 1);
        std::string fingerStr = binding.substr(pos1 + 1, pos2 - pos1 - 1);
        char axis = binding.back();
        return "Touchpad " + touchStr + " Finger " + fingerStr.substr(1) +
               " Axis " + (axis == 'x' ? "X" : "Y");
      }
    }
    return binding;
  }
  return binding;
}

// Actual filenames of OBJ meshes (used for file I/O)
std::string mesh_filenames[35] = {
    "top_shell.obj",    "bottom_shell.obj",  "extra.obj",
    "left_trigger.obj", "right_trigger.obj", "left_stick.obj",
    "right_stick.obj",  "left_ring.obj",     "right_ring.obj",
    "a_button.obj",     "b_button.obj",      "x_button.obj",
    "y_button.obj",     "back_button.obj",   "guide_button.obj",
    "start_button.obj", "left_cap.obj",      "right_cap.obj",
    "left_bumper.obj",  "right_bumper.obj",  "dpad_up.obj",
    "dpad_down.obj",    "dpad_left.obj",     "dpad_right.obj",
    "misc.obj",         "paddle1.obj",       "paddle2.obj",
    "paddle3.obj",      "paddle4.obj",       "touchpad.obj",
    "touch_point1.obj", "touch_point2.obj",  "touchpad2.obj",
    "touch_point3.obj", "touch_point4.obj"};

std::string invalid_characters = "\\/:*?\"<>|";

std::string mapping_names[35] = {"a",
                                 "b",
                                 "x",
                                 "y",
                                 "back",
                                 "guide",
                                 "start",
                                 "leftstick",
                                 "rightstick",
                                 "leftshoulder",
                                 "rightshoulder",
                                 "dpup",
                                 "dpdown",
                                 "dpleft",
                                 "dpright",
                                 "touchpad",
                                 "misc",
                                 "paddle1",
                                 "paddle2",
                                 "paddle3",
                                 "paddle4",
                                 "leftx",
                                 "lefty",
                                 "rightx",
                                 "righty",
                                 "lefttrigger",
                                 "righttrigger",
                                 "touch0_f0_x",
                                 "touch0_f0_y",
                                 "touch0_f1_x",
                                 "touch0_f1_y",
                                 "touch1_f0_x",
                                 "touch1_f0_y",
                                 "touch1_f1_x",
                                 "touch1_f1_y"};

std::string input_names[35] = {"a button",
                               "b button",
                               "x button",
                               "y button",
                               "back button",
                               "guide button",
                               "start button",
                               "left stick click",
                               "right stick click",
                               "left bumper",
                               "right bumper",
                               "dpad up",
                               "dpad down",
                               "dpad left",
                               "dpad right",
                               "touchpad click",
                               "misc button",
                               "paddle 1",
                               "paddle 2",
                               "paddle 3",
                               "paddle 4",
                               "left stick x-axis",
                               "left stick y-axis",
                               "right stick x-axis",
                               "right stick y-axis",
                               "left trigger",
                               "right trigger",
                               "Touchpad 0 Finger 0 X",
                               "Touchpad 0 Finger 0 Y",
                               "Touchpad 0 Finger 1 X",
                               "Touchpad 0 Finger 1 Y",
                               "Touchpad 1 Finger 0 X",
                               "Touchpad 1 Finger 0 Y",
                               "Touchpad 1 Finger 1 X",
                               "Touchpad 1 Finger 1 Y"};

std::string mesh_names[35] = {
    "top shell",     "bottom shell",  "extra",         "left trigger",
    "right trigger", "left stick",    "right stick",   "left ring",
    "right ring",    "a button",      "b button",      "x button",
    "y button",      "back button",   "guide button",  "start button",
    "left cap",      "right cap",     "left bumper",   "right bumper",
    "d-pad up",      "d-pad down",    "d-pad left",    "d-pad right",
    "misc",          "paddle 1",      "paddle 2",      "paddle 3",
    "paddle 4",      "touchpad",      "touch point 1", "touch point 2",
    "touchpad 2",    "touch point 3", "touch point 4"};

std::string current_mapping[27];

bool remap = false;
std::string rebind_string = "";

unsigned int tabs_made = 0;
unsigned selected_tab = 0;
unsigned selected_mesh = 0;
unsigned material_mesh = 0;
unsigned texture_mesh = 0;

GLFWwindow *glfw_settings_window;
GLFWmonitor *primary_monitor;
const GLFWvidmode *vid_mode;

ImGui::FileBrowser texture_dialog;
ImGui::FileBrowser model_dialog;
ImGui::FileBrowser import_model_dialog;

std::vector<window_tab> tabs;
std::vector<Texture> textures;

ImVec4 clear_color = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
ImGuiIO *io;

void createSettingsWindow() {
  glfwInit();

#if defined(IMGUI_IMPL_OPENGL_ES2)
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#elif defined(__APPLE__)
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, 1);

  glfw_settings_window =
      glfwCreateWindow(640, 480, "3D Controller Overlay", NULL, NULL);
  if (glfw_settings_window == NULL) {
    std::cout << "Failed to create settings window" << std::endl;
    glfwTerminate();
  }
  glfwMakeContextCurrent(glfw_settings_window);
  glfwSwapInterval(1);
  glfwSetFramebufferSizeCallback(glfw_settings_window,
                                 settings_framebuffer_size_callback);

  GLFWimage images[1];
  images[0].pixels =
      stbi_load("icon.png", &images[0].width, &images[0].height, 0, 4);
  if (images[0].pixels == NULL) {
    std::cout << "couldn't load settings window icon" << std::endl;
  } else {
    glfwSetWindowIcon(glfw_settings_window, 1, images);
  }
  stbi_image_free(images[0].pixels);

  primary_monitor = glfwGetPrimaryMonitor();
  vid_mode = glfwGetVideoMode(primary_monitor);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  io = &ImGui::GetIO();
  (void)io;

  ImGui::CreateContext();
  io = &ImGui::GetIO();

  // ---- Redirect ImGui INI file to config_base_path ----
  static std::string ini_path = config_base_path + "/imgui.ini";
  io->IniFilename = ini_path.c_str();

  ImGui::StyleColorsDark();
  ImGui::StyleColorsDark();
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 purple = ImVec4(0.45f, 0.18f, 0.59f, 1.0f); // royal purple
  ImVec4 purple_light = ImVec4(0.6f, 0.3f, 0.75f, 1.0f);
  ImVec4 purple_dark = ImVec4(0.3f, 0.1f, 0.4f, 1.0f);
  style.Colors[ImGuiCol_Button] = purple;
  style.Colors[ImGuiCol_ButtonHovered] = purple_light;
  style.Colors[ImGuiCol_ButtonActive] = purple_dark;
  style.Colors[ImGuiCol_Header] = purple;
  style.Colors[ImGuiCol_HeaderHovered] = purple_light;
  style.Colors[ImGuiCol_HeaderActive] = purple_dark;
  style.Colors[ImGuiCol_CheckMark] = purple_light;
  style.Colors[ImGuiCol_SliderGrab] = purple;
  style.Colors[ImGuiCol_SliderGrabActive] = purple_light;
  style.Colors[ImGuiCol_FrameBgHovered] = purple_dark;
  style.Colors[ImGuiCol_Tab] = purple_dark;
  style.Colors[ImGuiCol_TabHovered] = purple_light;
  style.Colors[ImGuiCol_TabActive] = purple;
  style.Colors[ImGuiCol_ResizeGrip] = purple;
  style.Colors[ImGuiCol_ResizeGripHovered] = purple_light;
  style.Colors[ImGuiCol_ResizeGripActive] = purple_dark;

  ImGui_ImplGlfw_InitForOpenGL(glfw_settings_window, true);
  if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
    std::cout << "failed to init imgui for opengl3." << std::endl;
  }

  texture_dialog.SetWindowSize(400, 300);
  texture_dialog.SetTitle("Select Texture File");
  texture_dialog.SetTypeFilters({".png", ".jpg"});

  model_dialog.SetWindowSize(400, 300);
  model_dialog.SetTitle("Select Model File");
  model_dialog.SetTypeFilters(
      {".obj", ".fbx", ".gltf", ".glb", ".blend", ".dae", ".stl"});

  import_model_dialog.SetWindowSize(400, 300);
  import_model_dialog.SetTitle("Import 3D Model");
  import_model_dialog.SetTypeFilters(
      {".obj", ".fbx", ".gltf", ".glb", ".blend", ".dae", ".stl"});
}

GLFWwindow *getSettingsWindow() { return glfw_settings_window; }

void settings_framebuffer_size_callback(GLFWwindow *window, int width,
                                        int height) {
  glViewport(0, 0, width, height);
}

void close_window(unsigned ID) {
  removeControllerWindow(ID);
  removeTab(ID);
}

const GLFWvidmode *get_vid_mode() { return vid_mode; }

void removeTab(unsigned ID) {
  for (unsigned i = 0; i < tabs.size(); ++i) {
    if (tabs[i].ID == ID) {
      tabs.erase(tabs.begin() + i);
      selected_tab = 0;
      break;
    }
  }
}

void removeSettingsWindow() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(glfw_settings_window);
}

void settings_window_input(bool &quit) {
  if (glfwGetKey(glfw_settings_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(glfw_settings_window, true);
  }
  if (glfwWindowShouldClose(glfw_settings_window)) {
    quit = true;
  }
}

void settings_sdl_events(SDL_Event *event) {
  // Maybe used later
}

bool check_tab_title_exists(std::string title) {
  bool exists = false;
  for (my_tab t : tabs) {
    if (title == t.title) {
      exists = true;
      break;
    }
  }
  return exists;
}

// Forward declarations of helper functions (defined later)
void DrawImportPreviewControls(controller_window &w);
void SaveImportedModel(controller_window &w);
void writeOBJ(const std::string &path, const ImportedMesh &mesh);

void drawSettingsWindow() {
  glfwMakeContextCurrent(glfw_settings_window);
  glfwSwapInterval(1);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove;

#ifdef IMGUI_HAS_VIEWPORT
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);
#else
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
#endif

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Settings Window", nullptr, window_flags);

  // ---- Draw Import Preview controls if any preview window exists ----
  for (auto &w : windows) {
    if (w.is_import_preview && w.import_preview.is_open) {
      if (ImGui::CollapsingHeader("Import Preview",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawImportPreviewControls(w);
      }
      break;
    }
  }

  bool new_controller_window = false;
  int new_tab_number = 1;
  std::string new_tab_title = "Controller ";
  while (check_tab_title_exists(
      std::string("Controller ").append(std::to_string(new_tab_number)))) {
    new_tab_number++;
  }
  new_tab_title.append(std::to_string(new_tab_number));

  static ImGuiTabBarFlags tab_bar_flags =
      ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable |
      ImGuiTabBarFlags_FittingPolicyResizeDown;

  if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags)) {
    if (ImGui::TabItemButton("New", ImGuiTabItemFlags_Trailing |
                                        ImGuiTabItemFlags_NoTooltip)) {
      window_tab new_tab;
      tabs_made++;
      new_tab.title = new_tab_title;
      tabs.push_back(new_tab);
      new_controller_window = true;
    }
    for (unsigned i = 0; i < tabs.size(); ++i) {
      bool open = true;
      if (ImGui::BeginTabItem(tabs[i].title.c_str(), &open,
                              ImGuiTabItemFlags_None)) {
        selected_tab = i;
        ImGui::EndTabItem();
      }
      if (!open) {
        glfwSetWindowShouldClose(getControllerWindow(tabs[i].ID)->glfw_window,
                                 true);
      }
    }
    ImGui::EndTabBar();
  }

  if (tabs.size() > 0 && new_controller_window == false) {
    controller_window *current_window =
        getControllerWindow(tabs[selected_tab].ID);

    if (current_window->is_import_preview) {
      // Preview windows only show import controls (drawn outside this block)
      // Skip all normal sections.
      ImGui::End();
      ImGui::PopStyleVar();
      return;
    }
    if (!current_window) {
      ImGui::End();
      ImGui::PopStyleVar();
      return;
    }

    // ============================================================
    // WINDOW
    // ============================================================
    if (ImGui::CollapsingHeader("Window")) {
      char title[20] = {};
      if (ImGui::InputTextWithHint("Title", tabs[selected_tab].title.c_str(),
                                   title, IM_ARRAYSIZE(title),
                                   ImGuiInputTextFlags_EnterReturnsTrue)) {
        glfwSetWindowTitle(current_window->glfw_window, title);
        tabs[selected_tab].title = std::string(title);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Set the window title.");
      ImGui::NewLine();

      if (ImGui::Checkbox("Always on Top", &current_window->always_on_top)) {
        glfwSetWindowAttrib(current_window->glfw_window, GLFW_FLOATING,
                            current_window->always_on_top);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keep window above all others.");

      if (ImGui::Checkbox("Borderless", &current_window->borderless)) {
        glfwSetWindowAttrib(current_window->glfw_window, GLFW_DECORATED,
                            !current_window->borderless);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide title bar and borders.");

      ImGui::Checkbox("Drag to Move", &current_window->drag_to_move);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Left‑click drag to move window.");

      ImGui::Checkbox("Scroll to Resize", &current_window->scroll_to_resize);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scroll mouse wheel to resize window.");

      ImGui::Checkbox("Show Grid", &current_window->grid);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show/hide reference grid.");

      ImGui::Checkbox("Wireframe Mode", &current_window->wireframe);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle wireframe rendering.");

      ImGui::NewLine();
      int w = 0, h = 0;
      glfwGetWindowSize(current_window->glfw_window, &w, &h);
      if (ImGui::InputInt("Width", &w, 10, 100,
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (w < 10)
          w = 10;
        if (w > vid_mode->width)
          w = vid_mode->width;
        glfwSetWindowSize(current_window->glfw_window, w, h);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Window width in pixels.");

      if (ImGui::InputInt("Height", &h, 10, 100,
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (h < 10)
          h = 10;
        if (h > vid_mode->height)
          h = vid_mode->height;
        glfwSetWindowSize(current_window->glfw_window, w, h);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Window height in pixels.");

      ImGui::NewLine();
      ImGui::SliderInt("Swap Interval", &current_window->swap_interval, 0, 2);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("V‑sync: 0 = off, 1 = on, 2 = adaptive.");

      ImGui::NewLine();
      if (ImGui::Button("Open Data Directory")) {
        OsOpenInShell(config_base_path.c_str());
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Open the folder where settings, models, and logs are stored.");

      ImGui::NewLine();
      ImGui::ColorEdit4("Background Color", current_window->bg_color);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Background colour and opacity.");
    }

    // ============================================================
    // CAMERA
    // ============================================================
    if (ImGui::CollapsingHeader("Camera")) {
      ImGui::SliderFloat("Distance", &current_window->camera_distance, 1, 10);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Camera distance from the model.");
      ImGui::SliderFloat("Yaw", &current_window->camera_yaw, -360, 360);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Horizontal camera orbit.");
      ImGui::SliderFloat("Pitch", &current_window->camera_pitch, -180, 180);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Vertical camera orbit.");
      ImGui::SliderFloat("Roll", &current_window->camera_roll, -180, 180);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Camera roll (tilt).");
      if (ImGui::Button("Reset")) {
        current_window->camera_distance = 3.3f;
        current_window->camera_yaw = 0.0f;
        current_window->camera_pitch = 89.999f;
        current_window->camera_roll = 0.0f;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset camera to default view.");
    }

    // ============================================================
    // CONTROLLER
    // ============================================================
    if (ImGui::CollapsingHeader("Controller")) {
      std::vector<int> all_devices;
      for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        all_devices.push_back(i);
      }

      std::string device_name = "None";
      if (current_window->is_gamecontroller && current_window->sdl_controller) {
        device_name = SDL_GameControllerName(current_window->sdl_controller);
      } else if (current_window->sdl_joystick) {
        device_name = SDL_JoystickName(current_window->sdl_joystick);
      }

      if (ImGui::BeginCombo("Controllers", device_name.c_str(), 0)) {
        for (int idx : all_devices) {
          const char *name = SDL_JoystickNameForIndex(idx);
          bool is_game = SDL_IsGameController(idx);
          std::string label = std::string(name ? name : "Unknown") +
                              (is_game ? " (gamepad)" : " (joystick)") + " [" +
                              std::to_string(idx) + "]";
          if (ImGui::Selectable(label.c_str())) {
            // ---- Close any currently open device ----
            if (current_window->sdl_controller) {
              SDL_GameControllerClose(current_window->sdl_controller);
              current_window->sdl_controller = nullptr;
            }
            if (current_window->sdl_joystick) {
              SDL_JoystickClose(current_window->sdl_joystick);
              current_window->sdl_joystick = nullptr;
            }
            current_window->is_gamecontroller = false;

            // ---- Open the new device ----
            if (is_game) {
              current_window->sdl_controller = SDL_GameControllerOpen(idx);
              if (current_window->sdl_controller) {
                current_window->is_gamecontroller = true;
                spdlog::info(
                    "Switched to gamecontroller: {}",
                    SDL_GameControllerName(current_window->sdl_controller));

                // ---- Re‑enable gyro if it was previously enabled ----
                if (current_window->gyro_enabled) {
                  if (SDL_GameControllerHasSensor(
                          current_window->sdl_controller, SDL_SENSOR_GYRO)) {
                    SDL_GameControllerSetSensorEnabled(
                        current_window->sdl_controller, SDL_SENSOR_GYRO,
                        SDL_TRUE);
                    spdlog::info("Gyro re‑enabled on new controller.");
                  } else {
                    spdlog::warn(
                        "New controller does not support gyro, disabling.");
                    current_window->gyro_enabled = false;
                  }
                }
              } else {
                spdlog::error("Failed to open gamecontroller {}: {}", idx,
                              SDL_GetError());
              }
            } else {
              int numJoy = SDL_NumJoysticks();
              if (idx < 0 || idx >= numJoy) {
                spdlog::error(
                    "Joystick index {} out of range ({} joysticks present)",
                    idx, numJoy);
              } else {
                current_window->sdl_joystick = SDL_JoystickOpen(idx);
                if (current_window->sdl_joystick) {
                  current_window->is_gamecontroller = false;
                  spdlog::info("Switched to generic joystick: {}",
                               SDL_JoystickName(current_window->sdl_joystick));
                } else {
                  spdlog::error("Failed to open generic joystick {}: {}", idx,
                                SDL_GetError() ? SDL_GetError()
                                               : "(no error details)");
                }
              }
            }
            current_window->joystick_index = idx;

            // ---- Reset all input state ----
            // Reset touchpad states
            for (int t = 0; t < 4; ++t) {
              for (int f = 0; f < 2; ++f) {
                current_window->touchpad_data[t][f].state = 0;
                current_window->touchpad_data[t][f].x = 0.0f;
                current_window->touchpad_data[t][f].y = 0.0f;
              }
            }
            // Reset axis and hat history
            for (int i = 0; i < 32; ++i)
              current_window->last_axis_values[i] = 0.0f;
            for (int i = 0; i < 16; ++i)
              current_window->last_hat_values[i] = SDL_HAT_CENTERED;
            // Reset button states
            for (int i = 0; i < 128; ++i)
              current_window->last_joy_button_values[i] = false;
            for (int i = 0; i < 64; ++i)
              current_window->last_button_values[i] = false;

            // ---- Reset gyro completely ----
            current_window->gyro_matrix = glm::mat4(1.0f);
            current_window->gyro_data[0] = 0.0f;
            current_window->gyro_data[1] = 0.0f;
            current_window->gyro_data[2] = 0.0f;
            current_window->gyro_time = 0;
            current_window->gyro_toggled =
                true; // force first‑frame timestamp read
            current_window->lastTime = glfwGetTime();
            // Force gyro to re‑initialise timestamp on next frame
          }
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select a gamepad or joystick.");

      if (ImGui::TreeNode("Settings")) {
        ImGui::Checkbox("Popup Bumpers", &current_window->model.popup_bumpers);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Animate bumpers when pressed.");
        ImGui::SameLine();
        ImGui::Checkbox("Popup Triggers",
                        &current_window->model.popup_triggers);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Animate triggers when pressed.");
        ImGui::SameLine();
        ImGui::Checkbox("Popup Paddles", &current_window->model.popup_paddles);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Animate paddles when pressed.");
        ImGui::NewLine();
        if (current_window->model.meshes.size() > 7) {
          ImGui::SliderInt(
              "L-Stick Highlight Deadzone",
              &current_window->model.meshes[7].ring_highlight_deadzone, 0, 100);
        }
        if (current_window->model.meshes.size() > 8) {
          ImGui::SliderInt(
              "R-Stick Highlight Deadzone",
              &current_window->model.meshes[8].ring_highlight_deadzone, 0, 100);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Deadzone for right stick highlight ring.");
        ImGui::ColorEdit3("Highlight Color", current_window->highlight_color);
        ImGui::ColorEdit3("Press Color", current_window->global_press_color);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Color used when a button is pressed (if no per‑mesh override).");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Colour used for highlights on buttons and sticks.");
        for (int i = 3; i < (int)current_window->model.meshes.size(); i++) {
          if (i != 5 && i != 6) {
            current_window->model.meshes[i].material.highlight[0] =
                current_window->highlight_color[0];
            current_window->model.meshes[i].material.highlight[1] =
                current_window->highlight_color[1];
            current_window->model.meshes[i].material.highlight[2] =
                current_window->highlight_color[2];
          }
        }
        ImGui::TreePop();
      }
    } // end Controller

    // ============================================================
    // MODEL
    // ============================================================

    static int mesh_to_delete = -1; // for per‑row delete confirmation

    if (ImGui::CollapsingHeader("Model")) {

      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Model Selection");
      ImGui::Separator();

      // ---- Clamp all mesh indices to valid range ----
      size_t meshCount = current_window->model.meshes.size();
      if (meshCount == 0) {
        selected_mesh = 0;
        material_mesh = 0;
        texture_mesh = 0;
      } else {
        if (selected_mesh >= meshCount)
          selected_mesh = meshCount - 1;
        if (material_mesh >= meshCount)
          material_mesh = meshCount - 1;
        if (texture_mesh >= meshCount)
          texture_mesh = meshCount - 1;
      }

      // ---- Model selection combo ----
      if (ImGui::BeginCombo("Models", current_window->model_name.c_str(), 0)) {
        std::string models_root = get_models_root();
        std::string dir_path = models_root;
        dir_path.append("/");

        if (std::filesystem::exists(dir_path) &&
            std::filesystem::is_directory(dir_path)) {
          struct stat sb;
          for (const auto &entry :
               std::filesystem::directory_iterator(dir_path)) {
            std::string model_dir = entry.path().string();
            std::string delimiter = "/";
            if (stat(model_dir.c_str(), &sb) == 0 && (sb.st_mode & S_IFDIR)) {
              size_t pos = 0;
              while ((pos = model_dir.find(delimiter)) != std::string::npos) {
                model_dir.erase(0, pos + delimiter.length());
              }
              if (ImGui::Selectable(model_dir.c_str())) {
                current_window->model_name = model_dir;
                std::string model_path = models_root;
                model_path.append("/");
                model_path.append(model_dir);
                glfwMakeContextCurrent(current_window->glfw_window);
                loadModel(current_window->model, model_path);
                // Update mesh_count
                glfwMakeContextCurrent(glfw_settings_window);
              }
            }
          }
        } else {
          ImGui::TextDisabled("No models directory found at:\n%s",
                              dir_path.c_str());
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select a controller model.");

      // ---- Source URL ----
      char source[256];
      strncpy(source, current_window->model.source.c_str(), 255);
      if (ImGui::InputText("Source URL", source, 256)) {
        current_window->model.source = source;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Optional URL where the model was obtained.");

      if (ImGui::Button("New Model")) {
        ImGui::OpenPopup("new");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create a new empty model folder.");

      if (ImGui::BeginPopup("new")) {
        char name[32] = {};
        static bool name_valid = true;
        if (ImGui::InputText("Model Name", name, IM_ARRAYSIZE(name),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
          bool valid = check_filename_valid(name);
          name_valid = valid;
          if (valid) {
            std::filesystem::path path(get_models_root());
            path /= name;
            std::filesystem::create_directory(path);
            std::string new_model_path = path.string();
            // Create a model with 35 default mesh slots (so the user can import
            // OBJs into each)
            current_window->model.meshes.resize(35);
            for (int i = 0; i < 35; ++i) {
              Mesh &m = current_window->model.meshes[i];
              m.name = mesh_names[i];
              m.filename = mesh_filenames[i];
              m.assignedPart = i; // each slot corresponds to a controller part
              m.parentIndex = -1;
              m.elements = 0; // empty geometry
              m.vao = m.vbo = m.ebo = 0;
              m.visible = true;
              // all other fields (position, travel, etc.) remain
              // zero-initialised
            }
            current_window->model.path = new_model_path;
            current_window->model_name = name;
            writeJson(current_window->model, new_model_path + "/info.json");
            ImGui::CloseCurrentPopup();
          } else {
            std::cout << "Name contains invalid characters "
                      << invalid_characters << std::endl;
          }
        }
        if (!name_valid) {
          ImGui::Text("Name cannot include characters \\/:*?\"<>|");
        }
        ImGui::EndPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Delete Model")) {
        ImGui::OpenPopup("delete");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delete the current model folder.");

      if (ImGui::BeginPopup("delete")) {
        ImGui::Text("Delete this model?");
        if (ImGui::Button("Confirm")) {
          std::filesystem::remove_all(current_window->model.path);
          std::string dir_path = get_models_root();
          dir_path.append("/");
          std::vector<std::string> model_folders;
          struct stat sb;
          for (const auto &entry :
               std::filesystem::directory_iterator(dir_path)) {
            if (stat(entry.path().string().c_str(), &sb) == 0 &&
                (sb.st_mode & S_IFDIR)) {
              model_folders.push_back(entry.path().string());
            }
          }
          if (model_folders.size() > 0) {
            glfwMakeContextCurrent(current_window->glfw_window);
            loadModel(current_window->model, model_folders.front().c_str());
            glfwMakeContextCurrent(glfw_settings_window);
            current_window->model_name = get_top_folder(model_folders.front());
          } else {
            current_window->model_name = "";
          }
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Import New Model")) {
        import_model_dialog.Open();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a 3D model file and map its meshes.");

      ImGui::TextWrapped(
          "Import a 3D model (FBX, glTF, OBJ, etc.) and map its meshes "
          "to controller parts. After importing, a preview window will "
          "open where you can assign each mesh to a controller part.");

      // ============================================================
      //  MATERIALS & TEXTURES
      // ============================================================
      if (!current_window->model.meshes.empty()) {
        // ---- Materials ----
        if (ImGui::TreeNode("Materials")) {
          // Ensure material_mesh is valid
          if (material_mesh >= (int)current_window->model.meshes.size())
            material_mesh = (int)current_window->model.meshes.size() - 1;
          // Get the actual mesh name for preview
          std::string previewName =
              (material_mesh >= 0 &&
               material_mesh < (int)current_window->model.meshes.size())
                  ? current_window->model.meshes[material_mesh].name
                  : "";
          if (ImGui::BeginCombo("Meshes", previewName.c_str(), 0)) {
            for (int i = 0; i < (int)current_window->model.meshes.size(); ++i) {
              const std::string &displayName =
                  current_window->model.meshes[i].name.empty()
                      ? ("Mesh " + std::to_string(i))
                      : current_window->model.meshes[i].name;
              if (ImGui::Selectable(displayName.c_str(), material_mesh == i)) {
                material_mesh = i;
              }
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select a mesh to edit its material.");

          Mesh &matMesh = current_window->model.meshes[material_mesh];
          ImGui::NewLine();
          ImGui::SliderFloat("Ambient", &matMesh.material.ambient, 0, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ambient light reflection.");
          ImGui::SliderFloat("Diffuse", &matMesh.material.diffuse, 0, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Diffuse light reflection.");
          ImGui::SliderFloat("Specular", &matMesh.material.specular, 0, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Specular (shininess) intensity.");
          ImGui::SliderFloat("Shininess", &matMesh.material.shininess, 1, 256);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Specular exponent (higher = sharper highlights).");
          ImGui::ColorEdit3("Color", matMesh.material.color);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Base colour of the mesh.");
          matMesh.original_color[0] = matMesh.material.color[0];
          matMesh.original_color[1] = matMesh.material.color[1];
          matMesh.original_color[2] = matMesh.material.color[2];
          matMesh.original_alpha = matMesh.material.alpha;
          ImGui::TreePop();
        }

        // ---- Textures ----
        if (ImGui::TreeNode("Textures")) {
          if (texture_mesh >= (int)current_window->model.meshes.size())
            texture_mesh = (int)current_window->model.meshes.size() - 1;
          std::string previewName =
              (texture_mesh >= 0 &&
               texture_mesh < (int)current_window->model.meshes.size())
                  ? current_window->model.meshes[texture_mesh].name
                  : "";
          if (ImGui::BeginCombo("Meshes", previewName.c_str(), 0)) {
            for (int i = 0; i < (int)current_window->model.meshes.size(); ++i) {
              const std::string &displayName =
                  current_window->model.meshes[i].name.empty()
                      ? ("Mesh " + std::to_string(i))
                      : current_window->model.meshes[i].name;
              if (ImGui::Selectable(displayName.c_str(), texture_mesh == i)) {
                texture_mesh = i;
              }
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select a mesh to manage its textures.");

          Mesh &texMesh = current_window->model.meshes[texture_mesh];
          ImGui::NewLine();
          static size_t current_texture = 0;
          if (ImGui::BeginListBox("Textures")) {
            for (size_t n = 0; n < texMesh.textures.size(); n++) {
              const bool is_selected = (current_texture == n);
              if (ImGui::Selectable(texMesh.textures[n].name.c_str(),
                                    is_selected)) {
                current_texture = n;
              }
            }
            ImGui::EndListBox();
          }
          if (texMesh.textures.size() < 16) {
            if (ImGui::Button("New Texture")) {
              texture_dialog.Open();
              current_texture = texMesh.textures.size();
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Add a new texture to the selected mesh.");
          }
          if (!texMesh.textures.empty()) {
            if (texMesh.textures.size() < 16) {
              ImGui::SameLine();
            }
            if (ImGui::Button("Delete Texture")) {
              glfwMakeContextCurrent(current_window->glfw_window);
              deleteTexture(texMesh.textures[current_texture].id);
              texMesh.textures.erase(texMesh.textures.begin() +
                                     current_texture);
              glfwMakeContextCurrent(glfw_settings_window);
              current_texture = 0;
              for (size_t i = 0; i < texMesh.textures.size(); i++) {
                texMesh.textures[i].name =
                    std::to_string(i + 1) + ": " + texMesh.textures[i].path;
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Remove the selected texture.");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
              if (current_texture > 0) {
                Texture temp = texMesh.textures[current_texture - 1];
                texMesh.textures[current_texture - 1] =
                    texMesh.textures[current_texture];
                texMesh.textures[current_texture] = temp;
                current_texture--;
              }
              for (size_t i = 0; i < texMesh.textures.size(); i++) {
                texMesh.textures[i].name =
                    std::to_string(i + 1) + ": " + texMesh.textures[i].path;
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Move selected texture up.");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
              if (current_texture < texMesh.textures.size() - 1) {
                Texture temp = texMesh.textures[current_texture + 1];
                texMesh.textures[current_texture + 1] =
                    texMesh.textures[current_texture];
                texMesh.textures[current_texture] = temp;
                current_texture++;
              }
              for (size_t i = 0; i < texMesh.textures.size(); i++) {
                texMesh.textures[i].name =
                    std::to_string(i + 1) + ": " + texMesh.textures[i].path;
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Move selected texture down.");

            Texture *t = &texMesh.textures[current_texture];
            ImGui::NewLine();
            enum Type { diffuse, specular, emission, type_count };
            const char *type_names[type_count] = {"Diffuse", "Specular",
                                                  "Emissive"};
            const char *type_name = (t->type >= 0 && t->type < type_count)
                                        ? type_names[t->type]
                                        : "Unknown";
            ImGui::SliderInt("Type", &t->type, 0, type_count - 1, type_name);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Texture type: diffuse, specular, or emissive.");
            enum Wrap {
              repeat,
              mirror_repeat,
              clamp_edge,
              clamp_border,
              wrap_count
            };
            const char *wrap_names[wrap_count] = {"Repeat", "Mirrored Repeat",
                                                  "Clamp to Edge",
                                                  "Clamp to Border"};
            const char *wrap_name_x = (t->wrapX >= 0 && t->wrapX < wrap_count)
                                          ? wrap_names[t->wrapX]
                                          : "Unknown";
            if (ImGui::SliderInt("X Wrap", &t->wrapX, 0, wrap_count - 1,
                                 wrap_name_x)) {
              glfwMakeContextCurrent(current_window->glfw_window);
              glBindTexture(GL_TEXTURE_2D, t->id);
              switch (t->wrapX) {
              case repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                break;
              case mirror_repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                GL_MIRRORED_REPEAT);
                break;
              case clamp_edge:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                GL_CLAMP_TO_EDGE);
                break;
              case clamp_border:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                GL_CLAMP_TO_BORDER);
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                                 t->border);
                break;
              }
              glfwMakeContextCurrent(glfw_settings_window);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Horizontal texture wrapping mode.");
            const char *wrap_name_y = (t->wrapY >= 0 && t->wrapY < wrap_count)
                                          ? wrap_names[t->wrapY]
                                          : "Unknown";
            if (ImGui::SliderInt("Y Wrap", &t->wrapY, 0, wrap_count - 1,
                                 wrap_name_y)) {
              glfwMakeContextCurrent(current_window->glfw_window);
              glBindTexture(GL_TEXTURE_2D, t->id);
              switch (t->wrapY) {
              case repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                break;
              case mirror_repeat:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                GL_MIRRORED_REPEAT);
                break;
              case clamp_edge:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                GL_CLAMP_TO_EDGE);
                break;
              case clamp_border:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                GL_CLAMP_TO_BORDER);
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                                 t->border);
                break;
              }
              glfwMakeContextCurrent(glfw_settings_window);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Vertical texture wrapping mode.");
            if (ImGui::ColorEdit3("Border Color", t->border)) {
              glfwMakeContextCurrent(current_window->glfw_window);
              glBindTexture(GL_TEXTURE_2D, t->id);
              glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                               t->border);
              glfwMakeContextCurrent(glfw_settings_window);
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Border color used when clamp‑to‑border is selected.");
            ImGui::InputFloat("Offset X", &t->offsetX, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Horizontal texture offset.");
            ImGui::InputFloat("Offset Y", &t->offsetY, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Vertical texture offset.");
            ImGui::InputFloat("Scale X", &t->scaleX, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Horizontal texture scale.");
            ImGui::InputFloat("Scale Y", &t->scaleY, 0.01f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Vertical texture scale.");
            ImGui::SliderAngle("Rotation", &t->rotation, -180.0f, 180.0f);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Texture rotation angle.");
          }
          ImGui::TreePop();
        }
      } else {
        ImGui::TextDisabled("No meshes in this model – Materials and Textures "
                            "are not available.");
      }
      // ============================================================
      //  MESH LIST TABLE
      // ============================================================
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.0f, 1.0f), "Mesh List & Editing");
      ImGui::Separator();
      ImGui::NewLine();
      ImGui::Separator();

      // ---- Dynamic mesh table ----
      ImGui::Text("Mesh List (%zu meshes)",
                  current_window->model.meshes.size());
      if (ImGui::BeginTable("MeshTable", 11,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Assigned Part",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Parent", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Joystick", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pivot", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Touch W", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Touch H", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)current_window->model.meshes.size(); ++i) {
          Mesh &mesh = current_window->model.meshes[i];
          bool hasMesh = (mesh.elements > 0);
          ImGui::TableNextRow();
          if (selected_mesh == i) {
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.35f, 0.15f, 0.45f, 1.0f)));
          }
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%d", i);
          ImGui::TableSetColumnIndex(1);
          if (hasMesh) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.3f, 0.1f, 0.4f, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.2f, 0.05f, 0.3f, 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,
                                ImVec2(0.0f, 0.5f));
            if (ImGui::Button(mesh.name.c_str(), ImVec2(-1, 0))) {
              selected_mesh = i;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
          } else {
            ImGui::TextDisabled("%s (empty)", mesh.name.c_str());
          }

          // ---- Assigned Part Column ----
          ImGui::TableSetColumnIndex(2);
          if (hasMesh) {
            int current_assignment =
                mesh.assignedPart + 1; // +1 for "Unassigned"
            const char *part_names[36];
            part_names[0] = "Unassigned";
            for (int j = 0; j < 35; ++j)
              part_names[j + 1] = mesh_names[j].c_str();
            ImGui::PushID(i + 1000);
            if (ImGui::Combo("##assign", &current_assignment, part_names, 36)) {
              mesh.assignedPart = current_assignment - 1;
              // After setting mesh.assignedPart, propagate touch dimensions if
              // applicable
              if (mesh.assignedPart == 29) {
                // Copy to touch_point1 (30) and touch_point2 (31)
                for (auto &m : current_window->model.meshes) {
                  if (m.assignedPart == 30 || m.assignedPart == 31) {
                    m.touch_width = mesh.touch_width;
                    m.touch_height = mesh.touch_height;
                  }
                }
              } else if (mesh.assignedPart == 32) {
                // Copy to touch_point3 (33) and touch_point4 (34)
                for (auto &m : current_window->model.meshes) {
                  if (m.assignedPart == 33 || m.assignedPart == 34) {
                    m.touch_width = mesh.touch_width;
                    m.touch_height = mesh.touch_height;
                  }
                }
              }
              writeJson(current_window->model,
                        current_window->model.path + "/info.json");
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Assign this mesh to a controller part.");
            ImGui::PopID();

            // Show default binding (if any)
            int part = mesh.assignedPart;
            if (part >= 9 && part <= 34) {
              ImGui::SameLine();
              ImGui::TextDisabled("(b%d)", part - 9);
            }
          } else {
            ImGui::TextDisabled("N/A");
          }

          // ---- Parent Column ----
          ImGui::TableSetColumnIndex(3);
          if (hasMesh) {
            int current_parent = mesh.parentIndex + 1; // +1 for "None"
            const char *parent_names[36];
            parent_names[0] = "None";
            for (int j = 0; j < 35; ++j)
              parent_names[j + 1] = mesh_names[j].c_str();
            ImGui::PushID(i + 2000);
            if (ImGui::Combo("##parent", &current_parent, parent_names, 36)) {
              mesh.parentIndex = current_parent - 1;
              writeJson(current_window->model,
                        current_window->model.path + "/info.json");
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Attach this mesh to a parent part (e.g., stick).");
            ImGui::PopID();
          } else {
            ImGui::TextDisabled("N/A");
          }

          // ---- Visible Column ----
          ImGui::TableSetColumnIndex(4);
          if (hasMesh) {
            ImGui::PushID(i + 3000);
            ImGui::Checkbox("##visible", &mesh.visible);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Show/hide this mesh in the 3D view.");
            ImGui::PopID();
          } else {
            ImGui::TextDisabled(" ");
          }

          // ---- Joystick Column ----
          ImGui::TableSetColumnIndex(5);
          if (hasMesh) {
            ImGui::PushID(i + 4000);
            ImGui::Checkbox("##joystick", &mesh.useJoystick);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Use raw joystick input (not gamecontroller) for this mesh.");
            ImGui::PopID();
          } else {
            ImGui::TextDisabled(" ");
          }

          // ---- Position Column ----
          ImGui::TableSetColumnIndex(6);
          if (hasMesh) {
            ImGui::Text("%.2f, %.2f, %.2f", mesh.position[0], mesh.position[1],
                        mesh.position[2]);
          } else {
            ImGui::TextDisabled("N/A");
          }

          // ---- Pivot Column ----
          ImGui::TableSetColumnIndex(7);
          if (hasMesh) {
            ImGui::Text("%.2f, %.2f, %.2f", mesh.pivot_offset[0],
                        mesh.pivot_offset[1], mesh.pivot_offset[2]);
          } else {
            ImGui::TextDisabled("N/A");
          }

          // ---- Touch Width Column ----
          ImGui::TableSetColumnIndex(8);
          bool isTouch = (mesh.assignedPart == 29 || mesh.assignedPart == 30 ||
                          mesh.assignedPart == 31 || mesh.assignedPart == 32 ||
                          mesh.assignedPart == 33 || mesh.assignedPart == 34);
          if (hasMesh && isTouch) {
            ImGui::Text("%.2f", mesh.touch_width);
          } else {
            ImGui::TextDisabled("N/A");
          }

          // ---- Touch Height Column ----
          ImGui::TableSetColumnIndex(9);
          if (hasMesh && isTouch) {
            ImGui::Text("%.2f", mesh.touch_height);
          } else {
            ImGui::TextDisabled("N/A");
          }

          // ---- Actions Column ----
          ImGui::TableSetColumnIndex(10);
          if (hasMesh) {
            if (ImGui::Button(("Import##" + std::to_string(i)).c_str())) {
              model_dialog.Open();
              selected_mesh = i;
            }
            ImGui::SameLine();
            if (ImGui::Button(("Del##" + std::to_string(i)).c_str())) {
              mesh_to_delete = i;
              ImGui::OpenPopup("delete_mesh_per_row");
            }
          } else {
            ImGui::TextDisabled(" ");
          }
        }
        ImGui::EndTable();
      }

      // ---- Per‑row delete popup ----
      if (ImGui::BeginPopup("delete_mesh_per_row")) {
        ImGui::Text("Delete this mesh?");
        if (ImGui::Button("Confirm")) {
          if (mesh_to_delete >= 0 &&
              mesh_to_delete < (int)current_window->model.meshes.size()) {
            // Erase from vector and rewrite JSON
            current_window->model.meshes.erase(
                current_window->model.meshes.begin() + mesh_to_delete);
            writeJson(current_window->model,
                      current_window->model.path + "/info.json");
            mesh_to_delete = -1;
            ImGui::CloseCurrentPopup();
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          mesh_to_delete = -1;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      ImGui::Separator();

      // ---- Detailed controls for the selected mesh ----
      if (selected_mesh >= 0 &&
          selected_mesh < (int)current_window->model.meshes.size()) {
        Mesh &selectedMesh = current_window->model.meshes[selected_mesh];
        if (selectedMesh.elements == 0) {
          ImGui::TextDisabled("No mesh loaded at index %d.", selected_mesh);
        } else {
          // ---- Highlight variables ----
          static std::map<int, std::array<float, 3>> original_colors;
          static bool highlight_selected = false;
          static float highlight_color[3] = {1.0f, 0.0f, 0.0f}; // red default

          ImGui::Text("Editing: %s", selectedMesh.name.c_str());

          // ---- Position Section ----
          ImGui::Separator();
          ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Position");
          ImGui::InputFloat("X Position", &selectedMesh.position[0], 0.01f,
                            1.0f, "%.3f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move the mesh along the X axis.");
          ImGui::InputFloat("Y Position", &selectedMesh.position[1], 0.01f,
                            1.0f, "%.3f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move the mesh along the Y axis.");
          ImGui::InputFloat("Z Position", &selectedMesh.position[2], 0.01f,
                            1.0f, "%.3f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move the mesh along the Z axis.");

          // ---- Pivot Section ----
          ImGui::Separator();
          ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f), "Pivot Point");
          ImGui::TextWrapped(
              "The pivot is the point around which the mesh rotates "
              "(for sticks, triggers, buttons). The orange circle shows its "
              "current position.");
          ImGui::InputFloat("Pivot X", &selectedMesh.pivot_offset[0], 0.01f,
                            1.0f, "%.3f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Offset of the pivot from the mesh origin (X).");
          ImGui::InputFloat("Pivot Y", &selectedMesh.pivot_offset[1], 0.01f,
                            1.0f, "%.3f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Offset of the pivot from the mesh origin (Y).");
          ImGui::InputFloat("Pivot Z", &selectedMesh.pivot_offset[2], 0.01f,
                            1.0f, "%.3f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Offset of the pivot from the mesh origin (Z).");

          // ---- Auto-center buttons ----
          if (selectedMesh.hasBBox) {
            ImGui::Text("Auto‑set pivot:");
            if (ImGui::Button("Center of Mass")) {
              glm::vec3 center = computeMeshCenter(selectedMesh);
              spdlog::info("Center of Mass: ({:.3f}, {:.3f}, {:.3f})", center.x,
                           center.y, center.z);
              selectedMesh.pivot_offset[0] = center.x;
              selectedMesh.pivot_offset[1] = center.y;
              selectedMesh.pivot_offset[2] = center.z;
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Sets the pivot to the geometric centre of the mesh.");
            ImGui::SameLine();
            if (selectedMesh.assignedPart == 5 ||
                selectedMesh.assignedPart == 6) { // stick parts
              if (ImGui::Button("Set Pivot to Stick Base")) {
                float px =
                    (selectedMesh.bboxMin.x + selectedMesh.bboxMax.x) * 0.5f;
                float py = selectedMesh.bboxMin.y;
                float pz =
                    (selectedMesh.bboxMin.z + selectedMesh.bboxMax.z) * 0.5f;
                selectedMesh.pivot_offset[0] = px;
                selectedMesh.pivot_offset[1] = py;
                selectedMesh.pivot_offset[2] = pz;
              }
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Sets the pivot to the bottom‑centre of the stick mesh. "
                    "This makes the stick rotate like a real joystick.");
            }
          } else {
            ImGui::TextDisabled(
                "Bounding box not available – auto‑center disabled.");
          }

          // ---- Rotation Section ----
          ImGui::Separator();
          ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Rotation");
          ImGui::InputFloat("Rot X (deg)", &selectedMesh.rotation[0], 0.1f,
                            1.0f, "%.1f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Euler rotation around the X axis (degrees).");
          ImGui::InputFloat("Rot Y (deg)", &selectedMesh.rotation[1], 0.1f,
                            1.0f, "%.1f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Euler rotation around the Y axis (degrees).");
          ImGui::InputFloat("Rot Z (deg)", &selectedMesh.rotation[2], 0.1f,
                            1.0f, "%.1f");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Euler rotation around the Z axis (degrees).");

          // ---- Reset Transform ----
          ImGui::Separator();
          if (ImGui::Button("Reset Transform")) {
            selectedMesh.position[0] = selectedMesh.position[1] =
                selectedMesh.position[2] = 0.0f;
            selectedMesh.pivot_offset[0] = selectedMesh.pivot_offset[1] =
                selectedMesh.pivot_offset[2] = 0.0f;
            selectedMesh.rotation[0] = selectedMesh.rotation[1] =
                selectedMesh.rotation[2] = 0.0f;
            selectedMesh.travel[0] = selectedMesh.travel[1] =
                selectedMesh.travel[2] = 0.0f;
            selectedMesh.popup_offset[0] = selectedMesh.popup_offset[1] =
                selectedMesh.popup_offset[2] = 0.0f;
            selectedMesh.popup_rotation[0] = selectedMesh.popup_rotation[1] =
                selectedMesh.popup_rotation[2] = 0.0f;
            selectedMesh.trigger_max = 0.0f;
            selectedMesh.stick_max = 0.0f;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reset all transform values (position, pivot, "
                              "rotation, travel, popup) to zero.");

          ImGui::SameLine();
          if (ImGui::Button("Save Model")) {
            writeJson(current_window->model,
                      current_window->model.path + "/info.json");
            spdlog::info("Model saved to {}", current_window->model.path);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Write current settings to info.json.");

          // ---- Edit Highlight ----
          ImGui::Checkbox("Highlight", &current_window->highlight_enabled);
          ImGui::SameLine();
          ImGui::ColorEdit3("Highlight Color", current_window->highlight_color);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Temporarily colour the selected mesh for editing.");

          if (current_window->highlight_enabled) {
            if (current_window->original_colors.find(selected_mesh) ==
                current_window->original_colors.end()) {
              current_window->original_colors[selected_mesh] = {
                  selectedMesh.material.color[0],
                  selectedMesh.material.color[1],
                  selectedMesh.material.color[2]};
            }
            selectedMesh.material.color[0] = current_window->highlight_color[0];
            selectedMesh.material.color[1] = current_window->highlight_color[1];
            selectedMesh.material.color[2] = current_window->highlight_color[2];
            selectedMesh.highlight_value = 0.0f;
          } else {
            auto it = current_window->original_colors.find(selected_mesh);
            if (it != current_window->original_colors.end()) {
              selectedMesh.material.color[0] = it->second[0];
              selectedMesh.material.color[1] = it->second[1];
              selectedMesh.material.color[2] = it->second[2];
              selectedMesh.highlight_value = 0.0f;
              current_window->original_colors.erase(it);
            }
          }

          // ---- Travel (for buttons/triggers) ----
          int part = selectedMesh.assignedPart;
          if (part > 8 && part < 30) {
            ImGui::Separator();
            ImGui::Text("Travel (button press offset)");
            ImGui::InputFloat("X Travel", &selectedMesh.travel[0], 0.01f, 1.0f,
                              "%.3f");
            ImGui::InputFloat("Y Travel", &selectedMesh.travel[1], 0.01f, 1.0f,
                              "%.3f");
            ImGui::InputFloat("Z Travel", &selectedMesh.travel[2], 0.01f, 1.0f,
                              "%.3f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Movement when the button is pressed.");
          }

          // ---- Popup offsets ----
          if ((part == 18 || part == 19) || (part > 24 && part < 29) ||
              (part == 3 || part == 4)) {
            ImGui::Separator();
            ImGui::Text("Popup animation");
            ImGui::InputFloat("Popup Offset X", &selectedMesh.popup_offset[0],
                              0.01f, 1.0f, "%.3f");
            ImGui::InputFloat("Popup Offset Y", &selectedMesh.popup_offset[1],
                              0.01f, 1.0f, "%.3f");
            ImGui::InputFloat("Popup Offset Z", &selectedMesh.popup_offset[2],
                              0.01f, 1.0f, "%.3f");
            ImGui::SliderAngle("Popup Yaw", &selectedMesh.popup_rotation[1],
                               -180, 180);
            ImGui::SliderAngle("Popup Pitch", &selectedMesh.popup_rotation[0],
                               -180, 180);
            ImGui::SliderAngle("Popup Roll", &selectedMesh.popup_rotation[2],
                               -180, 180);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Offset and rotation when the part 'pops up' (e.g. bumper).");
          }

          // ---- Stick max ----
          if (part == 5 || part == 6) {
            ImGui::Separator();
            if (ImGui::SliderAngle("Max Angle", &selectedMesh.stick_max, 0.0f,
                                   45.0f)) {
              // Propagate to ring and cap if they exist
              for (auto &m : current_window->model.meshes) {
                if (m.assignedPart == 7 || m.assignedPart == 16 ||
                    m.assignedPart == 8 || m.assignedPart == 17) {
                  m.stick_max = selectedMesh.stick_max;
                }
              }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Maximum deflection angle for the stick.");
          }

          // ---- Trigger max ----
          if (part == 3 || part == 4) {
            ImGui::Separator();
            ImGui::SliderAngle("Max Angle", &selectedMesh.trigger_max, 0.0f,
                               90.0f);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Maximum pull angle for the trigger.");
          }

          // ---- Touchpad dimensions ----
          if (part == 29 || part == 32) {
            ImGui::Separator();
            ImGui::Text("Touchpad dimensions");
            selectedMesh.visible = true;
            // Find associated touch point meshes
            int tp1, tp2;
            if (part == 29) {
              tp1 = 30;
              tp2 = 31;
            } else {
              tp1 = 33;
              tp2 = 34;
            }
            for (auto &m : current_window->model.meshes) {
              if (m.assignedPart == tp1 || m.assignedPart == tp2) {
                m.visible = true;
                m.touch_width = selectedMesh.touch_width;
                m.touch_height = selectedMesh.touch_height;
              }
            }
            if (ImGui::SliderFloat("Touch Area Width",
                                   &selectedMesh.touch_width, 0.01f, 5.0f,
                                   "%.2f")) {
              for (auto &m : current_window->model.meshes) {
                if (m.assignedPart == tp1 || m.assignedPart == tp2) {
                  m.touch_width = selectedMesh.touch_width;
                }
              }
            }
            if (ImGui::SliderFloat("Touch Area Height",
                                   &selectedMesh.touch_height, 0.01f, 5.0f,
                                   "%.2f")) {
              for (auto &m : current_window->model.meshes) {
                if (m.assignedPart == tp1 || m.assignedPart == tp2) {
                  m.touch_height = selectedMesh.touch_height;
                }
              }
            }
          }

          // ---- Touch Area Visualisation ----
          if (part == 29 || part == 30 || part == 31 || part == 32 ||
              part == 33 || part == 34) {
            ImGui::Separator();
            ImGui::Checkbox("Show Touch Area",
                            &current_window->show_touch_area);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip(
                  "Draws a magenta rectangle showing the touch area.");

            if (current_window->show_touch_area) {
              ImGui::SliderFloat("X Offset",
                                 &current_window->touch_area_offset[0], -2.0f,
                                 2.0f, "%.3f");
              ImGui::SliderFloat("Y Offset",
                                 &current_window->touch_area_offset[1], -2.0f,
                                 2.0f, "%.3f");
              ImGui::SliderFloat("Z Offset",
                                 &current_window->touch_area_offset[2], -2.0f,
                                 2.0f, "%.3f");
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Offset of the touch area rectangle from the "
                                  "touchpad's origin.");
            }
          }

          // ---- Custom scale ----
          if (selectedMesh.useCustomScale) {
            ImGui::Separator();
            ImGui::Text("Custom scale");
            ImGui::InputFloat("Scale X", &selectedMesh.scale[0], 0.01f, 1.0f,
                              "%.2f");
            ImGui::InputFloat("Scale Y", &selectedMesh.scale[1], 0.01f, 1.0f,
                              "%.2f");
            ImGui::InputFloat("Scale Z", &selectedMesh.scale[2], 0.01f, 1.0f,
                              "%.2f");
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Custom scale for this mesh.");
          }

          ImGui::Separator();
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                             "Changes are saved when you click 'Save Model' or "
                             "switch models.");
        }
      } else {
        ImGui::TextDisabled(
            "Select a mesh from the table above to edit its properties.");
      }
    } // end Model

    // ============================================================
    // GYRO
    // ============================================================
    if (ImGui::CollapsingHeader("Gyro")) {
      bool has_gyro = false;
      if (current_window->is_gamecontroller && current_window->sdl_controller) {
        has_gyro = SDL_GameControllerHasSensor(current_window->sdl_controller,
                                               SDL_SENSOR_GYRO) == SDL_TRUE;
      }
      if (!has_gyro && current_window->gyro_sensor) {
        has_gyro = true;
      }

      bool enabled = current_window->gyro_enabled;
      ImGui::BeginDisabled(!has_gyro);
      if (ImGui::Checkbox("Enable Gyro", &enabled)) {
        current_window->gyro_enabled = enabled;
        if (current_window->is_gamecontroller &&
            current_window->sdl_controller) {
          SDL_GameControllerSetSensorEnabled(current_window->sdl_controller,
                                             SDL_SENSOR_GYRO,
                                             (SDL_bool)enabled);
        }
        if (enabled) {
          current_window->gyro_toggled = true;
          Uint64 timestamp;
          if (SDL_GameControllerGetSensorDataWithTimestamp(
                  current_window->sdl_controller, SDL_SENSOR_GYRO, &timestamp,
                  current_window->gyro_data, 3) == 0) {
            current_window->gyro_time = timestamp;
          }
        } else {
          current_window->gyro_matrix = glm::mat4(1.0f);
        }
      }
      ImGui::EndDisabled();

      if (has_gyro && current_window->gyro_enabled) {
        ImGui::SliderFloat("Gyro Sensitivity",
                           &current_window->gyro_sensitivity, 0.1f, 100.0f,
                           "%.1f");
        ImGui::SliderInt("Gyro Correction", &current_window->gyro_correction, 0,
                         10);
        if (ImGui::Button("Reset Gyro")) {
          current_window->gyro_matrix = glm::mat4(1.0f);
        }
        ImGui::NewLine();
        ImGui::Text("Reset Gyro button combo");

        std::string button1_name = "";
        if (current_window->reset_gyro_button1 > -1) {
          button1_name =
              input_names[current_window->reset_gyro_button1].c_str();
        } else {
          button1_name = "none";
        }
        if (ImGui::BeginCombo("Button 1", button1_name.c_str(), 0)) {
          for (unsigned i = 0; i < 22; i++) {
            if (i > 0) {
              if (ImGui::Selectable(input_names[i - 1].c_str())) {
                current_window->reset_gyro_button1 = i - 1;
              }
            } else {
              if (ImGui::Selectable("none")) {
                current_window->reset_gyro_button1 = -1;
              }
            }
          }
          ImGui::EndCombo();
        }

        std::string button2_name = "";
        if (current_window->reset_gyro_button2 > -1) {
          button2_name =
              input_names[current_window->reset_gyro_button2].c_str();
        } else {
          button2_name = "none";
        }
        if (ImGui::BeginCombo("Button 2", button2_name.c_str(), 0)) {
          for (unsigned i = 0; i < 22; i++) {
            if (i > 0) {
              if (ImGui::Selectable(input_names[i - 1].c_str())) {
                current_window->reset_gyro_button2 = i - 1;
              }
            } else {
              if (ImGui::Selectable("none")) {
                current_window->reset_gyro_button2 = -1;
              }
            }
          }
          ImGui::EndCombo();
        }

        ImGui::Checkbox("Gyro Debug Logging",
                        &current_window->gyro_debug_logging);
      } else if (!has_gyro) {
        ImGui::TextDisabled("No gyroscope detected for this controller.");
      }
    }

    // ============================================================
    // LIGHTING
    // ============================================================
    if (ImGui::CollapsingHeader("Lighting")) {
      // ---- Directional Lights ----
      if (ImGui::TreeNode("Directional Lights")) {
        static unsigned current_dir_light = 0;
        std::string preview_name = "";
        if (current_window->direct_lights.size() > 0)
          preview_name =
              current_window->direct_lights[current_dir_light].name.c_str();
        if (ImGui::BeginCombo("Lights", preview_name.c_str(), 0)) {
          for (unsigned i = 0; i < current_window->direct_lights.size(); i++) {
            if (ImGui::Selectable(
                    current_window->direct_lights[i].name.c_str())) {
              current_dir_light = i;
            }
          }
          ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Select a directional light to edit.");

        // New light button
        if (current_window->direct_lights.size() < 16) {
          if (ImGui::Button("New Light")) {
            direct_light new_dir_light;
            std::string new_light_name = "Directional Light ";
            static unsigned count = current_window->direct_lights.size() + 1;
            bool name_exists = false;
            while (true) {
              name_exists = false;
              std::string test_name = new_light_name;
              test_name.append(std::to_string(count));
              for (direct_light d : current_window->direct_lights) {
                if (d.name == test_name.c_str()) {
                  name_exists = true;
                  count++;
                  break;
                }
              }
              if (!name_exists)
                break;
            }
            new_dir_light.name =
                new_light_name.append(std::to_string(count)).c_str();
            count++;
            current_window->direct_lights.push_back(new_dir_light);
            current_dir_light = current_window->direct_lights.size() - 1;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a new directional light.");
        }

        // Delete and edit – only if we have lights
        if (current_window->direct_lights.size() > 0) {
          if (current_window->direct_lights.size() < 16)
            ImGui::SameLine();
          if (ImGui::Button("Delete Light")) {
            current_window->direct_lights.erase(
                current_window->direct_lights.begin() + current_dir_light);
            // Reset index to 0 (or keep it valid)
            if (current_window->direct_lights.size() > 0) {
              if (current_dir_light >= current_window->direct_lights.size())
                current_dir_light = 0;
            }
            // If we deleted the only light, skip the rest of the editing UI
            // by using a goto or by re-checking the size.
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete the selected directional light.");
        }

        // ---- Editing controls (only if we still have lights) ----
        if (current_window->direct_lights.size() > 0) {
          ImGui::NewLine();
          direct_light *d = &current_window->direct_lights[current_dir_light];
          char name[64] = {};
          if (ImGui::InputTextWithHint("Name", d->name.c_str(), name,
                                       IM_ARRAYSIZE(name),
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
            bool exists = false;
            for (direct_light dl : current_window->direct_lights) {
              if (dl.name == name) {
                exists = true;
                break;
              }
            }
            if (!exists)
              d->name = std::string(name);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rename the light.");
          ImGui::SliderFloat("X Direction", &d->direction.x, -1, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light direction X.");
          ImGui::SliderFloat("Y Direction", &d->direction.y, -1, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light direction Y.");
          ImGui::SliderFloat("Z Direction", &d->direction.z, -1, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light direction Z.");
          ImGui::ColorEdit3("Color", d->color);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light colour.");
        }
        ImGui::TreePop();
      }

      // ---- Point Lights ----
      if (ImGui::TreeNode("Point Lights")) {
        static unsigned current_point_light = 0;
        std::string preview_name = "";
        if (current_window->point_lights.size() > 0)
          preview_name =
              current_window->point_lights[current_point_light].name.c_str();
        if (ImGui::BeginCombo("Lights", preview_name.c_str(), 0)) {
          for (unsigned i = 0; i < current_window->point_lights.size(); i++) {
            if (ImGui::Selectable(
                    current_window->point_lights[i].name.c_str())) {
              current_point_light = i;
            }
          }
          ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Select a point light to edit.");

        // New light button
        if (current_window->point_lights.size() < 16) {
          if (ImGui::Button("New Light")) {
            point_light new_point_light;
            std::string new_light_name = "Point Light ";
            static unsigned count = current_window->point_lights.size() + 1;
            bool name_exists = false;
            while (true) {
              name_exists = false;
              std::string test_name = new_light_name;
              test_name.append(std::to_string(count));
              for (point_light p : current_window->point_lights) {
                if (p.name == test_name.c_str()) {
                  name_exists = true;
                  count++;
                  break;
                }
              }
              if (!name_exists)
                break;
            }
            new_point_light.name =
                new_light_name.append(std::to_string(count)).c_str();
            count++;
            new_point_light.position.x = 2.0f;
            new_point_light.position.y = 2.0f;
            new_point_light.position.z = 2.0f;
            current_window->point_lights.push_back(new_point_light);
            current_point_light = current_window->point_lights.size() - 1;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a new point light.");
        }

        // Delete button
        if (current_window->point_lights.size() > 0) {
          if (current_window->point_lights.size() < 16)
            ImGui::SameLine();
          if (ImGui::Button("Delete Light")) {
            current_window->point_lights.erase(
                current_window->point_lights.begin() + current_point_light);
            if (current_window->point_lights.size() > 0) {
              if (current_point_light >= current_window->point_lights.size())
                current_point_light = 0;
            }
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete the selected point light.");
        }

        // ---- Editing controls (only if we still have lights) ----
        if (current_window->point_lights.size() > 0) {
          ImGui::NewLine();
          point_light *p = &current_window->point_lights[current_point_light];
          char name[64] = {};
          if (ImGui::InputTextWithHint("Name", p->name.c_str(), name,
                                       IM_ARRAYSIZE(name),
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
            bool exists = false;
            for (point_light pl : current_window->point_lights) {
              if (pl.name == name) {
                exists = true;
                break;
              }
            }
            if (!exists)
              p->name = std::string(name);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rename the light.");
          ImGui::Checkbox("Hide Source", &p->hide);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide the light bulb visual.");
          ImGui::SliderFloat("X Position", &p->position.x, -10, 10);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light position X.");
          ImGui::SliderFloat("Y Position", &p->position.y, -10, 10);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light position Y.");
          ImGui::SliderFloat("Z Position", &p->position.z, -10, 10);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light position Z.");
          ImGui::SliderFloat("Brightness", &p->intensity, 0, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light intensity.");
          if (ImGui::ColorEdit3("Color", p->color)) {
            p->ambient.r = p->color[0] * 0.05f;
            p->ambient.g = p->color[1] * 0.05f;
            p->ambient.b = p->color[2] * 0.05f;
            p->diffuse.r = p->color[0] * 0.8f;
            p->diffuse.g = p->color[1] * 0.8f;
            p->diffuse.b = p->color[2] * 0.8f;
            p->specular.r = p->color[0];
            p->specular.g = p->color[1];
            p->specular.b = p->color[2];
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light colour.");
        }
        ImGui::TreePop();
      }

      // ---- Spot Lights ----
      if (ImGui::TreeNode("Spot Lights")) {
        static unsigned current_spot_light = 0;
        std::string preview_name = "";
        if (current_window->spot_lights.size() > 0)
          preview_name =
              current_window->spot_lights[current_spot_light].name.c_str();
        if (ImGui::BeginCombo("Lights", preview_name.c_str(), 0)) {
          for (unsigned i = 0; i < current_window->spot_lights.size(); i++) {
            if (ImGui::Selectable(
                    current_window->spot_lights[i].name.c_str())) {
              current_spot_light = i;
            }
          }
          ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Select a spot light to edit.");

        // New light button
        if (current_window->spot_lights.size() < 16) {
          if (ImGui::Button("New Light")) {
            spot_light new_spot_light;
            std::string new_light_name = "Spot Light ";
            static unsigned count = current_window->spot_lights.size() + 1;
            bool name_exists = false;
            while (true) {
              name_exists = false;
              std::string test_name = new_light_name;
              test_name.append(std::to_string(count));
              for (spot_light s : current_window->spot_lights) {
                if (s.name == test_name.c_str()) {
                  name_exists = true;
                  count++;
                  break;
                }
              }
              if (!name_exists)
                break;
            }
            new_spot_light.name =
                new_light_name.append(std::to_string(count)).c_str();
            count++;
            new_spot_light.position.x = 0.0f;
            new_spot_light.position.y = 0.0f;
            new_spot_light.position.z = 2.0f;
            current_window->spot_lights.push_back(new_spot_light);
            current_spot_light = current_window->spot_lights.size() - 1;
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a new spot light.");
        }

        // Delete button
        if (current_window->spot_lights.size() > 0) {
          if (current_window->spot_lights.size() < 16)
            ImGui::SameLine();
          if (ImGui::Button("Delete Light")) {
            current_window->spot_lights.erase(
                current_window->spot_lights.begin() + current_spot_light);
            if (current_window->spot_lights.size() > 0) {
              if (current_spot_light >= current_window->spot_lights.size())
                current_spot_light = 0;
            }
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete the selected spot light.");
        }

        // ---- Editing controls (only if we still have lights) ----
        if (current_window->spot_lights.size() > 0) {
          ImGui::NewLine();
          spot_light *s = &current_window->spot_lights[current_spot_light];
          char name[64] = {};
          if (ImGui::InputTextWithHint("Name", s->name.c_str(), name,
                                       IM_ARRAYSIZE(name),
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
            bool exists = false;
            for (spot_light sl : current_window->spot_lights) {
              if (sl.name == name) {
                exists = true;
                break;
              }
            }
            if (!exists)
              s->name = std::string(name);
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rename the light.");
          ImGui::Checkbox("Hide Source", &s->hide);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide the light bulb visual.");
          ImGui::SliderFloat("X Position", &s->position.x, -10, 10);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light position X.");
          ImGui::SliderFloat("Y Position", &s->position.y, -10, 10);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light position Y.");
          ImGui::SliderFloat("Z Position", &s->position.z, -10, 10);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light position Z.");
          ImGui::SliderFloat("Brightness", &s->intensity, 0, 1);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light intensity.");
          if (ImGui::ColorEdit3("Color", s->color)) {
            s->ambient.r = s->color[0] * 0.05f;
            s->ambient.g = s->color[1] * 0.05f;
            s->ambient.b = s->color[2] * 0.05f;
            s->diffuse.r = s->color[0] * 0.8f;
            s->diffuse.g = s->color[1] * 0.8f;
            s->diffuse.b = s->color[2] * 0.8f;
            s->specular.r = s->color[0];
            s->specular.g = s->color[1];
            s->specular.b = s->color[2];
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light colour.");
          if (ImGui::SliderFloat("Yaw", &s->yaw, -180, 180)) {
            s->direction.x =
                cos(glm::radians(s->pitch)) * sin(glm::radians(s->yaw + 180));
            s->direction.y = sin(glm::radians(s->pitch));
            s->direction.z =
                cos(glm::radians(s->pitch)) * cos(glm::radians(s->yaw + 180));
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Horizontal direction.");
          if (ImGui::SliderFloat("Pitch", &s->pitch, -90, 90)) {
            s->direction.x =
                cos(glm::radians(s->pitch)) * sin(glm::radians(s->yaw + 180));
            s->direction.y = sin(glm::radians(s->pitch));
            s->direction.z =
                cos(glm::radians(s->pitch)) * cos(glm::radians(s->yaw + 180));
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Vertical direction.");
          ImGui::SliderFloat("Beam Angle", &s->cutoff, 0, 90);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Inner cone angle.");
          ImGui::SliderFloat("Edge Blur", &s->outer_cutoff, 0, 100);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Softness of the cone edge.");
        }
        ImGui::TreePop();
      }
    }
    // ============================================================
    // MAPPING
    // ============================================================
    if (ImGui::CollapsingHeader("Mapping")) {
      ImGui::Checkbox("Log Button Presses", &g_log_buttons);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Log controller inputs to console.");
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1, 1, 0, 1), "Loaded: %s",
                         g_loaded_mapping_name.empty()
                             ? "(default)"
                             : g_loaded_mapping_name.c_str());

      // Build the list of available bindings for the current controller
      std::vector<std::string> allBindings;
      if (current_window->is_gamecontroller && current_window->sdl_controller) {
        SDL_Joystick *joy =
            SDL_GameControllerGetJoystick(current_window->sdl_controller);
        if (joy) {
          int numButtons = SDL_JoystickNumButtons(joy);
          for (int i = 0; i < numButtons; ++i)
            allBindings.push_back("b" + std::to_string(i));
          int numAxes = SDL_JoystickNumAxes(joy);
          for (int i = 0; i < numAxes; ++i) {
            allBindings.push_back("a" + std::to_string(i) + "+");
            allBindings.push_back("a" + std::to_string(i) + "-");
          }
          int numHats = SDL_JoystickNumHats(joy);
          for (int h = 0; h < numHats; ++h)
            for (int d = 0; d < 8; ++d)
              allBindings.push_back("h" + std::to_string(h) + "." +
                                    std::to_string(d));
          int numTouchpads =
              SDL_GameControllerGetNumTouchpads(current_window->sdl_controller);
          for (int t = 0; t < numTouchpads; ++t) {
            int numFingers = SDL_GameControllerGetNumTouchpadFingers(
                current_window->sdl_controller, t);
            for (int f = 0; f < numFingers; ++f) {
              allBindings.push_back("t" + std::to_string(t) + "_f" +
                                    std::to_string(f) + "_x");
              allBindings.push_back("t" + std::to_string(t) + "_f" +
                                    std::to_string(f) + "_y");
            }
          }
          allBindings.push_back("unbound");
        }
      } else if (!current_window->is_gamecontroller &&
                 current_window->sdl_joystick) {
        int numAxes = SDL_JoystickNumAxes(current_window->sdl_joystick);
        int numButtons = SDL_JoystickNumButtons(current_window->sdl_joystick);
        int numHats = SDL_JoystickNumHats(current_window->sdl_joystick);
        allBindings.push_back("unbound");
        for (int i = 0; i < numButtons; ++i)
          allBindings.push_back("b" + std::to_string(i));
        for (int i = 0; i < numAxes; ++i) {
          allBindings.push_back("a" + std::to_string(i));
          allBindings.push_back("a" + std::to_string(i) + "+");
          allBindings.push_back("a" + std::to_string(i) + "-");
        }
        for (int h = 0; h < numHats; ++h)
          for (int d = 0; d < 8; ++d)
            allBindings.push_back("h" + std::to_string(h) + "." +
                                  std::to_string(d));
      } else {
        ImGui::TextDisabled("No controller connected.");
        goto end_mapping;
      }

      if (ImGui::BeginTable("MappingTable", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Part", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Invert", ImGuiTableColumnFlags_WidthFixed,
                                40.0f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 35; ++i) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%s", mesh_names[i].c_str());

          ImGui::TableSetColumnIndex(1);
          ImGui::PushID(i + 5000);
          ImGui::Checkbox("##invert", &current_window->invert_mapping[i]);
          ImGui::PopID();

          ImGui::TableSetColumnIndex(2);
          std::string currentBinding = current_window->mapping[i];
          std::string displayLabel =
              currentBinding.empty()
                  ? "unbound"
                  : currentBinding + " (" +
                        getBindingDescription(currentBinding) + ")";

          std::string comboID = "##combo_part_" + std::to_string(i);
          if (ImGui::BeginCombo(comboID.c_str(), displayLabel.c_str(),
                                ImGuiComboFlags_HeightLargest)) {
            for (const std::string &bind : allBindings) {
              std::string display =
                  bind + " (" + getBindingDescription(bind) + ")";
              bool isSelected = (bind == currentBinding);
              if (ImGui::Selectable(display.c_str(), isSelected)) {
                current_window->mapping[i] = (bind == "unbound") ? "" : bind;
              }
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Assign a controller input to this part.");
        }
        ImGui::EndTable();
      }

      // ---- Action buttons ----
      if (ImGui::Button("Apply Mapping")) {
        spdlog::info("Mapping stored. It will be used in the input loop.");
        g_loaded_mapping_name = "(applied)";
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Apply current mapping to the model.");

      ImGui::SameLine();

      if (ImGui::Button("Save Mapping")) {
        ImGui::OpenPopup("save_mapping_popup");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save current mapping to a file.");

      if (ImGui::BeginPopup("save_mapping_popup")) {
        char name[32] = {};
        static bool name_valid = true;
        if (ImGui::InputText("Mapping Name", name, IM_ARRAYSIZE(name),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
          bool valid = check_filename_valid(name);
          name_valid = valid;
          if (valid) {
            std::filesystem::path path(SDL_GetBasePath());
            path.append("mapping/");
            std::filesystem::create_directory(path);
            path.append(name);
            open_ofstream(path);
            std::string mapping = "";
            for (int i = 0; i < 35; ++i) {
              if (!current_window->mapping[i].empty()) {
                // Use mesh_names[i] as the key for compatibility with older
                // mapping files? We'll use the part name (mesh_names[i]) as
                // key.
                mapping.append(mesh_names[i]);
                mapping.append(":");
                mapping.append(current_window->mapping[i]);
                mapping.append(",");
              }
            }
            write_line(mapping);
            close_ofstream();
            g_loaded_mapping_name = name;
            spdlog::info("Saved mapping to {}", name);
            ImGui::CloseCurrentPopup();
          } else {
            ImGui::Text("Invalid characters in name.");
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Enter a name for the mapping file.");
        if (!name_valid)
          ImGui::Text("Name cannot include characters \\/:*?\"<>|");
        ImGui::EndPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Load Mapping")) {
        ImGui::OpenPopup("load_mapping_popup");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Load a saved mapping from a file.");

      if (ImGui::BeginPopup("load_mapping_popup")) {
        if (ImGui::BeginListBox("Mappings", ImVec2(-1, 150))) {
          std::string dir_path = SDL_GetBasePath();
          dir_path.append("mapping/");
          std::filesystem::create_directory(dir_path);
          for (const auto &entry :
               std::filesystem::directory_iterator(dir_path)) {
            std::string mapping_path = entry.path().string();
            std::string mapping_name =
                std::filesystem::path(mapping_path).filename().string();
            if (ImGui::Selectable(mapping_name.c_str(),
                                  g_loaded_mapping_name == mapping_name)) {
              for (int i = 0; i < 35; ++i)
                current_window->mapping[i] = "";
              open_ifstream(mapping_path);
              std::vector<std::string> lines;
              read_file(&lines);
              close_ifstream();
              if (!lines.empty()) {
                std::string mapStr = lines[0];
                std::stringstream ss(mapStr);
                std::string item;
                while (std::getline(ss, item, ',')) {
                  if (item.empty())
                    continue;
                  std::vector<std::string> kv = get_binding(item);
                  if (kv.size() == 2) {
                    // kv[0] is the part name (mesh name), kv[1] is binding
                    for (int i = 0; i < 35; ++i) {
                      if (kv[0] == mesh_names[i]) {
                        current_window->mapping[i] = kv[1];
                        break;
                      }
                    }
                  }
                }
                g_loaded_mapping_name = mapping_name;
                spdlog::info("Loaded mapping from {}", mapping_name);
              }
              ImGui::CloseCurrentPopup();
            }
          }
          ImGui::EndListBox();
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Select a mapping file to load.");
        ImGui::EndPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Reset to Default")) {
        for (int i = 0; i < 35; ++i)
          current_window->mapping[i] = "";
        g_loaded_mapping_name = "";
        spdlog::info("Mapping reset to default.");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clear all mappings.");
    } // end Mapping

  end_mapping:; // empty statement (label needs a statement)

    // ============================================================
    // HELP
    // ============================================================
    if (ImGui::CollapsingHeader("Help")) {
      ImGui::Text("3D Controller Overlay version 1.12");
      ImGui::NewLine();
      ImGui::Text("https://github.com/larfingshnew/3d-controller-overlay");
      if (ImGui::Button("Open Github Page")) {
        OsOpenInShell("https://github.com/larfingshnew/3d-controller-overlay");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open the project repository in your browser.");
      ImGui::NewLine();
      ImGui::Text("https://discord.gg/aKwHHvCMnS");
      if (ImGui::Button("Join Discord Server")) {
        OsOpenInShell("https://discord.gg/aKwHHvCMnS");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open the Discord invite link.");
    }
  } // end big if (tabs.size() > 0 && new_controller_window == false)

  ImGui::End();
  ImGui::PopStyleVar();

  texture_dialog.Display();
  model_dialog.Display();

  if (texture_dialog.HasSelected()) {
    controller_window *ctrl = getControllerWindow(tabs[selected_tab].ID);
    if (!ctrl) {
      spdlog::error("No controller window for texture import.");
      texture_dialog.ClearSelected();
    } else if (ctrl->model.meshes.empty()) {
      spdlog::error("Cannot add texture: model has no meshes.");
      texture_dialog.ClearSelected();
    } else if (texture_mesh >= ctrl->model.meshes.size()) {
      spdlog::error("Texture mesh index out of range.");
      texture_dialog.ClearSelected();
    } else {
      std::cout << "Selected filename : "
                << texture_dialog.GetSelected().string() << std::endl;
      glfwMakeContextCurrent(ctrl->glfw_window);
      Texture t;
      loadTexture(t.id, texture_dialog.GetSelected().string());
      t.path = texture_dialog.GetSelected().string();
      t.name =
          std::to_string(ctrl->model.meshes[texture_mesh].textures.size() + 1) +
          ": " + t.path;
      ctrl->model.meshes[texture_mesh].textures.push_back(t);
      glfwMakeContextCurrent(glfw_settings_window);
      texture_dialog.ClearSelected();
    }
  }

  if (model_dialog.HasSelected()) {
    controller_window *ctrl_win = getControllerWindow(tabs[selected_tab].ID);
    if (!ctrl_win) {
      spdlog::error("No valid controller window for model import.");
      model_dialog.ClearSelected();
    } else if (selected_mesh < 0 ||
               selected_mesh >= (int)ctrl_win->model.meshes.size()) {
      spdlog::error("Invalid mesh index: {}", selected_mesh);
      model_dialog.ClearSelected();
    } else {
      std::cout << "Selected filename : " << model_dialog.GetSelected().string()
                << std::endl;
      // 1. Copy the OBJ file into the model folder with the correct name
      const auto copy_options =
          std::filesystem::copy_options::overwrite_existing;
      std::filesystem::path from_path = model_dialog.GetSelected();
      std::filesystem::path to_path = get_models_root();
      to_path.append(ctrl_win->model.path);
      to_path.append(mesh_filenames[selected_mesh]);
      std::filesystem::copy(from_path, to_path, copy_options);

      // 2. Load the OBJ into the selected mesh slot
      glfwMakeContextCurrent(ctrl_win->glfw_window);
      loadMesh(ctrl_win->model.meshes[selected_mesh], to_path.string());
      glfwMakeContextCurrent(glfw_settings_window);

      // 3. Save the updated model (including the newly loaded geometry)
      writeJson(ctrl_win->model, ctrl_win->model.path + "/info.json");

      // 4. (Optional) Reload the model to ensure everything is consistent
      //    You can uncomment this if you want to refresh all meshes, but it's
      //    not needed
      // glfwMakeContextCurrent(ctrl_win->glfw_window);
      // loadModel(ctrl_win->model, ctrl_win->model.path);
      // glfwMakeContextCurrent(glfw_settings_window);
    }
    model_dialog.ClearSelected();
  }
  // --- Import Model Dialog ---
  import_model_dialog.Display();
  if (import_model_dialog.HasSelected()) {
    std::string filepath = import_model_dialog.GetSelected().string();
    spdlog::info("Importing model: {}", filepath);

    Model temp_model;
    importModelFile(temp_model, filepath);
    if (temp_model.imported_meshes.empty()) {
      spdlog::error("Failed to import model: no meshes found.");
      import_model_dialog.ClearSelected();
    } else {
      std::string preview_title =
          "Import Preview - " +
          std::filesystem::path(filepath).filename().string();
      createControllerWindow(preview_title, "dummy");
      controller_window *preview_window = getLastWindow();
      preview_window->model = temp_model;
      convertImportedToMeshes(preview_window->model);
      preview_window->is_import_preview = true;
      preview_window->import_preview.is_open = true;
      preview_window->import_preview.imported_model = temp_model;

      preview_window->import_preview.assignments.clear();
      for (auto &mesh : temp_model.imported_meshes) {
        ImportAssignment assign;
        assign.mesh_name = mesh.name;
        assign.assigned_part = -1;
        assign.touch_width = 1.0f;
        assign.touch_height = 1.0f;
        preview_window->import_preview.assignments.push_back(assign);
      }
      preview_window->import_preview.selected_mesh_index = -1;
      preview_window->import_preview.save_name = "NewModel";

      import_model_dialog.ClearSelected();
    }
  }

  ImGui::Render();
  glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(glfw_settings_window);

  if (new_controller_window) {
    std::string first_model = get_first_model();
    if (!first_model.empty()) {
      createControllerWindow(new_tab_title, first_model);
      tabs.back().ID = tabs_made;
      getLastWindow()->ID = tabs_made;
    } else {
      spdlog::warn("No model folders found in '{}'. Please create a "
                   "model folder.",
                   get_models_root());
      tabs.pop_back();
      tabs_made--;
    }
  }
} // end drawSettingsWindow()

// =========================================================================
//  HELPER FUNCTION DEFINITIONS
// =========================================================================

void DrawImportPreviewControls(controller_window &w) {
  ImGui::Text("Imported Model: %zu meshes",
              w.import_preview.imported_model.imported_meshes.size());

  // Sort assignments alphabetically by mesh name
  std::sort(w.import_preview.assignments.begin(),
            w.import_preview.assignments.end(),
            [](const ImportAssignment &a, const ImportAssignment &b) {
              return a.mesh_name < b.mesh_name;
            });

  // Update mesh colors and touch dimensions in real‑time
  for (auto &assign : w.import_preview.assignments) {
    if (assign.assigned_part >= 0 && assign.assigned_part < 35) {
      Mesh &mesh = w.model.meshes[assign.assigned_part];
      bool isTouchPart =
          (assign.assigned_part == 29 || assign.assigned_part == 30 ||
           assign.assigned_part == 31 || assign.assigned_part == 32 ||
           assign.assigned_part == 33 || assign.assigned_part == 34);
      if (isTouchPart) {
        mesh.material.color[0] = 1.0f;
        mesh.material.color[1] = 0.2f;
        mesh.material.color[2] = 0.2f;
        mesh.highlight_value = 0.3f;
        // Apply touch dimensions to the mesh
        mesh.touch_width = assign.touch_width;
        mesh.touch_height = assign.touch_height;
      } else {
        // Restore default color for non‑touch parts
        mesh.material.color[0] = 0.8f;
        mesh.material.color[1] = 0.8f;
        mesh.material.color[2] = 0.8f;
        mesh.highlight_value = 0.0f;
      }
    }
  }

  if (ImGui::BeginTable("ImportAssignTable", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("Mesh Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Controller Part",
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Max Angle", ImGuiTableColumnFlags_WidthFixed,
                            100.0f);
    ImGui::TableSetupColumn("Parent Part", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Touch W", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Touch H", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < w.import_preview.assignments.size(); ++i) {
      auto &assign = w.import_preview.assignments[i];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%s", assign.mesh_name.c_str());
      ImGui::TableSetColumnIndex(1);

      // ---- Part assignment combo ----
      int current_part =
          assign.assigned_part + 1; // +1 because "Unassigned" is at index 0
      const char *part_names[36];
      part_names[0] = "Unassigned";
      for (int j = 0; j < 35; ++j)
        part_names[j + 1] = mesh_names[j].c_str();

      ImGui::PushID(i);
      if (ImGui::Combo("##part", &current_part, part_names, 36)) {
        // Update assignment
        assign.assigned_part = current_part - 1;
        assign.max_angle = 0.0f;
        assign.parent_part = -1;
        assign.touch_width = 1.0f;
        assign.touch_height = 1.0f;
        w.import_preview.selected_mesh_index = -1;
        // Reset stick/trigger max on the mesh
        if (assign.assigned_part >= 0 &&
            assign.assigned_part < (int)w.model.meshes.size()) {
          w.model.meshes[assign.assigned_part].stick_max = 0.0f;
          w.model.meshes[assign.assigned_part].trigger_max = 0.0f;
        }
        spdlog::info("Assigned mesh '{}' to part {} ({})", assign.mesh_name,
                     assign.assigned_part,
                     assign.assigned_part >= 0
                         ? mesh_names[assign.assigned_part]
                         : "none");
      }
      ImGui::PopID();

      // ---- Max Angle Column ----
      ImGui::TableSetColumnIndex(2);
      bool isStick = (assign.assigned_part == 5 || assign.assigned_part == 6);
      bool isTrigger = (assign.assigned_part == 3 || assign.assigned_part == 4);
      if (isStick || isTrigger) {
        float max_angle_deg = glm::degrees(assign.max_angle);
        float min_deg = 0.0f, max_deg = 45.0f;
        if (isTrigger)
          max_deg = 90.0f;
        ImGui::PushID(i + 1000);
        if (ImGui::SliderFloat("##maxangle", &max_angle_deg, min_deg, max_deg,
                               "%.1f°")) {
          assign.max_angle = glm::radians(max_angle_deg);
          if (assign.assigned_part >= 0 &&
              assign.assigned_part < (int)w.model.meshes.size()) {
            if (isStick)
              w.model.meshes[assign.assigned_part].stick_max = assign.max_angle;
            else if (isTrigger)
              w.model.meshes[assign.assigned_part].trigger_max =
                  assign.max_angle;
          }
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Maximum rotation/pull angle in degrees.");
        }
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      // ---- Parent Part Column ----
      ImGui::TableSetColumnIndex(3);
      if (assign.assigned_part >= 0 && assign.assigned_part < 35) {
        int current_parent = assign.parent_part + 1;
        const char *parent_names[36];
        parent_names[0] = "None";
        for (int j = 0; j < 35; ++j)
          parent_names[j + 1] = mesh_names[j].c_str();
        ImGui::PushID(i + 2000);
        if (ImGui::Combo("##parent", &current_parent, parent_names, 36)) {
          assign.parent_part = current_parent - 1;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Attach this mesh to a parent part (e.g., stick).");
        }
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      // ---- Touch Width Column ----
      ImGui::TableSetColumnIndex(4);
      bool isTouchPart =
          (assign.assigned_part == 29 || assign.assigned_part == 30 ||
           assign.assigned_part == 31 || assign.assigned_part == 32 ||
           assign.assigned_part == 33 || assign.assigned_part == 34);
      if (isTouchPart) {
        ImGui::PushID(i + 3000);
        if (ImGui::SliderFloat("##tw", &assign.touch_width, 0.01f, 5.0f,
                               "%.2f")) {
          if (assign.touch_width < 0.01f)
            assign.touch_width = 0.01f;
          if (assign.touch_width > 5.0f)
            assign.touch_width = 5.0f;
          // Immediately update the mesh
          if (assign.assigned_part >= 0 &&
              assign.assigned_part < (int)w.model.meshes.size()) {
            w.model.meshes[assign.assigned_part].touch_width =
                assign.touch_width;
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Touch area width (world units). Adjust to match "
                            "your physical controller.");
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      // ---- Touch Height Column ----
      ImGui::TableSetColumnIndex(5);
      if (isTouchPart) {
        ImGui::PushID(i + 4000);
        if (ImGui::SliderFloat("##th", &assign.touch_height, 0.01f, 5.0f,
                               "%.2f")) {
          if (assign.touch_height < 0.01f)
            assign.touch_height = 0.01f;
          if (assign.touch_height > 5.0f)
            assign.touch_height = 5.0f;
          // Immediately update the mesh
          if (assign.assigned_part >= 0 &&
              assign.assigned_part < (int)w.model.meshes.size()) {
            w.model.meshes[assign.assigned_part].touch_height =
                assign.touch_height;
          }
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Touch area height (world units). Adjust to match "
                            "your physical controller.");
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("N/A");
      }

      // ---- Actions Column ----
      ImGui::TableSetColumnIndex(6);
      ImGui::PushID(i);
      if (ImGui::Button("Highlight")) {
        w.import_preview.selected_mesh_index = i;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Highlight this mesh in the 3D view.");
      // Add a note about unassigned meshes
      if (assign.assigned_part == -1) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Unassigned meshes are saved as separate OBJ files "
                            "in the model folder.");
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  char save_name[64] = {};
  strncpy(save_name, w.import_preview.save_name.c_str(), 63);
  if (ImGui::InputText("Model Name", save_name, 64)) {
    w.import_preview.save_name = save_name;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Name for the new model folder.");

  if (ImGui::Button("Save Model")) {
    SaveImportedModel(w);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Save the imported model with current assignments.");
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    w.import_preview.is_open = false;
    glfwSetWindowShouldClose(w.glfw_window, true);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Close the preview without saving.");
}

// Forward declaration of writeJson (defined in model.cpp)
void writeJson(Model &m, const std::string &path);

// Helper to build a Mesh from an ImportedMesh (no file I/O)
void buildMeshFromImported(Mesh &mesh, const ImportedMesh &imported) {
  // Clear any old GL resources
  if (mesh.vao)
    glDeleteVertexArrays(1, &mesh.vao);
  if (mesh.vbo)
    glDeleteBuffers(1, &mesh.vbo);
  if (mesh.ebo)
    glDeleteBuffers(1, &mesh.ebo);

  // Build vertex data array (8 floats per vertex: pos, normal, texcoord)
  std::vector<float> vertex_data;
  vertex_data.reserve(imported.positions.size() * 8);
  for (size_t i = 0; i < imported.positions.size(); ++i) {
    vertex_data.push_back(imported.positions[i].x);
    vertex_data.push_back(imported.positions[i].y);
    vertex_data.push_back(imported.positions[i].z);
    vertex_data.push_back(imported.normals[i].x);
    vertex_data.push_back(imported.normals[i].y);
    vertex_data.push_back(imported.normals[i].z);
    vertex_data.push_back(imported.texcoords[i].x);
    vertex_data.push_back(imported.texcoords[i].y);
  }

  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glGenBuffers(1, &mesh.ebo);

  glBindVertexArray(mesh.vao);

  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float),
               vertex_data.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               imported.indices.size() * sizeof(unsigned int),
               imported.indices.data(), GL_STATIC_DRAW);

  mesh.elements = imported.indices.size();

  // Compute bounding box
  if (!imported.positions.empty()) {
    mesh.hasBBox = true;
    mesh.bboxMin = imported.positions[0];
    mesh.bboxMax = imported.positions[0];
    for (const auto &v : imported.positions) {
      mesh.bboxMin.x = std::min(mesh.bboxMin.x, v.x);
      mesh.bboxMin.y = std::min(mesh.bboxMin.y, v.y);
      mesh.bboxMin.z = std::min(mesh.bboxMin.z, v.z);
      mesh.bboxMax.x = std::max(mesh.bboxMax.x, v.x);
      mesh.bboxMax.y = std::max(mesh.bboxMax.y, v.y);
      mesh.bboxMax.z = std::max(mesh.bboxMax.z, v.z);
    }
  } else {
    mesh.hasBBox = false;
  }

  glBindVertexArray(0);
}

void SaveImportedModel(controller_window &w) {
  std::string model_name = w.import_preview.save_name;
  if (model_name.empty()) {
    spdlog::error("Model name cannot be empty.");
    return;
  }

  std::string new_model_path = get_models_root() + "/" + model_name;
  std::filesystem::create_directories(new_model_path);
  spdlog::info("Saving imported model to: {}", new_model_path);

  // Clear any existing meshes
  w.model.meshes.clear();

  // For each assignment, create a Mesh and upload geometry
  for (auto &assign : w.import_preview.assignments) {
    // Find the imported mesh data
    auto it =
        std::find_if(w.import_preview.imported_model.imported_meshes.begin(),
                     w.import_preview.imported_model.imported_meshes.end(),
                     [&](const ImportedMesh &mesh) {
                       return mesh.name == assign.mesh_name;
                     });
    if (it == w.import_preview.imported_model.imported_meshes.end()) {
      spdlog::warn("Imported mesh '{}' not found, skipping.", assign.mesh_name);
      continue;
    }

    // Generate a filename (safe)
    std::string safe_name = assign.mesh_name;
    for (char &c : safe_name) {
      if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' ||
          c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        c = '_';
    }
    std::string obj_filename = safe_name + ".obj";
    std::string full_path = new_model_path + "/" + obj_filename;

    // Write the OBJ file
    spdlog::debug("Writing OBJ: {}", full_path);
    writeOBJ(full_path, *it);

    // Check if the file was written
    if (!std::filesystem::exists(full_path)) {
      spdlog::error("Failed to write OBJ file: {}", full_path);
      // Fallback: build mesh directly from imported data
      Mesh mesh;
      buildMeshFromImported(mesh, *it); // see below
      mesh.filename = obj_filename;
      mesh.name = assign.mesh_name;
      mesh.assignedPart = assign.assigned_part;
      mesh.parentIndex = assign.parent_part;
      mesh.stick_max = assign.max_angle;
      mesh.trigger_max = assign.max_angle;
      mesh.touch_width = assign.touch_width;
      mesh.touch_height = assign.touch_height;
      w.model.meshes.push_back(std::move(mesh));
      spdlog::info("Added mesh '{}' directly from imported data (no OBJ).",
                   assign.mesh_name);
      continue;
    }

    // Try to load the OBJ via the standard loader
    Mesh mesh;
    loadMesh(mesh, full_path);
    if (mesh.elements == 0) {
      spdlog::error("Failed to load OBJ: {}, falling back to direct import.",
                    full_path);
      // Fallback: build mesh directly from imported data
      buildMeshFromImported(mesh, *it);
    }

    // Set properties from assignment
    mesh.filename = obj_filename;
    mesh.name = assign.mesh_name;
    mesh.assignedPart = assign.assigned_part;
    mesh.parentIndex = assign.parent_part;
    mesh.stick_max = assign.max_angle;
    mesh.trigger_max = assign.max_angle;
    mesh.touch_width = assign.touch_width;
    mesh.touch_height = assign.touch_height;

    w.model.meshes.push_back(std::move(mesh));
    spdlog::info("Added mesh '{}' with {} vertices.", assign.mesh_name,
                 mesh.elements);
  }

  // Write info.json
  w.model.path = new_model_path;
  writeJson(w.model, new_model_path + "/info.json");

  spdlog::info("New model saved successfully: {} meshes.",
               w.model.meshes.size());

  // Close the preview window
  w.is_import_preview = false;
  w.import_preview.is_open = false;
  glfwSetWindowShouldClose(w.glfw_window, true);
}

void writeOBJ(const std::string &path, const ImportedMesh &mesh) {
  std::ofstream f(path);
  if (!f.is_open()) {
    spdlog::error("Failed to write OBJ: {}", path);
    return;
  }
  for (auto &p : mesh.positions) {
    f << "v " << p.x << " " << p.y << " " << p.z << "\n";
  }
  for (auto &n : mesh.normals) {
    f << "vn " << n.x << " " << n.y << " " << n.z << "\n";
  }
  for (auto &t : mesh.texcoords) {
    f << "vt " << t.x << " " << t.y << "\n";
  }
  f << "s off\n";
  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    int i0 = mesh.indices[i] + 1;
    int i1 = mesh.indices[i + 1] + 1;
    int i2 = mesh.indices[i + 2] + 1;
    f << "f " << i0 << "/" << i0 << "/" << i0 << " " << i1 << "/" << i1 << "/"
      << i1 << " " << i2 << "/" << i2 << "/" << i2 << "\n";
  }
}

void saveTabs() {
  clear_directory("settings");

  // ---- Temporarily remove highlights from all windows ----
  std::vector<controller_window *> windows_with_highlight;
  for (auto &w : windows) {
    if (w.highlight_enabled && !w.original_colors.empty()) {
      windows_with_highlight.push_back(&w);
      // Restore original colours
      for (auto &pair : w.original_colors) {
        int idx = pair.first;
        Mesh &mesh = w.model.meshes[idx];
        mesh.material.color[0] = pair.second[0];
        mesh.material.color[1] = pair.second[1];
        mesh.material.color[2] = pair.second[2];
        mesh.highlight_value = 0.0f;
      }
    }
  }

  // ---- Save all tabs ----
  for (window_tab t : tabs) {
    controller_window *w = getControllerWindow(t.ID);
    if (!w)
      continue;
    writeJson(w->model, w->model.path + "/info.json");

    // Write settings file (existing code, unchanged)
    std::string path = "settings/";
    path.append(t.title);
    open_ofstream(path);
    // ... (keep all the existing write_* calls exactly as they were) ...
    // I'll include them below for completeness, but you can keep your
    // existing ones.
    write_string(std::string("model path"), w->model.path);
    write_string(std::string("title"), t.title);
    write_int(std::string("always on top"), w->always_on_top);
    write_int(std::string("borderless"), w->borderless);
    write_int(std::string("drag to move"), w->drag_to_move);
    write_int(std::string("scroll to resize"), w->scroll_to_resize);
    write_int(std::string("show grid"), w->grid);
    write_int(std::string("wireframe"), w->wireframe);
    int ww = 640, hh = 480;
    if (!glfwGetWindowAttrib(w->glfw_window, GLFW_ICONIFIED)) {
      glfwGetWindowSize(w->glfw_window, &ww, &hh);
    }
    write_int(std::string("width"), ww);
    write_int(std::string("height"), hh);
    int x = 100, y = 100;
    if (!glfwGetWindowAttrib(w->glfw_window, GLFW_ICONIFIED)) {
      glfwGetWindowPos(w->glfw_window, &x, &y);
    }
    write_int(std::string("x pos"), x);
    write_int(std::string("y pos"), y);
    write_int(std::string("swap interval"), w->swap_interval);
    write_int(std::string("frame cap"), w->frame_cap);
    write_float(std::string("bg red"), w->bg_color[0]);
    write_float(std::string("bg green"), w->bg_color[1]);
    write_float(std::string("bg blue"), w->bg_color[2]);
    write_float(std::string("bg alpha"), w->bg_color[3]);
    write_int(std::string("freelook"), w->freelook);
    write_float(std::string("camera distance"), w->camera_distance);
    write_float(std::string("camera yaw"), w->camera_yaw);
    write_float(std::string("camera pitch"), w->camera_pitch);
    write_float(std::string("camera roll"), w->camera_roll);
    write_int(std::string("move speed"), w->move_speed);
    write_int(std::string("turn speed"), w->turn_speed);
    write_float(std::string("freelook yaw"), w->freelook_yaw);
    write_float(std::string("freelook pitch"), w->freelook_pitch);
    write_3_floats(std::string("freelook position"), w->freelook_position.x,
                   w->freelook_position.y, w->freelook_position.z);
    write_int(std::string("popup bumbers"), w->model.popup_bumpers);
    write_int(std::string("popup triggers"), w->model.popup_triggers);
    write_int(std::string("popup paddles"), w->model.popup_paddles);
    write_int(std::string("left stick highlight deadzone"),
              (w->model.meshes.size() > 7)
                  ? w->model.meshes[7].ring_highlight_deadzone
                  : 0);
    write_int(std::string("right stick highlight deadzone"),
              (w->model.meshes.size() > 8)
                  ? w->model.meshes[8].ring_highlight_deadzone
                  : 0);
    write_float(std::string("highlight red"), w->highlight_color[0]);
    write_float(std::string("highlight green"), w->highlight_color[1]);
    write_float(std::string("highlight blue"), w->highlight_color[2]);
    write_float(std::string("global press red"), w->global_press_color[0]);
    write_float(std::string("global press green"), w->global_press_color[1]);
    write_float(std::string("global press blue"), w->global_press_color[2]);
    write_float(std::string("touch area offset x"), w->touch_area_offset[0]);
    write_float(std::string("touch area offset y"), w->touch_area_offset[1]);
    write_float(std::string("touch area offset z"), w->touch_area_offset[2]);
    write_int(std::string("model meshes"), w->model.meshes.size());
    write_line(std::string("materials"));
    for (int i = 0; i < (int)w->model.meshes.size(); ++i) {
      write_line(std::to_string(w->model.meshes[i].material.ambient));
      write_line(std::to_string(w->model.meshes[i].material.diffuse));
      write_line(std::to_string(w->model.meshes[i].material.specular));
      write_line(std::to_string(w->model.meshes[i].material.shininess));
      write_line(std::to_string(w->model.meshes[i].material.color[0]));
      write_line(std::to_string(w->model.meshes[i].material.color[1]));
      write_line(std::to_string(w->model.meshes[i].material.color[2]));
      write_line(std::to_string(w->model.meshes[i].material.highlight[0]));
      write_line(std::to_string(w->model.meshes[i].material.highlight[1]));
      write_line(std::to_string(w->model.meshes[i].material.highlight[2]));
    }
    write_line(std::string("textures"));
    for (int i = 0; i < (int)w->model.meshes.size(); ++i) {
      write_line(std::to_string(w->model.meshes[i].textures.size()));
    }
    for (int i = 0; i < (int)w->model.meshes.size(); ++i) {
      for (int j = 0; j < (int)w->model.meshes[i].textures.size(); ++j) {
        write_line(w->model.meshes[i].textures[j].path);
        write_line(std::to_string(w->model.meshes[i].textures[j].type));
        write_line(std::to_string(w->model.meshes[i].textures[j].wrapX));
        write_line(std::to_string(w->model.meshes[i].textures[j].wrapY));
        write_line(std::to_string(w->model.meshes[i].textures[j].offsetX));
        write_line(std::to_string(w->model.meshes[i].textures[j].offsetY));
        write_line(std::to_string(w->model.meshes[i].textures[j].scaleX));
        write_line(std::to_string(w->model.meshes[i].textures[j].scaleY));
        write_line(std::to_string(w->model.meshes[i].textures[j].rotation));
        write_line(std::to_string(w->model.meshes[i].textures[j].border[0]));
        write_line(std::to_string(w->model.meshes[i].textures[j].border[1]));
        write_line(std::to_string(w->model.meshes[i].textures[j].border[2]));
        write_line(std::to_string(w->model.meshes[i].textures[j].border[3]));
      }
    }
    write_int(std::string("gyro debug logging"), w->gyro_debug_logging);
    write_int(std::string("gyro enabled"), w->gyro_enabled);
    write_int(std::string("reset gyro button 1"), w->reset_gyro_button1);
    write_int(std::string("reset gyro button 2"), w->reset_gyro_button2);
    write_int(std::string("gyro correction"), w->gyro_correction);
    write_float(std::string("gyro sensitivity"), w->gyro_sensitivity);
    write_int(std::string("direct lights"), w->direct_lights.size());
    for (int i = 0; i < (int)w->direct_lights.size(); ++i) {
      write_line(w->direct_lights[i].name);
      write_line(std::to_string(w->direct_lights[i].direction[0]));
      write_line(std::to_string(w->direct_lights[i].direction[1]));
      write_line(std::to_string(w->direct_lights[i].direction[2]));
      write_line(std::to_string(w->direct_lights[i].color[0]));
      write_line(std::to_string(w->direct_lights[i].color[1]));
      write_line(std::to_string(w->direct_lights[i].color[2]));
    }
    write_int(std::string("point lights"), w->point_lights.size());
    for (int i = 0; i < (int)w->point_lights.size(); ++i) {
      write_line(w->point_lights[i].name);
      write_line(std::to_string(w->point_lights[i].hide));
      write_line(std::to_string(w->point_lights[i].position[0]));
      write_line(std::to_string(w->point_lights[i].position[1]));
      write_line(std::to_string(w->point_lights[i].position[2]));
      write_line(std::to_string(w->point_lights[i].intensity));
      write_line(std::to_string(w->point_lights[i].color[0]));
      write_line(std::to_string(w->point_lights[i].color[1]));
      write_line(std::to_string(w->point_lights[i].color[2]));
    }
    write_int(std::string("spot lights"), w->spot_lights.size());
    for (int i = 0; i < (int)w->spot_lights.size(); ++i) {
      write_line(w->spot_lights[i].name);
      write_line(std::to_string(w->spot_lights[i].hide));
      write_line(std::to_string(w->spot_lights[i].position[0]));
      write_line(std::to_string(w->spot_lights[i].position[1]));
      write_line(std::to_string(w->spot_lights[i].position[2]));
      write_line(std::to_string(w->spot_lights[i].intensity));
      write_line(std::to_string(w->spot_lights[i].color[0]));
      write_line(std::to_string(w->spot_lights[i].color[1]));
      write_line(std::to_string(w->spot_lights[i].color[2]));
      write_line(std::to_string(w->spot_lights[i].yaw));
      write_line(std::to_string(w->spot_lights[i].pitch));
      write_line(std::to_string(w->spot_lights[i].cutoff));
      write_line(std::to_string(w->spot_lights[i].outer_cutoff));
    }
    close_ofstream();
  }

  // ---- Re‑apply highlights ----
  for (controller_window *w : windows_with_highlight) {
    for (auto &pair : w->original_colors) {
      int idx = pair.first;
      Mesh &mesh = w->model.meshes[idx];
      mesh.material.color[0] = w->highlight_color[0];
      mesh.material.color[1] = w->highlight_color[1];
      mesh.material.color[2] = w->highlight_color[2];
      mesh.highlight_value = 0.2f;
    }
  }
}

void loadTabs() {
  std::vector<std::filesystem::path> files;
  get_directory_contents(&files, "settings");
  for (std::filesystem::path path : files) {
    std::string new_tab_title = "Controller ";
    window_tab new_tab;
    tabs_made++;
    new_tab_title.append(std::to_string(tabs_made));
    new_tab.title = new_tab_title;
    tabs.push_back(new_tab);
    tabs.back().ID = tabs_made;

    std::vector<std::string> lines;
    open_ifstream(path.c_str());
    read_file(&lines);
    close_ifstream();
    unsigned line_index = 0;

    for (std::string line : lines) {
      if (line == "model path") {
        createControllerWindow(new_tab_title, lines[line_index + 1]);
        getLastWindow()->ID = tabs_made;
        getControllerWindow(tabs.back().ID)->model_name =
            get_top_folder(getControllerWindow(tabs.back().ID)->model.path);
      }
      if (line == "title") {
        tabs.back().title = lines[line_index + 1];
        glfwSetWindowTitle(getControllerWindow(tabs.back().ID)->glfw_window,
                           lines[line_index + 1].c_str());
      }
      if (line == "always on top") {
        getControllerWindow(tabs.back().ID)->always_on_top =
            std::stoi(lines[line_index + 1]);
        glfwSetWindowAttrib(getControllerWindow(tabs.back().ID)->glfw_window,
                            GLFW_FLOATING, std::stoi(lines[line_index + 1]));
      }
      if (line == "borderless") {
        getControllerWindow(tabs.back().ID)->borderless =
            std::stoi(lines[line_index + 1]);
        glfwSetWindowAttrib(getControllerWindow(tabs.back().ID)->glfw_window,
                            GLFW_DECORATED, !std::stoi(lines[line_index + 1]));
      }
      if (line == "drag to move")
        getControllerWindow(tabs.back().ID)->drag_to_move =
            std::stoi(lines[line_index + 1]);
      if (line == "scroll to resize")
        getControllerWindow(tabs.back().ID)->scroll_to_resize =
            std::stoi(lines[line_index + 1]);
      if (line == "show grid")
        getControllerWindow(tabs.back().ID)->grid =
            std::stoi(lines[line_index + 1]);
      if (line == "wireframe")
        getControllerWindow(tabs.back().ID)->wireframe =
            std::stoi(lines[line_index + 1]);
      if (line == "width")
        glfwSetWindowSize(getControllerWindow(tabs.back().ID)->glfw_window,
                          std::stoi(lines[line_index + 1]),
                          std::stoi(lines[line_index + 3]));
      if (line == "x pos")
        glfwSetWindowPos(getControllerWindow(tabs.back().ID)->glfw_window,
                         std::stoi(lines[line_index + 1]),
                         std::stoi(lines[line_index + 3]));
      if (line == "swap interval")
        getControllerWindow(tabs.back().ID)->swap_interval =
            std::stoi(lines[line_index + 1]);
      if (line == "frame cap")
        getControllerWindow(tabs.back().ID)->frame_cap =
            std::stoi(lines[line_index + 1]);
      if (line == "bg red")
        getControllerWindow(tabs.back().ID)->bg_color[0] =
            std::stof(lines[line_index + 1]);
      if (line == "bg green")
        getControllerWindow(tabs.back().ID)->bg_color[1] =
            std::stof(lines[line_index + 1]);
      if (line == "bg blue")
        getControllerWindow(tabs.back().ID)->bg_color[2] =
            std::stof(lines[line_index + 1]);
      if (line == "bg alpha")
        getControllerWindow(tabs.back().ID)->bg_color[3] =
            std::stof(lines[line_index + 1]);
      if (line == "freelook")
        getControllerWindow(tabs.back().ID)->freelook =
            std::stoi(lines[line_index + 1]);
      if (line == "camera distance")
        getControllerWindow(tabs.back().ID)->camera_distance =
            std::stof(lines[line_index + 1]);
      if (line == "camera yaw")
        getControllerWindow(tabs.back().ID)->camera_yaw =
            std::stof(lines[line_index + 1]);
      if (line == "camera pitch")
        getControllerWindow(tabs.back().ID)->camera_pitch =
            std::stof(lines[line_index + 1]);
      if (line == "camera roll")
        getControllerWindow(tabs.back().ID)->camera_roll =
            std::stof(lines[line_index + 1]);
      if (line == "move speed")
        getControllerWindow(tabs.back().ID)->move_speed =
            std::stoi(lines[line_index + 1]);
      if (line == "turn speed")
        getControllerWindow(tabs.back().ID)->turn_speed =
            std::stoi(lines[line_index + 1]);
      if (line == "freelook yaw")
        getControllerWindow(tabs.back().ID)->freelook_yaw =
            std::stof(lines[line_index + 1]);
      if (line == "freelook pitch")
        getControllerWindow(tabs.back().ID)->freelook_pitch =
            std::stof(lines[line_index + 1]);
      if (line == "freelook position") {
        getControllerWindow(tabs.back().ID)->freelook_position.x =
            std::stof(lines[line_index + 1]);
        getControllerWindow(tabs.back().ID)->freelook_position.y =
            std::stof(lines[line_index + 2]);
        getControllerWindow(tabs.back().ID)->freelook_position.z =
            std::stof(lines[line_index + 3]);
      }
      if (line == "popup bumbers")
        getControllerWindow(tabs.back().ID)->model.popup_bumpers =
            std::stoi(lines[line_index + 1]);
      if (line == "popup triggers")
        getControllerWindow(tabs.back().ID)->model.popup_triggers =
            std::stoi(lines[line_index + 1]);
      if (line == "popup paddles")
        getControllerWindow(tabs.back().ID)->model.popup_paddles =
            std::stoi(lines[line_index + 1]);
      if (line == "left stick highlight deadzone") {
        controller_window *cw = getControllerWindow(tabs.back().ID);
        if (cw && cw->model.meshes.size() > 7)
          cw->model.meshes[7].ring_highlight_deadzone =
              std::stoi(lines[line_index + 1]);
      }
      if (line == "right stick highlight deadzone") {
        controller_window *cw = getControllerWindow(tabs.back().ID);
        if (cw && cw->model.meshes.size() > 8)
          cw->model.meshes[8].ring_highlight_deadzone =
              std::stoi(lines[line_index + 1]);
      }
      if (line == "highlight red")
        getControllerWindow(tabs.back().ID)->highlight_color[0] =
            std::stof(lines[line_index + 1]);
      if (line == "highlight green")
        getControllerWindow(tabs.back().ID)->highlight_color[1] =
            std::stof(lines[line_index + 1]);
      if (line == "highlight blue")
        getControllerWindow(tabs.back().ID)->highlight_color[2] =
            std::stof(lines[line_index + 1]);
      {
        controller_window *curWin = getControllerWindow(tabs.back().ID);
        if (curWin) {
          for (int i = 3; i < (int)curWin->model.meshes.size(); i++) {
            if (i != 5 && i != 6) {
              curWin->model.meshes[i].material.highlight[0] =
                  curWin->highlight_color[0];
              curWin->model.meshes[i].material.highlight[1] =
                  curWin->highlight_color[1];
              curWin->model.meshes[i].material.highlight[2] =
                  curWin->highlight_color[2];
            }
          }
        }
      }
      if (line == "global press red")
        getControllerWindow(tabs.back().ID)->global_press_color[0] =
            std::stof(lines[line_index + 1]);
      if (line == "global press green")
        getControllerWindow(tabs.back().ID)->global_press_color[1] =
            std::stof(lines[line_index + 1]);
      if (line == "global press blue")
        getControllerWindow(tabs.back().ID)->global_press_color[2] =
            std::stof(lines[line_index + 1]);
      if (line == "materials") {
        for (int i = 0;
             i < (int)getControllerWindow(tabs.back().ID)->model.meshes.size();
             i++) {
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.ambient = std::stof(lines[line_index + 1 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.diffuse = std::stof(lines[line_index + 2 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.specular = std::stof(lines[line_index + 3 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.shininess = std::stof(lines[line_index + 4 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.color[0] = std::stof(lines[line_index + 5 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.color[1] = std::stof(lines[line_index + 6 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.color[2] = std::stof(lines[line_index + 7 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.highlight[0] =
              std::stof(lines[line_index + 8 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.highlight[1] =
              std::stof(lines[line_index + 9 + (i * 10)]);
          getControllerWindow(tabs.back().ID)
              ->model.meshes[i]
              .material.highlight[2] =
              std::stof(lines[line_index + 10 + (i * 10)]);
        }
      }
      if (line == "textures") {
        unsigned texture_count = 0;
        for (int mesh = 0;
             mesh <
             (int)getControllerWindow(tabs.back().ID)->model.meshes.size();
             mesh++) {
          int mesh_textures = std::stoi(lines[line_index + 1 + mesh]);
          for (int i = 0; i < mesh_textures; i++) {
            unsigned texture_line =
                line_index +
                (int)getControllerWindow(tabs.back().ID)->model.meshes.size() +
                1 + texture_count * 13;
            Texture t;
            loadTexture(t.id, lines[texture_line]);
            t.path = lines[texture_line];
            t.name = lines[texture_line];
            t.type = std::stoi(lines[texture_line + 1]);
            t.wrapX = std::stoi(lines[texture_line + 2]);
            t.wrapY = std::stoi(lines[texture_line + 3]);
            t.offsetX = std::stof(lines[texture_line + 4]);
            t.offsetY = std::stof(lines[texture_line + 5]);
            t.scaleX = std::stof(lines[texture_line + 6]);
            t.scaleY = std::stof(lines[texture_line + 7]);
            t.rotation = std::stof(lines[texture_line + 8]);
            t.border[0] = std::stof(lines[texture_line + 9]);
            t.border[1] = std::stof(lines[texture_line + 10]);
            t.border[2] = std::stof(lines[texture_line + 11]);
            t.border[3] = std::stof(lines[texture_line + 12]);
            texture_count++;
            glBindTexture(GL_TEXTURE_2D, t.id);
            enum Wrap {
              repeat,
              mirror_repeat,
              clamp_edge,
              clamp_border,
              wrap_count
            };
            switch (t.wrapX) {
            case repeat:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
              break;
            case mirror_repeat:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                              GL_MIRRORED_REPEAT);
              break;
            case clamp_edge:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_EDGE);
              break;
            case clamp_border:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_BORDER);
              break;
            }
            switch (t.wrapY) {
            case repeat:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
              break;
            case mirror_repeat:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                              GL_MIRRORED_REPEAT);
              break;
            case clamp_edge:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_EDGE);
              break;
            case clamp_border:
              glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_BORDER);
              break;
            }
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, t.border);
            getControllerWindow(tabs.back().ID)
                ->model.meshes[mesh]
                .textures.push_back(t);
          }
        }
      }
      if (line == "gyro debug logging") {
        getControllerWindow(tabs.back().ID)->gyro_debug_logging =
            std::stoi(lines[line_index + 1]);
      }
      if (line == "gyro sensitivity") {
        getControllerWindow(tabs.back().ID)->gyro_sensitivity =
            std::stof(lines[line_index + 1]);
      }
      if (line == "touch area offset x") {
        getControllerWindow(tabs.back().ID)->touch_area_offset[0] =
            std::stof(lines[line_index + 1]);
      }
      if (line == "touch area offset y") {
        getControllerWindow(tabs.back().ID)->touch_area_offset[1] =
            std::stof(lines[line_index + 1]);
      }
      if (line == "touch area offset z") {
        getControllerWindow(tabs.back().ID)->touch_area_offset[2] =
            std::stof(lines[line_index + 1]);
      }
      if (line == "gyro enabled") {
        getControllerWindow(tabs.back().ID)->gyro_enabled =
            std::stoi(lines[line_index + 1]);
        SDL_GameControllerSetSensorEnabled(
            getControllerWindow(tabs.back().ID)->sdl_controller,
            SDL_SENSOR_GYRO,
            (SDL_bool)getControllerWindow(tabs.back().ID)->gyro_enabled);
      }
      if (line == "reset gyro button 1")
        getControllerWindow(tabs.back().ID)->reset_gyro_button1 =
            std::stoi(lines[line_index + 1]);
      if (line == "reset gyro button 2")
        getControllerWindow(tabs.back().ID)->reset_gyro_button2 =
            std::stoi(lines[line_index + 1]);
      if (line == "gyro correction")
        getControllerWindow(tabs.back().ID)->gyro_correction =
            std::stoi(lines[line_index + 1]);
      line_index++;
      if (line == "direct lights") {
        getControllerWindow(tabs.back().ID)->direct_lights.clear();
        int lights = std::stoi(lines[line_index]);
        for (int i = 0; i < lights; i++) {
          direct_light new_light;
          new_light.name = lines[line_index + (i * 7) + 1];
          new_light.direction[0] = std::stof(lines[line_index + (i * 7) + 2]);
          new_light.direction[1] = std::stof(lines[line_index + (i * 7) + 3]);
          new_light.direction[2] = std::stof(lines[line_index + (i * 7) + 4]);
          new_light.color[0] = std::stof(lines[line_index + (i * 7) + 5]);
          new_light.color[1] = std::stof(lines[line_index + (i * 7) + 6]);
          new_light.color[2] = std::stof(lines[line_index + (i * 7) + 7]);
          getControllerWindow(tabs.back().ID)
              ->direct_lights.push_back(new_light);
        }
      }
      if (line == "point lights") {
        getControllerWindow(tabs.back().ID)->point_lights.clear();
        int lights = std::stoi(lines[line_index]);
        for (int i = 0; i < lights; i++) {
          point_light new_light;
          new_light.name = lines[line_index + (i * 9) + 1];
          new_light.hide = (bool)std::stoi(lines[line_index + (i * 9) + 2]);
          new_light.position[0] = std::stof(lines[line_index + (i * 9) + 3]);
          new_light.position[1] = std::stof(lines[line_index + (i * 9) + 4]);
          new_light.position[2] = std::stof(lines[line_index + (i * 9) + 5]);
          new_light.intensity = std::stof(lines[line_index + (i * 9) + 6]);
          new_light.color[0] = std::stof(lines[line_index + (i * 9) + 7]);
          new_light.color[1] = std::stof(lines[line_index + (i * 9) + 8]);
          new_light.color[2] = std::stof(lines[line_index + (i * 9) + 9]);
          new_light.ambient.r = new_light.color[0] * 0.05f;
          new_light.ambient.g = new_light.color[1] * 0.05f;
          new_light.ambient.b = new_light.color[2] * 0.05f;
          new_light.diffuse.r = new_light.color[0] * 0.8f;
          new_light.diffuse.g = new_light.color[1] * 0.8f;
          new_light.diffuse.b = new_light.color[2] * 0.8f;
          new_light.specular.r = new_light.color[0];
          new_light.specular.g = new_light.color[1];
          new_light.specular.b = new_light.color[2];
          getControllerWindow(tabs.back().ID)
              ->point_lights.push_back(new_light);
        }
      }
      if (line == "spot lights") {
        getControllerWindow(tabs.back().ID)->spot_lights.clear();
        int lights = std::stoi(lines[line_index]);
        for (int i = 0; i < lights; i++) {
          spot_light new_light;
          new_light.name = lines[line_index + (i * 13) + 1];
          new_light.hide = (bool)std::stoi(lines[line_index + (i * 13) + 2]);
          new_light.position[0] = std::stof(lines[line_index + (i * 13) + 3]);
          new_light.position[1] = std::stof(lines[line_index + (i * 13) + 4]);
          new_light.position[2] = std::stof(lines[line_index + (i * 13) + 5]);
          new_light.intensity = std::stof(lines[line_index + (i * 13) + 6]);
          new_light.color[0] = std::stof(lines[line_index + (i * 13) + 7]);
          new_light.color[1] = std::stof(lines[line_index + (i * 13) + 8]);
          new_light.color[2] = std::stof(lines[line_index + (i * 13) + 9]);
          new_light.ambient.r = new_light.color[0] * 0.05f;
          new_light.ambient.g = new_light.color[1] * 0.05f;
          new_light.ambient.b = new_light.color[2] * 0.05f;
          new_light.diffuse.r = new_light.color[0] * 0.8f;
          new_light.diffuse.g = new_light.color[1] * 0.8f;
          new_light.diffuse.b = new_light.color[2] * 0.8f;
          new_light.specular.r = new_light.color[0];
          new_light.specular.g = new_light.color[1];
          new_light.specular.b = new_light.color[2];
          new_light.yaw = std::stof(lines[line_index + (i * 13) + 10]);
          new_light.pitch = std::stof(lines[line_index + (i * 13) + 11]);
          new_light.direction.x = cos(glm::radians(new_light.pitch)) *
                                  sin(glm::radians(new_light.yaw + 180));
          new_light.direction.y = sin(glm::radians(new_light.pitch));
          new_light.direction.z = cos(glm::radians(new_light.pitch)) *
                                  cos(glm::radians(new_light.yaw + 180));
          new_light.cutoff = std::stof(lines[line_index + (i * 13) + 12]);
          new_light.outer_cutoff = std::stof(lines[line_index + (i * 13) + 13]);
          getControllerWindow(tabs.back().ID)->spot_lights.push_back(new_light);
        }
      }
    }
  }
}

bool check_filename_valid(const char *name) {
  bool valid = true;
  for (int i = 0; i < 32; i++) {
    for (char c : invalid_characters) {
      if (name[i] == c) {
        valid = false;
        break;
      }
    }
    if (!valid)
      break;
  }
  return valid;
}

std::string get_top_folder(std::string path) {
  std::string delimiter = "/";
  std::string dir = path;
  struct stat sb;
  if (stat(dir.c_str(), &sb) == 0 && (sb.st_mode & S_IFDIR)) {
    size_t pos = 0;
    while ((pos = dir.find(delimiter)) != std::string::npos) {
      dir.erase(0, pos + delimiter.length());
    }
  }
  return dir;
}

std::vector<std::string> get_binding(std::string b) {
  std::vector<std::string> binding;
  std::stringstream line_stream(b);
  std::string word;
  while (std::getline(line_stream, word, ':')) {
    binding.push_back(word);
  }
  return binding;
}

std::vector<std::string>
get_current_mapping(SDL_GameController *sdl_controller) {
  std::vector<std::string> mapping;
  if (sdl_controller) {
    char *mapping_str = SDL_GameControllerMapping(sdl_controller);
    if (mapping_str) {
      std::stringstream line_stream(mapping_str);
      std::string word;
      while (std::getline(line_stream, word, ',')) {
        if (!word.empty())
          mapping.push_back(word);
      }
      SDL_free(mapping_str);
    }
  }
  return mapping;
}

std::string get_first_model() {
  std::string dir_path = get_models_root();
  dir_path.append("/");
  // get_models_root() already guarantees the directory exists (creating
  // and, on first run, seeding it from the bundled/installed defaults),
  // so no separate exists()/create_directories() check is needed here.
  std::vector<std::string> model_folders;
  struct stat sb;
  for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
    if (stat(entry.path().string().c_str(), &sb) == 0 &&
        (sb.st_mode & S_IFDIR)) {
      model_folders.push_back(entry.path().string());
    }
  }
  if (model_folders.empty())
    return "";
  return model_folders.front();
}

void OsOpenInShell(const char *path) {
  std::string open_executable = "";
#if defined(__linux__)
  open_executable = "xdg-open";
#elif __APPLE__
  open_executable = "open";
#elif _WIN32
  ::ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWDEFAULT);
#else
  // unsupported
#endif
  if (!open_executable.empty()) {
    char command[256];
    snprintf(command, 256, "%s \"%s\"", open_executable.c_str(), path);
    system(command);
  }
}