#include "controller_window.h"
#include "cube_info.h"
#include "settings_window.h"
#include "shader.h"
#include "shaders.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

extern unsigned selected_tab;
extern unsigned selected_mesh;
extern std::vector<window_tab> tabs;

extern bool g_log_buttons;
extern std::string config_base_path;
extern bool gQuit;

static GLuint g_glowTexture = 0;

void createGlowTexture() {
  if (g_glowTexture)
    return;
  const int size = 128;
  std::vector<unsigned char> data(size * size * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      float dx = (x - size / 2.0f) / (size / 2.0f);
      float dy = (y - size / 2.0f) / (size / 2.0f);
      float dist = std::sqrt(dx * dx + dy * dy);
      float alpha = 1.0f - dist;
      if (alpha < 0)
        alpha = 0;
      // Smoothstep for soft falloff
      alpha = alpha * alpha * (3 - 2 * alpha);
      int idx = (y * size + x) * 4;
      data[idx + 0] = 100; // Light blue glow
      data[idx + 1] = 180;
      data[idx + 2] = 255;
      data[idx + 3] = (unsigned char)(alpha * 255);
    }
  }
  glGenTextures(1, &g_glowTexture);
  glBindTexture(GL_TEXTURE_2D, g_glowTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
void createTouchAreaRect(controller_window &w) {
  if (!w.touch_area_vao) {
    // Vertices in the XZ plane (Y=0), centered at origin.
    float vertices[] = {-0.5f, 0.0f, -0.5f, 0.5f,  0.0f, -0.5f,
                        0.5f,  0.0f, 0.5f,  -0.5f, 0.0f, 0.5f};
    unsigned int line_indices[] = {0, 1, 1, 2, 2, 3, 3, 0};
    glGenVertexArrays(1, &w.touch_area_vao);
    glGenBuffers(1, &w.touch_area_vbo);
    glGenBuffers(1, &w.touch_area_ebo);
    glBindVertexArray(w.touch_area_vao);
    glBindBuffer(GL_ARRAY_BUFFER, w.touch_area_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, w.touch_area_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(line_indices), line_indices,
                 GL_STATIC_DRAW);
    w.touch_area_elements = 8;
    glBindVertexArray(0);
  }
}

// Helper to log axis changes (only when logging is enabled)
void logAxisChange(controller_window &w, int axisIdx, float value,
                   const std::string &label) {
  if (g_log_buttons && axisIdx < 32) {
    float diff = value - w.last_axis_values[axisIdx];
    if (fabs(diff) > 0.01f) { // avoid spam
      spdlog::info("Axis {} ({}) changed to {:.3f}", axisIdx, label, value);
      w.last_axis_values[axisIdx] = value;
    }
  }
}

// Helper to log hat changes
void logHatChange(controller_window &w, int hatIdx, Uint8 value) {
  if (g_log_buttons && hatIdx < 16) {
    if (value != w.last_hat_values[hatIdx]) {
      const char *dirName = "Center";
      switch (value) {
      case SDL_HAT_UP:
        dirName = "Up";
        break;
      case SDL_HAT_RIGHT:
        dirName = "Right";
        break;
      case SDL_HAT_DOWN:
        dirName = "Down";
        break;
      case SDL_HAT_LEFT:
        dirName = "Left";
        break;
      case SDL_HAT_RIGHTUP:
        dirName = "Right-Up";
        break;
      case SDL_HAT_RIGHTDOWN:
        dirName = "Right-Down";
        break;
      case SDL_HAT_LEFTUP:
        dirName = "Left-Up";
        break;
      case SDL_HAT_LEFTDOWN:
        dirName = "Left-Down";
        break;
      default:
        dirName = "Unknown";
        break;
      }
      spdlog::info("Hat {} changed to {}", hatIdx, dirName);
      w.last_hat_values[hatIdx] = value;
    }
  }
}

std::string button_names[21] = {"south button", "east button",  "west button",
                                "north button", "back button",  "guide button",
                                "start button", "left cap",     "right cap",
                                "left bumper",  "right bumper", "d-pad up",
                                "d-pad down",   "d-pad left",   "d-pad right",
                                "misc",         "paddle 1",     "paddle 2",
                                "paddle 3",     "paddle 4",     "touchpad"};

float grid_vertices[] = {-1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f,  0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                         0.0f,  0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};

unsigned int defaultWidth = 640;
unsigned int defaultHeight = 480;
std::vector<controller_window> windows;

// Helper: get axis value with choice of raw joystick or gamecontroller
float get_axis_value_choice(controller_window &w, int axis_idx, bool useRaw) {
  if (useRaw) {
    SDL_Joystick *joy = nullptr;
    if (w.is_gamecontroller && w.sdl_controller) {
      joy = SDL_GameControllerGetJoystick(w.sdl_controller);
    } else if (w.sdl_joystick) {
      joy = w.sdl_joystick;
    }
    if (joy && axis_idx < SDL_JoystickNumAxes(joy)) {
      return SDL_JoystickGetAxis(joy, axis_idx) / 32767.0f;
    }
    return 0.0f;
  } else {
    // gamecontroller API
    if (w.is_gamecontroller && w.sdl_controller) {
      if (axis_idx >= 0 && axis_idx < 6) {
        return SDL_GameControllerGetAxis(w.sdl_controller,
                                         (SDL_GameControllerAxis)axis_idx) /
               32767.0f;
      }
      // For axes beyond 6, fallback to raw joystick? We'll return 0.
    }
    return 0.0f;
  }
}

// Helper: get axis value (original behaviour for gamecontroller)
float get_axis_value(controller_window &w, int axis_idx) {
  // For standard axes 0-5 use gamecontroller API, for extra axes use raw
  // joystick.
  if (axis_idx < 6) {
    return get_axis_value_choice(w, axis_idx, false);
  } else {
    return get_axis_value_choice(w, axis_idx, true);
  }
}

// Helper: get button value with choice
bool get_button_value_choice(controller_window &w, int btn_idx, bool useRaw) {
  if (useRaw) {

    SDL_Joystick *joy = nullptr;
    if (w.is_gamecontroller && w.sdl_controller) {
      joy = SDL_GameControllerGetJoystick(w.sdl_controller);
    } else if (w.sdl_joystick) {
      joy = w.sdl_joystick;
    }
    if (joy && btn_idx < SDL_JoystickNumButtons(joy)) {
      return SDL_JoystickGetButton(joy, btn_idx);
    }
    return false;
  } else {
    if (w.is_gamecontroller && w.sdl_controller) {
      if (btn_idx < SDL_CONTROLLER_BUTTON_MAX) {
        return SDL_GameControllerGetButton(w.sdl_controller,
                                           (SDL_GameControllerButton)btn_idx);
      }
    }
    return false;
  }
}

// Helper: get hat value (raw joystick)
static Uint8 getHatValue(controller_window &w, int hatIdx) {
  if (w.sdl_joystick && hatIdx < SDL_JoystickNumHats(w.sdl_joystick)) {
    return SDL_JoystickGetHat(w.sdl_joystick, hatIdx);
  }
  return SDL_HAT_CENTERED;
}

void createControllerWindow(std::string title, std::string model_path) {
  controller_window w;
  w.gyro_sensitivity = 5.0f;
  w.logger = spdlog::get("3dco+"); // reuse global logger

  glfwWindowHint(GLFW_SAMPLES, 4);
  w.glfw_window =
      glfwCreateWindow(defaultWidth, defaultHeight, title.c_str(), NULL, NULL);
  if (!w.glfw_window) {
    spdlog::error("Failed to create controller window: {}", title);
    glfwTerminate();
    return;
  }
  glfwMakeContextCurrent(w.glfw_window);
  glEnable(GL_MULTISAMPLE);

  // Icon loading
  GLFWimage images[1];
  images[0].pixels =
      stbi_load("icon.png", &images[0].width, &images[0].height, 0, 4);
  if (images[0].pixels) {
    glfwSetWindowIcon(w.glfw_window, 1, images);
    stbi_image_free(images[0].pixels);
  } else {
    spdlog::warn("Could not load icon.png");
  }

  glfwSetScrollCallback(w.glfw_window, controller_window_scroll_callback);
  w.lastFrame = glfwGetTime();

  make_grid(w);
  lightingSpecification(w);

  createShader(w.shader, vertex_shader_code.c_str(),
               fragment_shader_code.c_str());
  createShader(w.grid_shader, grid_vertex_shader_code.c_str(),
               grid_fragment_shader_code.c_str());
  createShader(w.light_source_shader, light_source_vertex_shader_code.c_str(),
               light_source_fragment_shader_code.c_str());
  createShader(w.touch_shader, touch_area_vertex_shader_code.c_str(),
               touch_area_fragment_shader_code.c_str());

  direct_light d;
  w.direct_lights.push_back(d);

  loadModel(w.model, model_path);

  if (w.model.meshes.empty()) {
    spdlog::error("Failed to load any meshes for model at '{}'.", model_path);
  } else {
    spdlog::info("Loaded {} meshes from '{}'.", w.model.meshes.size(),
                 model_path);
  }

  w.model_name = get_top_folder(model_path);

  // --- Device enumeration and opening ---
  // Skip if this is a dummy preview window (import preview)
  if (model_path == "dummy") {
    // Dummy preview window – no controller attached
    spdlog::info("Creating import preview window (no controller)");
    w.sdl_controller = nullptr;
    w.sdl_joystick = nullptr;
    w.is_gamecontroller = false;
    w.gyro_enabled = false;
    w.gyro_sensor = nullptr;
    w.accel_sensor = nullptr;
    w.scroll_to_resize = false;
    w.drag_to_move = false;
    w.freelook = false;
    w.mouse_first_click = true;
  }
  if (model_path != "dummy") {
    int num_joysticks = SDL_NumJoysticks();
    if (num_joysticks == 0) {
      spdlog::error("No joysticks found.");
    } else {
      spdlog::info("Found {} joystick(s).", num_joysticks);
      for (int i = 0; i < num_joysticks; ++i) {
        const char *name = SDL_JoystickNameForIndex(i);
        bool is_game = SDL_IsGameController(i);
        spdlog::debug("Device {}: {} (gamecontroller: {})", i,
                      name ? name : "Unknown", is_game);
      }

      // For now, open the first available device
      int chosen = 0;
      if (SDL_IsGameController(chosen)) {
        w.sdl_controller = SDL_GameControllerOpen(chosen);
        w.is_gamecontroller = true;
        w.joystick_index = chosen;
        if (w.sdl_controller) {
          spdlog::info("Opened gamecontroller: {}",
                       SDL_GameControllerName(w.sdl_controller));
          // Enable sensors if available
          if (SDL_GameControllerHasSensor(w.sdl_controller, SDL_SENSOR_GYRO)) {
            SDL_GameControllerSetSensorEnabled(w.sdl_controller,
                                               SDL_SENSOR_GYRO, SDL_TRUE);
            spdlog::info("Controller has gyro: true");
            w.gyro_enabled = true;
            // Read initial timestamp to avoid large dt on first frame
            Uint64 timestamp;
            if (SDL_GameControllerGetSensorDataWithTimestamp(
                    w.sdl_controller, SDL_SENSOR_GYRO, &timestamp, w.gyro_data,
                    3) == 0) {
              w.gyro_time = timestamp;
              w.gyro_toggled = true;
            }
          }
          if (SDL_GameControllerHasSensor(w.sdl_controller, SDL_SENSOR_ACCEL)) {
            SDL_GameControllerSetSensorEnabled(w.sdl_controller,
                                               SDL_SENSOR_ACCEL, SDL_TRUE);
          }
        } else {
          spdlog::error("Failed to open gamecontroller {}: {}", chosen,
                        SDL_GetError());
        }
      } else {
        w.sdl_joystick = SDL_JoystickOpen(chosen);
        spdlog::warn("Generic joystick opened – you may need to manually map "
                     "buttons in the Mapping section.");
        w.is_gamecontroller = false;
        w.joystick_index = chosen;
        if (w.sdl_joystick) {
          spdlog::info("Opened generic joystick: {}",
                       SDL_JoystickName(w.sdl_joystick));
          // --- Match sensors by device instance ID ---
          SDL_JoystickID joyID = SDL_JoystickInstanceID(w.sdl_joystick);
          int num_sensors = SDL_NumSensors();
          for (int s = 0; s < num_sensors; ++s) {
            if (SDL_SensorGetDeviceType(s) == SDL_SENSOR_GYRO) {
              SDL_SensorID sensorID = SDL_SensorGetDeviceInstanceID(s);
              // If the sensor's instance ID matches the joystick's, open it.
              if (sensorID == joyID) {
                w.gyro_sensor = SDL_SensorOpen(s);
                if (w.gyro_sensor) {
                  spdlog::info("Gyro sensor opened (index {})", s);
                  w.gyro_enabled = true;
                  break;
                }
              }
            }
          }
          // If no matching gyro was found, fallback to opening the first gyro
          // sensor (some platforms may not have the same instance ID).
          if (!w.gyro_sensor) {
            for (int s = 0; s < num_sensors; ++s) {
              if (SDL_SensorGetDeviceType(s) == SDL_SENSOR_GYRO) {
                w.gyro_sensor = SDL_SensorOpen(s);
                if (w.gyro_sensor) {
                  spdlog::info("Gyro sensor opened by fallback (index {})", s);
                  w.gyro_enabled = true;
                  break;
                }
              }
            }
          }
          // Same for accelerometer
          for (int s = 0; s < num_sensors; ++s) {
            if (SDL_SensorGetDeviceType(s) == SDL_SENSOR_ACCEL) {
              SDL_SensorID sensorID = SDL_SensorGetDeviceInstanceID(s);
              if (sensorID == joyID) {
                w.accel_sensor = SDL_SensorOpen(s);
                if (w.accel_sensor) {
                  spdlog::info("Accel sensor opened (index {})", s);
                  break;
                }
              }
            }
          }
          if (!w.accel_sensor) {
            for (int s = 0; s < num_sensors; ++s) {
              if (SDL_SensorGetDeviceType(s) == SDL_SENSOR_ACCEL) {
                w.accel_sensor = SDL_SensorOpen(s);
                if (w.accel_sensor) {
                  spdlog::info("Accel sensor opened by fallback (index {})", s);
                  break;
                }
              }
            }
          }
        } else {
          spdlog::error("Failed to open generic joystick {}: {}", chosen,
                        SDL_GetError());
        }
      }
    }
  } else {
    // Dummy preview window – no controller attached
    spdlog::info("Creating import preview window (no controller)");
    w.sdl_controller = nullptr;
    w.sdl_joystick = nullptr;
    w.is_gamecontroller = false;
    w.gyro_enabled = false;
    w.gyro_sensor = nullptr;
    w.accel_sensor = nullptr;
    w.scroll_to_resize = false;
    w.drag_to_move = false;
    w.freelook = false;
    w.mouse_first_click = true;
  }

  w.gyro_matrix = glm::mat4(1.0f);

  // ----- RESET BUTTON STATES FOR THIS WINDOW -----
  for (int i = 0; i < 128; ++i) {
    w.last_joy_button_values[i] = false;
  }
  for (int i = 0; i < 64; ++i) {
    w.last_button_values[i] = false;
  }

  windows.push_back(w);
}

void applyMappingToMeshes(controller_window &w) {
  for (int meshIdx = 0; meshIdx < (int)w.model.meshes.size(); ++meshIdx) {
    Mesh &mesh = w.model.meshes[meshIdx];
    if (mesh.inputBinding.empty())
      continue;

    // Parse type:value
    size_t colon = mesh.inputBinding.find(':');
    if (colon == std::string::npos)
      continue;
    std::string type = mesh.inputBinding.substr(0, colon);
    std::string value = mesh.inputBinding.substr(colon + 1);

    // Reset press/highlight for this mesh
    mesh.press = 0.0f;
    mesh.highlight_value = 0.0f;
    mesh.pull = 0.0f;

    if (type == "gamepad" || type == "joystick") {
      bool useRaw = (type == "joystick");

      // ---- Handle full stick bindings ----
      if (value == "leftstick") {
        float lx = get_axis_value_choice(w, 0, useRaw);
        float ly = get_axis_value_choice(w, 1, useRaw);
        if (mesh.invert) {
          lx = -lx;
          ly = -ly;
        }
        mesh.stick_X = lx * 32767.0f;
        mesh.stick_Y = ly * 32767.0f;
        if (fabs(lx) > 0.1f || fabs(ly) > 0.1f)
          mesh.highlight_value = std::max(fabs(lx), fabs(ly)) * 1.2f;
        else
          mesh.highlight_value = 0.0f;
        continue;
      } else if (value == "rightstick") {
        float rx = get_axis_value_choice(w, 2, useRaw);
        float ry = get_axis_value_choice(w, 3, useRaw);
        if (mesh.invert) {
          rx = -rx;
          ry = -ry;
        }
        mesh.stick_X = rx * 32767.0f;
        mesh.stick_Y = ry * 32767.0f;
        if (fabs(rx) > 0.1f || fabs(ry) > 0.1f)
          mesh.highlight_value = std::max(fabs(rx), fabs(ry)) * 1.2f;
        else
          mesh.highlight_value = 0.0f;
        continue;
      }

      // Parse value: could be bX, aX+, aX-, hX.Y
      char prefix = value[0];
      int num = 0;
      int hatDir = -1;
      bool isDirection = false;
      int dir = 0;

      if (prefix == 'b') {
        num = std::stoi(value.substr(1));
        bool pressed = get_button_value_choice(w, num, useRaw);
        if (mesh.invert)
          pressed = !pressed;
        mesh.press = pressed ? 1.0f : 0.0f;
        mesh.highlight_value = pressed ? 1.0f : 0.0f;
      } else if (prefix == 'a') {
        // Check for + or - at end
        if (value.back() == '+') {
          isDirection = true;
          dir = 1;
          num = std::stoi(value.substr(1, value.size() - 2));
        } else if (value.back() == '-') {
          isDirection = true;
          dir = -1;
          num = std::stoi(value.substr(1, value.size() - 2));
        } else {
          // Plain analog axis (no + or -)
          num = std::stoi(value.substr(1));
        }
        float axisVal = get_axis_value_choice(w, num, useRaw);
        if (mesh.invert)
          axisVal = -axisVal;

        if (isDirection) {
          // Directional (button-like) axis: press if past threshold
          bool pressed = (dir > 0) ? (axisVal > 0.5f) : (axisVal < -0.5f);
          mesh.press = pressed ? 1.0f : 0.0f;
          mesh.highlight_value = pressed ? 1.0f : 0.0f;
        } else {
          // Plain analog axis
          if (type == "gamepad" && (num == 4 || num == 5)) {
            // Gamepad triggers are already 0..1
            float val = std::max(0.0f, std::min(1.0f, axisVal));
            mesh.pull = val * 32767.0f;
            mesh.press = val;
            mesh.highlight_value = val;
          } else {
            // For centered axes (sticks, generic joystick) map -1..1 → 0..1
            float val = (axisVal + 1.0f) * 0.5f;
            mesh.pull = val * 32767.0f;
            mesh.press = val;
            mesh.highlight_value = val;
          }
        }
      } else if (prefix == 'h') {
        // Hat: hX.Y – no inversion
        size_t dot = value.find('.');
        if (dot != std::string::npos) {
          num = std::stoi(value.substr(1, dot - 1));
          hatDir = std::stoi(value.substr(dot + 1));
          Uint8 hatVal = getHatValue(w, num);
          Uint8 sdlDir = 0;
          switch (hatDir) {
          case 0:
            sdlDir = SDL_HAT_UP;
            break;
          case 1:
            sdlDir = SDL_HAT_RIGHTUP;
            break;
          case 2:
            sdlDir = SDL_HAT_RIGHT;
            break;
          case 3:
            sdlDir = SDL_HAT_RIGHTDOWN;
            break;
          case 4:
            sdlDir = SDL_HAT_DOWN;
            break;
          case 5:
            sdlDir = SDL_HAT_LEFTDOWN;
            break;
          case 6:
            sdlDir = SDL_HAT_LEFT;
            break;
          case 7:
            sdlDir = SDL_HAT_LEFTUP;
            break;
          }
          bool pressed = (hatVal & sdlDir) != 0;
          mesh.press = pressed ? 1.0f : 0.0f;
          mesh.highlight_value = pressed ? 1.0f : 0.0f;
        }
      }

      // ---- Touchpad handling (only for gamepad) ----
      if (type == "gamepad" && value.rfind("touch", 0) == 0) {
        // Parse touchX_fY[_z] (z is optional axis)
        std::string rest = value.substr(5);
        size_t underscore1 = rest.find('_');
        if (underscore1 != std::string::npos) {
          std::string touchStr = rest.substr(0, underscore1);
          std::string rest2 = rest.substr(underscore1 + 1);
          size_t underscore2 = rest2.find('_');
          if (underscore2 == std::string::npos) {
            // Combined: touchX_fY (both axes)
            std::string fingerStr = rest2;
            int touchpadIdx = std::stoi(touchStr);
            int fingerIdx = std::stoi(fingerStr.substr(1));
            if (touchpadIdx >= 0 && touchpadIdx < 4 && fingerIdx >= 0 &&
                fingerIdx < 2) {
              auto &ts = w.touchpad_data[touchpadIdx][fingerIdx];
              if (ts.state == 1) {
                float x = ts.x;
                float y = ts.y;
                if (mesh.invert) {
                  x = 1.0f - x;
                  y = 1.0f - y;
                }
                mesh.touch_X = x;
                mesh.touch_Y = y;
                mesh.touch_state = 1;
                mesh.glow_intensity = 1.0f;

                // ---- AUTO-ANCHOR: if this mesh is a touchpoint part, parent
                // it to a touchpad ----
                int part = mesh.assignedPart;
                if (part == 30 || part == 31 || part == 33 || part == 34) {
                  int touchpadIdxFound = getTouchpadAncestor(w.model, meshIdx);
                  if (touchpadIdxFound == -1) {
                    // No touchpad ancestor – search for any other mesh marked
                    // as touchpad
                    for (int i = 0; i < (int)w.model.meshes.size(); ++i) {
                      if (i != meshIdx && w.model.meshes[i].isTouchpad) {
                        touchpadIdxFound = i;
                        break;
                      }
                    }
                  }
                  if (touchpadIdxFound != -1 && touchpadIdxFound != meshIdx) {
                    // Only update if parent changes or position needs reset
                    if (mesh.parentIndex != touchpadIdxFound ||
                        mesh.position[0] != 0.0f || mesh.position[1] != 0.0f ||
                        mesh.position[2] != 0.0f) {
                      mesh.parentIndex = touchpadIdxFound;
                      mesh.position[0] = 0.0f;
                      mesh.position[1] = 0.0f;
                      mesh.position[2] = 0.0f;
                      mesh.useCustomScale = false;
                      spdlog::info("Anchored touchpoint '{}' to touchpad '{}'",
                                   mesh.name,
                                   w.model.meshes[touchpadIdxFound].name);
                    }
                  } else if (touchpadIdxFound == meshIdx) {
                    spdlog::warn("Touchpoint '{}' is also a touchpad – cannot "
                                 "anchor to itself",
                                 mesh.name);
                  }
                }
              } else {
                mesh.touch_state = 0;
                mesh.glow_intensity = 0.0f;
              }
            }
          } else {
            // Per-axis: touchX_fY_z
            std::string fingerStr = rest2.substr(0, underscore2);
            char axis = rest2.back();
            int touchpadIdx = std::stoi(touchStr);
            int fingerIdx = std::stoi(fingerStr.substr(1));
            if (touchpadIdx >= 0 && touchpadIdx < 4 && fingerIdx >= 0 &&
                fingerIdx < 2) {
              auto &ts = w.touchpad_data[touchpadIdx][fingerIdx];
              if (ts.state == 1) {
                float val = (axis == 'x') ? ts.x : ts.y;
                if (mesh.invert)
                  val = 1.0f - val;
                if (axis == 'x')
                  mesh.touch_X = val;
                else
                  mesh.touch_Y = val;
                mesh.touch_state = 1;
                mesh.glow_intensity = 1.0f;

                // ---- AUTO-ANCHOR (same as above) ----
                int part = mesh.assignedPart;
                if (part == 30 || part == 31 || part == 33 || part == 34) {
                  int touchpadIdxFound = getTouchpadAncestor(w.model, meshIdx);
                  if (touchpadIdxFound == -1) {
                    for (int i = 0; i < (int)w.model.meshes.size(); ++i) {
                      if (i != meshIdx && w.model.meshes[i].isTouchpad) {
                        touchpadIdxFound = i;
                        break;
                      }
                    }
                  }
                  if (touchpadIdxFound != -1 && touchpadIdxFound != meshIdx) {
                    if (mesh.parentIndex != touchpadIdxFound ||
                        mesh.position[0] != 0.0f || mesh.position[1] != 0.0f ||
                        mesh.position[2] != 0.0f) {
                      mesh.parentIndex = touchpadIdxFound;
                      mesh.position[0] = 0.0f;
                      mesh.position[1] = 0.0f;
                      mesh.position[2] = 0.0f;
                      mesh.useCustomScale = false;
                      spdlog::info("Anchored touchpoint '{}' to touchpad '{}'",
                                   mesh.name,
                                   w.model.meshes[touchpadIdxFound].name);
                    }
                  } else if (touchpadIdxFound == meshIdx) {
                    spdlog::warn("Touchpoint '{}' is also a touchpad – cannot "
                                 "anchor to itself",
                                 mesh.name);
                  }
                }
              } else {
                mesh.touch_state = 0;
                mesh.glow_intensity = 0.0f;
              }
            }
          }
        }
        continue; // done with this mesh
      }
    } else if (type == "keyboard") {
      // Keyboard – inversion will be added later
    } else if (type == "mouse") {
      // Mouse – inversion will be added later
    }
  }
}
void controller_window_input() {
  SDL_PumpEvents();

  for (auto &w : windows) {
    // Helper: safe access to mesh by index
    auto meshAt = [&](int idx) -> Mesh * {
      return (idx >= 0 && idx < (int)w.model.meshes.size())
                 ? &w.model.meshes[idx]
                 : nullptr;
    };

    // ---------- CONTROLLER INPUT (skip for preview windows) ----------
    if (!w.is_import_preview) {
      if (w.model.meshes.empty()) {
        spdlog::warn("Controller window has empty model meshes; skipping "
                     "controller input.");
        // Still allow mouse orbit below
      } else {
        // --- GYRO ---
        if (w.gyro_enabled) {
          bool has_gyro_source = false;
          if (w.is_gamecontroller && w.sdl_controller) {
            has_gyro_source = true;
            Uint64 timestamp;
            if (SDL_GameControllerGetSensorDataWithTimestamp(
                    w.sdl_controller, SDL_SENSOR_GYRO, &timestamp, w.gyro_data,
                    3) == 0) {
              // ---- Process gamecontroller gyro data ----
              if (isnan(w.gyro_data[0]) || isnan(w.gyro_data[1]) ||
                  isnan(w.gyro_data[2])) {
                if (w.gyro_debug_logging) {
                  spdlog::warn("Gyro data contains NaN, skipping frame");
                }
                goto skip_gyro_processing;
              }
              if (fabs(w.gyro_data[0]) < 1e-6f &&
                  fabs(w.gyro_data[1]) < 1e-6f &&
                  fabs(w.gyro_data[2]) < 1e-6f) {
                goto skip_gyro_processing;
              }
              if (w.gyro_debug_logging) {
                static int frame_counter = 0;
                if (++frame_counter % 60 == 0) {
                  spdlog::debug("Gyro raw: x={:.3f} y={:.3f} z={:.3f}",
                                w.gyro_data[0], w.gyro_data[1], w.gyro_data[2]);
                }
              }
              if (w.gyro_toggled) {
                w.gyro_time = timestamp;
                w.gyro_toggled = false;
              } else {
                float dt = (timestamp - w.gyro_time) * 0.000001f;
                if (dt > 0.1f)
                  dt = 0.1f;
                if (dt < 0.0001f)
                  dt = 0.0001f;
                float sens = w.gyro_sensitivity;
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[0] * dt * sens,
                                glm::vec3(1, 0, 0));
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[1] * dt * sens,
                                glm::vec3(0, 1, 0));
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[2] * dt * sens,
                                glm::vec3(0, 0, 1));
                // After applying rotations
                w.gyro_matrix[3][0] = 0.0f;
                w.gyro_matrix[3][1] = 0.0f;
                w.gyro_matrix[3][2] = 0.0f;
                w.gyro_matrix[3][3] = 1.0f;
                // re-orthogonalise
                glm::mat3 rot = glm::mat3(w.gyro_matrix);
                glm::vec3 col0 = rot[0];
                glm::vec3 col1 = rot[1];
                glm::vec3 col2 = rot[2];
                col0 = glm::normalize(col0);
                col1 = glm::normalize(col1 - glm::dot(col0, col1) * col0);
                col2 = glm::cross(col0, col1);
                rot = glm::mat3(col0, col1, col2);
                w.gyro_matrix = glm::mat4(rot);
                // Drift reset
                float angle = glm::angle(glm::quat_cast(w.gyro_matrix));
                if (angle > 10.0f) {
                  w.gyro_matrix = glm::mat4(1.0f);
                  if (w.gyro_debug_logging)
                    spdlog::warn("Gyro reset due to excessive drift");
                }
                w.gyro_time = timestamp;
                // Correction
                glm::vec3 up_error =
                    glm::cross(glm::vec3(0, 1, 0),
                               glm::vec3(0, 1, 0) * glm::mat3(w.gyro_matrix));
                if (glm::length(up_error) > 0.001f) {
                  w.gyro_matrix =
                      glm::rotate(w.gyro_matrix, w.gyro_correction * 0.0001f,
                                  glm::normalize(up_error));
                }
                glm::vec3 right_error =
                    glm::cross(glm::vec3(1, 0, 0),
                               glm::vec3(1, 0, 0) * glm::mat3(w.gyro_matrix));
                if (glm::length(right_error) > 0.001f) {
                  w.gyro_matrix =
                      glm::rotate(w.gyro_matrix, w.gyro_correction * 0.0001f,
                                  glm::normalize(right_error));
                }
                // Reset via buttons
                if (w.reset_gyro_button1 >= 0 && w.reset_gyro_button2 >= 0) {
                  if (get_button_value_choice(w, w.reset_gyro_button1, true) &&
                      get_button_value_choice(w, w.reset_gyro_button2, true)) {
                    w.gyro_matrix = glm::mat4(1.0f);
                    if (w.gyro_debug_logging)
                      spdlog::debug("Gyro reset via button combo");
                  }
                }
                // Debug logging
                if (w.gyro_debug_logging) {
                  static int log_counter = 0;
                  if (++log_counter % 120 == 0) {
                    glm::vec3 euler =
                        glm::eulerAngles(glm::quat_cast(w.gyro_matrix));
                    spdlog::debug(
                        "Gyro Euler: yaw={:.3f} pitch={:.3f} roll={:.3f}",
                        glm::degrees(euler.y), glm::degrees(euler.x),
                        glm::degrees(euler.z));
                  }
                }
              }
            } else {
              if (w.gyro_debug_logging) {
                spdlog::debug("Failed to read gamecontroller gyro data");
              }
            }
          } else if (w.gyro_sensor) {
            has_gyro_source = true;
            // ---- READ GENERIC SENSOR DATA ----
            float sensor_data[3];
            Uint64 timestamp;
            if (SDL_SensorGetDataWithTimestamp(w.gyro_sensor, &timestamp,
                                               sensor_data, 3) == 0) {
              // Copy to w.gyro_data for consistency
              w.gyro_data[0] = sensor_data[0];
              w.gyro_data[1] = sensor_data[1];
              w.gyro_data[2] = sensor_data[2];
              // Now process exactly the same way as gamecontroller gyro
              if (isnan(w.gyro_data[0]) || isnan(w.gyro_data[1]) ||
                  isnan(w.gyro_data[2])) {
                if (w.gyro_debug_logging) {
                  spdlog::warn("Gyro data contains NaN, skipping frame");
                }
                goto skip_gyro_processing;
              }
              if (fabs(w.gyro_data[0]) < 1e-6f &&
                  fabs(w.gyro_data[1]) < 1e-6f &&
                  fabs(w.gyro_data[2]) < 1e-6f) {
                goto skip_gyro_processing;
              }
              if (w.gyro_debug_logging) {
                static int frame_counter = 0;
                if (++frame_counter % 60 == 0) {
                  spdlog::debug("Gyro raw: x={:.3f} y={:.3f} z={:.3f}",
                                w.gyro_data[0], w.gyro_data[1], w.gyro_data[2]);
                }
              }
              if (w.gyro_toggled) {
                w.gyro_time = timestamp;
                w.gyro_toggled = false;
              } else {
                float dt = (timestamp - w.gyro_time) * 0.000001f;
                if (dt > 0.1f)
                  dt = 0.1f;
                if (dt < 0.0001f)
                  dt = 0.0001f;
                float sens = w.gyro_sensitivity;
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[0] * dt * sens,
                                glm::vec3(1, 0, 0));
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[1] * dt * sens,
                                glm::vec3(0, 1, 0));
                w.gyro_matrix =
                    glm::rotate(w.gyro_matrix, w.gyro_data[2] * dt * sens,
                                glm::vec3(0, 0, 1));
                // After applying rotations
                w.gyro_matrix[3][0] = 0.0f;
                w.gyro_matrix[3][1] = 0.0f;
                w.gyro_matrix[3][2] = 0.0f;
                w.gyro_matrix[3][3] = 1.0f;
                // re-orthogonalise
                glm::mat3 rot = glm::mat3(w.gyro_matrix);
                glm::vec3 col0 = rot[0];
                glm::vec3 col1 = rot[1];
                glm::vec3 col2 = rot[2];
                col0 = glm::normalize(col0);
                col1 = glm::normalize(col1 - glm::dot(col0, col1) * col0);
                col2 = glm::cross(col0, col1);
                rot = glm::mat3(col0, col1, col2);
                w.gyro_matrix = glm::mat4(rot);
                // Drift reset
                float angle = glm::angle(glm::quat_cast(w.gyro_matrix));
                if (angle > 10.0f) {
                  w.gyro_matrix = glm::mat4(1.0f);
                  if (w.gyro_debug_logging)
                    spdlog::warn("Gyro reset due to excessive drift");
                }
                w.gyro_time = timestamp;
                // Correction
                glm::vec3 up_error =
                    glm::cross(glm::vec3(0, 1, 0),
                               glm::vec3(0, 1, 0) * glm::mat3(w.gyro_matrix));
                if (glm::length(up_error) > 0.001f) {
                  w.gyro_matrix =
                      glm::rotate(w.gyro_matrix, w.gyro_correction * 0.0001f,
                                  glm::normalize(up_error));
                }
                glm::vec3 right_error =
                    glm::cross(glm::vec3(1, 0, 0),
                               glm::vec3(1, 0, 0) * glm::mat3(w.gyro_matrix));
                if (glm::length(right_error) > 0.001f) {
                  w.gyro_matrix =
                      glm::rotate(w.gyro_matrix, w.gyro_correction * 0.0001f,
                                  glm::normalize(right_error));
                }
                // Reset via buttons
                if (w.reset_gyro_button1 >= 0 && w.reset_gyro_button2 >= 0) {
                  if (get_button_value_choice(w, w.reset_gyro_button1, true) &&
                      get_button_value_choice(w, w.reset_gyro_button2, true)) {
                    w.gyro_matrix = glm::mat4(1.0f);
                    if (w.gyro_debug_logging)
                      spdlog::debug("Gyro reset via button combo");
                  }
                }
                // Debug logging
                if (w.gyro_debug_logging) {
                  static int log_counter = 0;
                  if (++log_counter % 120 == 0) {
                    glm::vec3 euler =
                        glm::eulerAngles(glm::quat_cast(w.gyro_matrix));
                    spdlog::debug(
                        "Gyro Euler: yaw={:.3f} pitch={:.3f} roll={:.3f}",
                        glm::degrees(euler.y), glm::degrees(euler.x),
                        glm::degrees(euler.z));
                  }
                }
              }
            } else {
              if (w.gyro_debug_logging) {
                spdlog::debug("Failed to read generic gyro data");
              }
            }
          } else {
            // no gyro source – disable
            w.gyro_enabled = false;
            w.gyro_debug_logging = false;
            if (w.gyro_debug_logging) {
              spdlog::debug("No gyro source available; disabling gyro.");
            }
          }
        // Label to skip processing on invalid data
        skip_gyro_processing:;
        } // end if gyro_enabled
        // We'll compute axis values per mesh, using the mesh's useJoystick
        // flag. Helper to find mesh by part.
        auto findMeshByPart = [&](int part) -> Mesh * {
          for (auto &mesh : w.model.meshes) {
            if (mesh.assignedPart == part)
              return &mesh;
          }
          return nullptr;
        };

        // Remove the old button loop – it's now handled by applyMappingToMeshes
        // (We'll keep it commented out or delete it)
        // Touchpad data
        if (w.is_gamecontroller && w.sdl_controller) {
          int touch_pads = SDL_GameControllerGetNumTouchpads(w.sdl_controller);
          for (int t = 0; t < touch_pads && t < 4; ++t) {
            int numFingers =
                SDL_GameControllerGetNumTouchpadFingers(w.sdl_controller, t);
            for (int f = 0; f < numFingers && f < 2; ++f) {
              SDL_GameControllerGetTouchpadFinger(
                  w.sdl_controller, t, f, &w.touchpad_data[t][f].state,
                  &w.touchpad_data[t][f].x, &w.touchpad_data[t][f].y, nullptr);
            }
          }
        }

        applyMappingToMeshes(w);

        // ---- Propagate stick motion to children (rings, caps) ----
        for (int stickPart : {5, 6}) {
          Mesh *stickMesh = nullptr;
          int stickIndex = -1;
          for (int i = 0; i < (int)w.model.meshes.size(); ++i) {
            if (w.model.meshes[i].assignedPart == stickPart) {
              stickMesh = &w.model.meshes[i];
              stickIndex = i;
              break;
            }
          }
          if (!stickMesh)
            continue;

          for (auto &child : w.model.meshes) {
            if (child.parentIndex == stickIndex && child.inputBinding.empty()) {
              // Copy stick deflection
              child.stick_X = stickMesh->stick_X;
              child.stick_Y = stickMesh->stick_Y;

              // Update highlight for ring meshes (parts 7 and 8)
              if (child.assignedPart == 7 || child.assignedPart == 8) {
                // Generic highlight for any child of a stick
                float threshold = child.ring_highlight_deadzone * 0.01f;
                float dx = fabs(stickMesh->stick_X / 32767.0f);
                float dy = fabs(stickMesh->stick_Y / 32767.0f);
                if (dx > threshold || dy > threshold) {
                  child.highlight_value = std::max(dx, dy) * 1.2f;
                } else {
                  child.highlight_value = 0.0f;
                }
              }
            }
          }
        }
      } // end if (!w.is_import_preview)

      // ---------- MOUSE ORBIT & ZOOM (for ALL windows, unless freelook)
      // ----------
      if (!w.freelook) {
        int win_width, win_height;
        glfwGetWindowSize(w.glfw_window, &win_width, &win_height);
        if (win_width == 0 || win_height == 0)
          continue;

        double mouse_x, mouse_y;
        glfwGetCursorPos(w.glfw_window, &mouse_x, &mouse_y);
        int left_button =
            glfwGetMouseButton(w.glfw_window, GLFW_MOUSE_BUTTON_LEFT);
        int middle_button =
            glfwGetMouseButton(w.glfw_window, GLFW_MOUSE_BUTTON_MIDDLE);

        // ---- Check if mouse is over the pivot circle ----
        bool pivotHit = false;
        glm::vec3 pivotPos(0.0f);
        if (selected_tab < tabs.size()) {
          unsigned activeID = tabs[selected_tab].ID;
          if (w.ID == activeID && selected_mesh >= 0 &&
              selected_mesh < (int)w.model.meshes.size()) {
            const Mesh &mesh = w.model.meshes[selected_mesh];
            if (mesh.elements > 0) {
              glm::mat4 pivotMat =
                  getMeshFinalMatrix(w.model, selected_mesh, w.gyro_matrix);
              pivotPos = glm::vec3(pivotMat[3]);
              glm::vec4 clipPos = w.projection_matrix * w.view_matrix *
                                  glm::vec4(pivotPos, 1.0f);
              if (clipPos.w > 0.0f) {
                clipPos /= clipPos.w;
                float screenX = (clipPos.x * 0.5f + 0.5f) * win_width;
                float screenY = (1.0f - (clipPos.y * 0.5f + 0.5f)) * win_height;
                double dist = sqrt((mouse_x - screenX) * (mouse_x - screenX) +
                                   (mouse_y - screenY) * (mouse_y - screenY));
                if (dist < 20.0) {
                  pivotHit = true;
                }
              }
            }
          }
        }

        // ---- Handle left button ----
        if (left_button == GLFW_PRESS && !w.drag_to_move) {
          if (pivotHit && !w.pivot_dragging) {
            w.pivot_dragging = true;
            w.pivot_drag_start_screen_x = mouse_x;
            w.pivot_drag_start_screen_y = mouse_y;
            w.pivot_drag_mesh_index = selected_mesh;
            w.pivot_drag_start_world = pivotPos;
          }

          if (w.pivot_dragging) {
            if (w.pivot_drag_mesh_index >= 0 &&
                w.pivot_drag_mesh_index < (int)w.model.meshes.size()) {
              Mesh &mesh = w.model.meshes[w.pivot_drag_mesh_index];

              glm::vec3 camRight = glm::normalize(
                  glm::vec3(w.view_matrix[0][0], w.view_matrix[1][0],
                            w.view_matrix[2][0]));
              glm::vec3 camUp = glm::normalize(glm::vec3(w.view_matrix[0][1],
                                                         w.view_matrix[1][1],
                                                         w.view_matrix[2][1]));

              float distance = glm::length(w.camera_position - pivotPos);
              float scale = distance * 0.001f;

              double dx = mouse_x - w.pivot_drag_start_screen_x;
              double dy = mouse_y - w.pivot_drag_start_screen_y;
              glm::vec3 deltaWorld =
                  (float)dx * scale * camRight - (float)dy * scale * camUp;

              mesh.pivot_offset[0] += deltaWorld.x;
              mesh.pivot_offset[1] += deltaWorld.y;
              mesh.pivot_offset[2] += deltaWorld.z;

              w.pivot_drag_start_screen_x = mouse_x;
              w.pivot_drag_start_screen_y = mouse_y;
            }
          } else {
            if (w.mouse_first_click) {
              w.prev_mouse_x = mouse_x;
              w.prev_mouse_y = mouse_y;
              w.mouse_first_click = false;
            }
            double delta_x = mouse_x - w.prev_mouse_x;
            double delta_y = mouse_y - w.prev_mouse_y;
            float sensitivity = 0.5f;

            if (glfwGetKey(w.glfw_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(w.glfw_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
              w.camera_roll += delta_x * sensitivity;
            } else {
              w.camera_yaw -= delta_x * sensitivity;
              w.camera_pitch += delta_y * sensitivity;
            }
            w.prev_mouse_x = mouse_x;
            w.prev_mouse_y = mouse_y;
          }
        } else {
          w.pivot_dragging = false;
          w.pivot_drag_mesh_index = -1;
          w.mouse_first_click = true;
        }

        // ---- Middle-click reset ----
        if (middle_button == GLFW_PRESS) {
          w.camera_yaw = 0.0f;
          w.camera_pitch = 89.999f;
          w.camera_distance = 3.5f;
          w.camera_roll = 0.0f;
        }
      }
    } // end for

    // Close windows that should close
    for (int i = (int)windows.size() - 1; i >= 0; --i) {
      if (glfwWindowShouldClose(windows[i].glfw_window)) {
        // System window close → quit the entire application
        gQuit = true;
        break; // Stop processing further windows; main loop will exit
      }
    }
  }
}

// ----------------------------------------------------------------------
//  GLOBAL FUNCTIONS (outside controller_window_input)
// ----------------------------------------------------------------------

void controller_sdl_events(SDL_Event *event) {
  if (event->type == SDL_CONTROLLERDEVICEADDED) {
    spdlog::info("Game controller added. Reopening...");
  }
  if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
    spdlog::warn("Controller removed.");
  }
  if (event->type == SDL_JOYDEVICEADDED) {
    spdlog::info("Joystick added.");
  }
  if (event->type == SDL_JOYDEVICEREMOVED) {
    spdlog::warn("Joystick removed.");
  }
  if (event->type == SDL_SENSORUPDATE) {
    for (auto &w : windows) {
      if (w.gyro_sensor &&
          event->sensor.which == SDL_SensorGetInstanceID(w.gyro_sensor)) {
        spdlog::debug("Gyro update: x={:.3f} y={:.3f} z={:.3f}",
                      event->sensor.data[0], event->sensor.data[1],
                      event->sensor.data[2]);
      }
      if (w.accel_sensor &&
          event->sensor.which == SDL_SensorGetInstanceID(w.accel_sensor)) {
        spdlog::debug("Accel update: x={:.3f} y={:.3f} z={:.3f}",
                      event->sensor.data[0], event->sensor.data[1],
                      event->sensor.data[2]);
      }
    }
  }
}

void controller_window_scroll_callback(GLFWwindow *window, double xoffset,
                                       double yoffset) {
  for (auto &w : windows) {
    if (w.glfw_window == window) {
      if (w.scroll_to_resize) {
        int ww = 0, hh = 0;
        glfwGetWindowSize(window, &ww, &hh);
        const GLFWvidmode *mode = get_vid_mode();
        if (yoffset > 0) {
          ww = (int)(ww * 1.05f);
          hh = (int)(hh * 1.05f);
          if (ww > mode->width)
            ww = mode->width;
          if (hh > mode->height)
            hh = mode->height;
        } else if (yoffset < 0) {
          ww = (int)(ww * 0.95f);
          hh = (int)(hh * 0.95f);
          if (ww < 10)
            ww = 10;
          if (hh < 10)
            hh = 10;
        }
        glfwSetWindowSize(window, ww, hh);
      } else {
        if (!w.freelook) {
          float zoom_speed = 0.2f;
          w.camera_distance -= yoffset * zoom_speed;
          if (w.camera_distance < 0.5f)
            w.camera_distance = 0.5f;
          if (w.camera_distance > 20.0f)
            w.camera_distance = 20.0f;
        }
      }
      break;
    }
  }
}

void createPivotCircle(controller_window &w) {
  if (w.pivot_vao)
    return;
  const int segments = w.pivot_segments;
  std::vector<float> verts;
  for (int i = 0; i <= segments; ++i) {
    float angle = 2.0f * 3.14159265f * (float)i / (float)segments;
    verts.push_back(0.1f * cosf(angle));
    verts.push_back(0.1f * sinf(angle));
    verts.push_back(0.0f);
  }
  glGenVertexArrays(1, &w.pivot_vao);
  glGenBuffers(1, &w.pivot_vbo);
  glBindVertexArray(w.pivot_vao);
  glBindBuffer(GL_ARRAY_BUFFER, w.pivot_vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(),
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

void make_grid(controller_window &w) {
  std::vector<glm::vec3> vertices;
  std::vector<glm::uvec4> indices;
  int slices = 100;
  for (int j = 0; j <= slices; ++j) {
    for (int i = 0; i <= slices; ++i) {
      float x = (float)i / (float)slices;
      float y = 0;
      float z = (float)j / (float)slices;
      vertices.push_back(glm::vec3(x, y, z));
    }
  }
  for (int j = 0; j < slices; ++j) {
    for (int i = 0; i < slices; ++i) {
      int row1 = j * (slices + 1);
      int row2 = (j + 1) * (slices + 1);
      indices.push_back(
          glm::uvec4(row1 + i, row1 + i + 1, row1 + i + 1, row2 + i + 1));
      indices.push_back(glm::uvec4(row2 + i + 1, row2 + i, row2 + i, row1 + i));
    }
  }
  glGenVertexArrays(1, &w.grid_vao);
  glBindVertexArray(w.grid_vao);
  glGenBuffers(1, &w.grid_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, w.grid_vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3),
               glm::value_ptr(vertices[0]), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  glGenBuffers(1, &w.grid_ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, w.grid_ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(glm::uvec4),
               glm::value_ptr(indices[0]), GL_STATIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  w.grid_length = (GLuint)indices.size() * 4;
}

void lightingSpecification(controller_window &w) {
  glGenVertexArrays(1, &w.lighting_vao);
  glGenBuffers(1, &w.lighting_vertex_data);
  glGenBuffers(1, &w.lighting_normal_data);
  glGenBuffers(1, &w.lighting_texture_data);
  glBindVertexArray(w.lighting_vao);
  glBindBuffer(GL_ARRAY_BUFFER, w.lighting_vertex_data);
  glBufferData(GL_ARRAY_BUFFER, CUBE_VERTICES_SIZE * sizeof(GLfloat),
               cube_vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, w.lighting_normal_data);
  glBufferData(GL_ARRAY_BUFFER, CUBE_NORMALS_SIZE * sizeof(GLfloat),
               cube_normals, GL_STATIC_DRAW);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, w.lighting_texture_data);
  glBufferData(GL_ARRAY_BUFFER, CUBE_TEX_COORDS_SIZE * sizeof(GLfloat),
               cube_tex_coords, GL_STATIC_DRAW);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
}

void createShader(GLuint &shader_id, const char *vs_source,
                  const char *fs_source) {
  shader_id = CreateShaderProgram(vs_source, fs_source);
}

void update_camera(controller_window &w, GLuint &shader, int window_width,
                   int window_height) {
  glUseProgram(shader);
  if (w.freelook) {
    w.freelook_direction.x =
        cos(glm::radians(w.freelook_pitch)) * sin(glm::radians(w.freelook_yaw));
    w.freelook_direction.y = sin(glm::radians(w.freelook_pitch));
    w.freelook_direction.z =
        cos(glm::radians(w.freelook_pitch)) * cos(glm::radians(w.freelook_yaw));
    glm::vec3 front = w.freelook_position + w.freelook_direction;
    w.view_matrix =
        glm::lookAt(w.freelook_position, front, glm::vec3(0.0f, 1.0f, 0.0f));
  } else {
    w.camera_position.x = cos(glm::radians(w.camera_pitch)) *
                          sin(glm::radians(w.camera_yaw)) * w.camera_distance;
    w.camera_position.y = sin(glm::radians(w.camera_pitch)) * w.camera_distance;
    w.camera_position.z = cos(glm::radians(w.camera_pitch)) *
                          cos(glm::radians(w.camera_yaw)) * w.camera_distance;
    glm::vec3 front =
        glm::normalize(glm::vec3(0.0, 0.0, 0.0) - w.camera_position);
    glm::vec3 right =
        glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::cross(right, front);
    glm::mat4 roll_mat = glm::mat4(1.0f);
    roll_mat = glm::rotate(roll_mat, glm::radians(w.camera_roll), front);
    up = glm::vec3(roll_mat * glm::vec4(up, 1.0));
    w.view_matrix = glm::lookAt(w.camera_position, front, up);
  }
  shaderUniformMat4(shader, "view", w.view_matrix);
  w.projection_matrix = glm::perspective(
      glm::radians(45.0f), (float)window_width / window_height, 0.1f, 100.0f);
  shaderUniformMat4(shader, "projection", w.projection_matrix);
  glUseProgram(0);
}

// drawControllerWindows() – keep your existing implementation, but make sure
// the call to drawModel passes the global press color. We'll modify that in
// Step 3.
void drawControllerWindows() {
  for (controller_window &w : windows) {
    if (glfwWindowShouldClose(w.glfw_window))
      continue;
    if (!glfwGetWindowAttrib(w.glfw_window, GLFW_ICONIFIED)) {
      glfwMakeContextCurrent(w.glfw_window);
      glfwSwapInterval(w.swap_interval);
      w.deltaTime = glfwGetTime() - w.lastTime;
      w.lastTime = glfwGetTime();

      int width = 0, height = 0;
      glfwGetWindowSize(w.glfw_window, &width, &height);
      glViewport(0, 0, width, height);

      update_camera(w, w.shader, width, height);
      update_camera(w, w.light_source_shader, width, height);
      update_camera(w, w.grid_shader, width, height);

      glEnable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      if (w.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

      glClearColor(w.bg_color[0] * w.bg_color[3], w.bg_color[1] * w.bg_color[3],
                   w.bg_color[2] * w.bg_color[3], 1.0f * w.bg_color[3]);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      // --- Draw grid ---
      if (w.grid) {
        glBindVertexArray(w.grid_vao);
        glUseProgram(w.grid_shader);
        glEnableVertexAttribArray(0);
        glm::mat4 grid_model = glm::mat4(1.0f);
        grid_model =
            glm::translate(grid_model, glm::vec3(-50.0f, 0.0f, -50.0f));
        grid_model = glm::scale(grid_model, glm::vec3(100.0f, 0.0f, 100.0f));
        shaderUniformMat4(w.grid_shader, "model", grid_model);
        shaderUniformVec3(w.grid_shader, "gridColor",
                          glm::vec3(0.5f, 0.5f, 0.5f));
        glDrawElements(GL_LINES, w.grid_length, GL_UNSIGNED_INT, NULL);
      }

      // --- Draw light sources ---
      glBindVertexArray(w.lighting_vao);
      glUseProgram(w.light_source_shader);
      for (point_light p : w.point_lights) {
        if (!p.hide) {
          shaderUniformVec3(w.light_source_shader, "lightColor",
                            glm::vec3(p.color[0], p.color[1], p.color[2]));
          glm::mat4 light_source_model = glm::mat4(1.0f);
          light_source_model = glm::translate(light_source_model, p.position);
          light_source_model = glm::scale(light_source_model, glm::vec3(0.2f));
          shaderUniformMat4(w.light_source_shader, "model", light_source_model);
          glDrawArrays(GL_TRIANGLES, 0, 36);
        }
      }
      for (spot_light s : w.spot_lights) {
        if (!s.hide) {
          shaderUniformVec3(w.light_source_shader, "lightColor",
                            glm::vec3(s.color[0], s.color[1], s.color[2]));
          glm::mat4 light_source_model = glm::mat4(1.0f);
          light_source_model = glm::translate(light_source_model, s.position);
          light_source_model =
              glm::rotate(light_source_model, glm::radians(s.pitch),
                          glm::vec3(1.0f, 0.0f, 0.0f));
          light_source_model =
              glm::rotate(light_source_model, glm::radians(s.yaw),
                          glm::vec3(0.0f, 1.0f, 0.0f));
          light_source_model =
              glm::scale(light_source_model, glm::vec3(0.1f, 0.1f, 0.3f));
          shaderUniformMat4(w.light_source_shader, "model", light_source_model);
          glDrawArrays(GL_TRIANGLES, 0, 36);
        }
      }

      // --- Draw main model ---
      glUseProgram(w.shader);

      if (w.freelook)
        shaderUniformVec3(w.shader, "viewPos", w.freelook_position);
      else
        shaderUniformVec3(w.shader, "viewPos", w.camera_position);

      shaderUniformFloat(w.shader, "time", glfwGetTime());

      // Direct lights
      shaderUniformInt(w.shader, "direct_lights", w.direct_lights.size());
      for (unsigned i = 0; i < w.direct_lights.size(); ++i) {
        std::string name = "dirLights[";
        name.append(std::to_string(i));
        name.append("]");
        shaderUniformVec3(w.shader,
                          std::string(name).append(".direction").c_str(),
                          w.direct_lights[i].direction);
        shaderUniformVec3(
            w.shader, std::string(name).append(".ambient").c_str(),
            glm::vec3(w.direct_lights[i].color[0] * w.direct_lights[i].ambient,
                      w.direct_lights[i].color[1] * w.direct_lights[i].ambient,
                      w.direct_lights[i].color[2] *
                          w.direct_lights[i].ambient));
        shaderUniformVec3(
            w.shader, std::string(name).append(".diffuse").c_str(),
            glm::vec3(w.direct_lights[i].color[0] * w.direct_lights[i].diffuse,
                      w.direct_lights[i].color[1] * w.direct_lights[i].diffuse,
                      w.direct_lights[i].color[2] *
                          w.direct_lights[i].diffuse));
        shaderUniformVec3(
            w.shader, std::string(name).append(".specular").c_str(),
            glm::vec3(w.direct_lights[i].color[0] * w.direct_lights[i].specular,
                      w.direct_lights[i].color[1] * w.direct_lights[i].specular,
                      w.direct_lights[i].color[2] *
                          w.direct_lights[i].specular));
      }

      // Point lights
      shaderUniformInt(w.shader, "point_lights", w.point_lights.size());
      for (unsigned i = 0; i < w.point_lights.size(); ++i) {
        std::string name = "pointLights[";
        name.append(std::to_string(i));
        name.append("]");
        shaderUniformFloat(
            w.shader, std::string(name).append(".constant").c_str(),
            w.point_lights[i].constant - w.point_lights[i].intensity);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".linear").c_str(),
                           w.point_lights[i].linear);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".quadratic").c_str(),
                           w.point_lights[i].quadratic);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".position").c_str(),
                          w.point_lights[i].position);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".ambient").c_str(),
                          w.point_lights[i].ambient);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".diffuse").c_str(),
                          w.point_lights[i].diffuse);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".specular").c_str(),
                          w.point_lights[i].specular);
      }

      // Spot lights
      shaderUniformInt(w.shader, "spot_lights", w.spot_lights.size());
      for (unsigned i = 0; i < w.spot_lights.size(); ++i) {
        std::string name = "spotLights[";
        name.append(std::to_string(i));
        name.append("]");
        shaderUniformVec3(w.shader,
                          std::string(name).append(".position").c_str(),
                          w.spot_lights[i].position);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".direction").c_str(),
                          w.spot_lights[i].direction);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".cutoff").c_str(),
                           glm::cos(glm::radians(w.spot_lights[i].cutoff)));
        shaderUniformFloat(
            w.shader, std::string(name).append(".outer_cutoff").c_str(),
            glm::cos(glm::radians(w.spot_lights[i].cutoff +
                                  (w.spot_lights[i].outer_cutoff * 0.2f))));
        shaderUniformFloat(
            w.shader, std::string(name).append(".constant").c_str(),
            w.spot_lights[i].constant - w.spot_lights[i].intensity);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".linear").c_str(),
                           w.spot_lights[i].linear);
        shaderUniformFloat(w.shader,
                           std::string(name).append(".quadratic").c_str(),
                           w.spot_lights[i].quadratic);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".ambient").c_str(),
                          w.spot_lights[i].ambient);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".diffuse").c_str(),
                          w.spot_lights[i].diffuse);
        shaderUniformVec3(w.shader,
                          std::string(name).append(".specular").c_str(),
                          w.spot_lights[i].specular);
      }

      // --- Draw model ---
      int highlight =
          w.is_import_preview ? w.import_preview.selected_mesh_index : -1;
      if (w.is_import_preview) {
        if (highlight != w.last_highlight_index) {
          spdlog::debug("Preview highlight index: {}", highlight);
          w.last_highlight_index = highlight;
        }
      }
      w.model.motion_matrix = w.gyro_matrix;

      // Decay glow for touch points, but only if they are not mapped
      float decay = 1.0f - w.deltaTime * 1.2f;
      if (decay < 0.0f)
        decay = 0.0f;

      for (auto &mesh : w.model.meshes) {
        int part = mesh.assignedPart;
        if (mesh.touch_state > 0) {
          // Apply decay to all touch points (both bound and unbound)
          mesh.glow_intensity *= decay;
          if (mesh.glow_intensity < 0.001f)
            mesh.glow_intensity = 0.0f;
          // Force white glow material so the glow texture shows correctly
          mesh.material.color[0] = 1.0f;
          mesh.material.color[1] = 1.0f;
          mesh.material.color[2] = 1.0f;
          mesh.material.alpha = mesh.glow_intensity;
          mesh.visible = (mesh.glow_intensity > 0.001f);
        }
      }

      // Pass global press color to drawModel
      glm::vec3 globalPressColor =
          glm::vec3(w.global_press_color[0], w.global_press_color[1],
                    w.global_press_color[2]);
      drawModel(w.model, w.shader, highlight, globalPressColor);

      // --- Draw pivot circle ---
      if (selected_tab < tabs.size()) {
        unsigned activeID = tabs[selected_tab].ID;
        if (w.ID == activeID && selected_mesh >= 0 &&
            selected_mesh < (int)w.model.meshes.size()) {
          const Mesh &mesh = w.model.meshes[selected_mesh];
          if (mesh.elements > 0) {
            glm::mat4 pivotMat =
                getMeshFinalMatrix(w.model, selected_mesh, w.gyro_matrix);
            glm::vec3 pivotPos = glm::vec3(pivotMat[3]);
            createPivotCircle(w);
            glUseProgram(w.grid_shader);
            shaderUniformMat4(
                w.grid_shader, "model",
                glm::translate(glm::mat4(1.0f), pivotPos) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 0.1f)));
            shaderUniformVec3(w.grid_shader, "gridColor",
                              glm::vec3(1.0f, 0.6f, 0.0f));
            glBindVertexArray(w.pivot_vao);
            glDrawArrays(GL_LINE_LOOP, 0, w.pivot_segments + 1);
            glBindVertexArray(0);
            glUseProgram(0);
          }
        }
      }

      // --- Draw touch area (optional) ---
      if (w.show_touch_area) {
        createTouchAreaRect(w);
        glUseProgram(w.grid_shader);
        // Set alpha for touch area (50% transparency)
        shaderUniformFloat(w.grid_shader, "alpha", 0.5f);
        for (int i = 0; i < (int)w.model.meshes.size(); ++i) {
          const Mesh &touchpad = w.model.meshes[i];
          if (!touchpad.isTouchpad || touchpad.elements == 0)
            continue;
          float tw = touchpad.touch_width;
          float th = touchpad.touch_height;
          if (tw < 0.01f)
            tw = 1.0f;
          if (th < 0.01f)
            th = 1.0f;

          // Get touchpad's world matrix (without gyro, but includes its own
          // transform and parents)
          glm::mat4 touchpadWorld = getModelMatrixWithoutGyro(w.model, i);
          // Apply touch offset and rotation
          glm::mat4 rectModel = touchpadWorld;
          rectModel =
              glm::translate(rectModel, glm::vec3(touchpad.touch_offset[0],
                                                  touchpad.touch_offset[1],
                                                  touchpad.touch_offset[2]));
          rectModel =
              glm::rotate(rectModel, glm::radians(touchpad.touch_rotation[1]),
                          glm::vec3(0, 1, 0)); // yaw
          rectModel =
              glm::rotate(rectModel, glm::radians(touchpad.touch_rotation[0]),
                          glm::vec3(1, 0, 0)); // pitch
          rectModel =
              glm::rotate(rectModel, glm::radians(touchpad.touch_rotation[2]),
                          glm::vec3(0, 0, 1)); // roll
          rectModel = glm::scale(rectModel, glm::vec3(tw, 0.02f, th));

          shaderUniformMat4(w.grid_shader, "model", rectModel);
          shaderUniformVec3(w.grid_shader, "gridColor",
                            glm::vec3(1.0f, 0.0f, 1.0f));

          glBindVertexArray(w.touch_area_vao);
          // Draw filled rectangle with transparency
          glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
          // Draw outline (opaque)
          glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
          shaderUniformFloat(w.grid_shader, "alpha", 1.0f); // outline opaque
          glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
          glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
          glBindVertexArray(0);
          // Reset alpha to 0.5 for next rectangle
          shaderUniformFloat(w.grid_shader, "alpha", 0.5f);
        }
        // Reset alpha for other grid elements (e.g., normal grid, pivot circle)
        shaderUniformFloat(w.grid_shader, "alpha", 1.0f);
        glUseProgram(0);
      }

      glUseProgram(0);
      glfwSwapBuffers(w.glfw_window);

      // ---- MEGA DEBUG TOUCH RECT (optional, you can keep or remove) ----
      // ...
    }
  }
}

void destroyWindows() {
  for (controller_window w : windows) {
    glfwDestroyWindow(w.glfw_window);
  }
}

void removeControllerWindow(unsigned ID) {
  for (unsigned i = 0; i < windows.size(); ++i) {
    if (windows[i].ID == ID) {
      glfwDestroyWindow(windows[i].glfw_window);
      windows.erase(windows.begin() + i);
      break;
    }
  }
}

controller_window *getControllerWindow(unsigned ID) {
  for (unsigned i = 0; i < windows.size(); ++i) {
    if (windows[i].ID == ID) {
      return &windows[i];
    }
  }
  return nullptr;
}

controller_window *getLastWindow() { return &windows.back(); }