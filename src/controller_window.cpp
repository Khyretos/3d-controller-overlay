#include "controller_window.h"
#include "shader.h"
#include "cube_info.h"
#include "settings_window.h"
#include "shaders.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

extern bool g_log_buttons;

void createTouchAreaRect(controller_window &w) {
  if (!w.touch_area_vao) {
    float vertices[] = {-0.5f, -0.5f, 0.0f, 0.5f,  -0.5f, 0.0f,
                        0.5f,  0.5f,  0.0f, -0.5f, 0.5f,  0.0f};
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

void generateTouchAreaMesh(controller_window &w) {
  // Safety: ensure we have at least 33 meshes
  if (w.model.meshes.size() <= 32) {
    spdlog::warn("generateTouchAreaMesh: vector size {} < 33, resizing.",
                 w.model.meshes.size());
    w.model.meshes.resize(33);
  }
  Mesh &mesh = w.model.meshes[32];
  if (mesh.elements > 0)
    return;

  // Unit quad (same as before)
  float verts[] = {-0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                   0.5f,  0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                   0.5f,  0.0f, 0.5f,  0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                   -0.5f, 0.0f, 0.5f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  unsigned int indices[] = {0, 1, 2, 0, 2, 3};
  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glGenBuffers(1, &mesh.ebo);
  glBindVertexArray(mesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  mesh.elements = 6;
  glBindVertexArray(0);

  // Material: bright magenta
  mesh.material.color[0] = 1.0f;
  mesh.material.color[1] = 0.0f;
  mesh.material.color[2] = 1.0f;
  mesh.material.alpha = 0.5f;
  mesh.material.ambient = 1.0f;
  mesh.material.diffuse = 1.0f;
  mesh.material.specular = 0.0f;
  mesh.material.shininess = 1.0f;
  mesh.visible = true;
  mesh.highlight_value = 0.0f;
  mesh.parentIndex = 29; // parent to touchpad
  mesh.useCustomScale = true;
  mesh.scale[0] = 1.0f;
  mesh.scale[1] = 1.0f;
  mesh.scale[2] = 1.0f;
  // Position offset: we'll set position relative to touchpad in draw loop
  mesh.position[0] = 0.0f;
  mesh.position[1] = 0.1f; // offset above touchpad
  mesh.position[2] = 0.0f;
  mesh.pivot_offset[0] = 0.0f;
  mesh.pivot_offset[1] = 0.0f;
  mesh.pivot_offset[2] = 0.0f;
  mesh.rotation[0] = 0.0f;
  mesh.rotation[1] = 0.0f;
  mesh.rotation[2] = 0.0f;
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

// Helper: get axis value (works for gamecontroller or joystick)
float get_axis_value(controller_window &w, int axis_idx) {
  if (w.is_gamecontroller && w.sdl_controller) {
    // Standard gamecontroller axes: 0..5
    if (axis_idx >= 0 && axis_idx < 6) {
      return SDL_GameControllerGetAxis(w.sdl_controller,
                                       (SDL_GameControllerAxis)axis_idx) /
             32767.0f;
    } else {
      // Extra axes: use the underlying joystick
      SDL_Joystick *joy = SDL_GameControllerGetJoystick(w.sdl_controller);
      if (joy && axis_idx < SDL_JoystickNumAxes(joy)) {
        return SDL_JoystickGetAxis(joy, axis_idx) / 32767.0f;
      }
    }
  } else if (w.sdl_joystick) {
    // Generic joystick
    return SDL_JoystickGetAxis(w.sdl_joystick, axis_idx) / 32767.0f;
  }
  return 0.0f;
}

// Helper: get button value
bool get_button_value(controller_window &w, int btn_idx) {
  if (w.is_gamecontroller) {
    return SDL_GameControllerGetButton(w.sdl_controller,
                                       (SDL_GameControllerButton)btn_idx);
  } else {
    return SDL_JoystickGetButton(w.sdl_joystick, btn_idx);
  }
}

void createControllerWindow(std::string title, std::string model_path) {
  controller_window w;
  w.gyro_sensitivity = 5.0f;
  w.logger = spdlog::get("3dco"); // reuse global logger

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

  // Ensure we have at least 33 meshes (index 32 for touch area)
  if (w.model.meshes.size() < 33) {
    w.model.meshes.resize(33);
  }
  generateTouchAreaMesh(w);

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
        w.is_gamecontroller = false;
        w.joystick_index = chosen;
        if (w.sdl_joystick) {
          spdlog::info("Opened generic joystick: {}",
                       SDL_JoystickName(w.sdl_joystick));
          // Try to open sensors for this joystick (by matching device index)
          int num_sensors = SDL_NumSensors();
          for (int s = 0; s < num_sensors; ++s) {
            if (SDL_SensorGetDeviceType(s) == SDL_SENSOR_GYRO) {
              w.gyro_sensor = SDL_SensorOpen(s);
              if (w.gyro_sensor) {
                spdlog::info("Gyro sensor opened (index {})", s);
                w.gyro_enabled = true;
                break;
              }
            }
          }
          for (int s = 0; s < num_sensors; ++s) {
            if (SDL_SensorGetDeviceType(s) == SDL_SENSOR_ACCEL) {
              w.accel_sensor = SDL_SensorOpen(s);
              if (w.accel_sensor) {
                spdlog::info("Accel sensor opened (index {})", s);
                break;
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
  // -------------------------------------------------

  windows.push_back(w);

  // Initialize mapping to empty strings
  for (int i = 0; i < 31; ++i) {
    windows.back().mapping[i] = "";
  }
}

void applyMappingToMeshes(controller_window &w) {
  // Helper to read axis values (including extra axes via raw joystick)
  auto getAxisValue = [&](int axisIdx) -> float {
    return get_axis_value(w, axisIdx);
  };

  auto getButtonValue = [&](int btnIdx) -> bool {
    if (w.is_gamecontroller && w.sdl_controller) {
      return SDL_GameControllerGetButton(w.sdl_controller,
                                         (SDL_GameControllerButton)btnIdx);
    } else if (w.sdl_joystick) {
      return SDL_JoystickGetButton(w.sdl_joystick, btnIdx);
    }
    return false;
  };

  auto getHatValue = [&](int hatIdx) -> Uint8 {
    if (w.sdl_joystick) {
      return SDL_JoystickGetHat(w.sdl_joystick, hatIdx);
    }
    return SDL_HAT_CENTERED;
  };

  // Map input index to target mesh and type
  struct Target {
    int meshIdx;
    enum Type { BUTTON, AXIS_X, AXIS_Y, TRIGGER, TOUCH_X, TOUCH_Y } type;
  };

  Target targets[31];
  for (int i = 0; i < 31; ++i) {
    Target t;
    t.meshIdx = -1;
    t.type = Target::BUTTON;
    if (i < 7) {
      t.meshIdx = 9 + i; // a=9, b=10, x=11, y=12, back=13, guide=14, start=15
    } else if (i == 7) {
      t.meshIdx = 5; // left stick click
    } else if (i == 8) {
      t.meshIdx = 6; // right stick click
    } else if (i >= 9 && i <= 14) {
      t.meshIdx = 18 + (i - 9); // bumpers and dpad
    } else if (i == 15) {
      t.meshIdx = 29; // touchpad click
    } else if (i >= 16 && i <= 20) {
      t.meshIdx = 24 + (i - 16); // misc, paddle1-4
    } else if (i == 21 || i == 22) {
      t.meshIdx = 5; // left stick axes
      t.type = (i == 21) ? Target::AXIS_X : Target::AXIS_Y;
    } else if (i == 23 || i == 24) {
      t.meshIdx = 6; // right stick axes
      t.type = (i == 23) ? Target::AXIS_X : Target::AXIS_Y;
    } else if (i == 25) {
      t.meshIdx = 3; // left trigger
      t.type = Target::TRIGGER;
    } else if (i == 26) {
      t.meshIdx = 4; // right trigger
      t.type = Target::TRIGGER;
    } else if (i == 27) {
      t.meshIdx = 30; // touch point 1
      t.type = Target::TOUCH_X;
    } else if (i == 28) {
      t.meshIdx = 30; // touch point 1
      t.type = Target::TOUCH_Y;
    } else if (i == 29) {
      t.meshIdx = 31; // touch point 2
      t.type = Target::TOUCH_X;
    } else if (i == 30) {
      t.meshIdx = 31; // touch point 2
      t.type = Target::TOUCH_Y;
    }
    targets[i] = t;
  }

  // Process each input (0..30)
  for (int i = 0; i < 31; ++i) {
    Target &target = targets[i];
    if (target.meshIdx < 0 || target.meshIdx >= (int)w.model.meshes.size())
      continue;

    Mesh &mesh = w.model.meshes[target.meshIdx];

    // ---- If no binding is set, use defaults for touchpoints ----
    std::string binding = w.mapping[i];
    if (binding.empty()) {
      // For touchpoint meshes (30 and 31), default to touchpad 0 data
      if (i >= 27 && i <= 30) {
        int touchpad_idx = 0;
        int finger_idx = (i == 27 || i == 28) ? 0 : 1;
        auto &ts = w.touchpad_data[touchpad_idx][finger_idx];
        if (target.type == Target::TOUCH_X) {
          mesh.touch_X = ts.x;
        } else if (target.type == Target::TOUCH_Y) {
          mesh.touch_Y = ts.y;
        }
        bool touching = (ts.state == 1);
        mesh.highlight_value = touching ? 1.0f : 0.0f;
        mesh.touch_state = touching ? 1 : 0;
        mesh.visible = true;
        // Ensure highlight color is set
        mesh.material.highlight[0] = w.highlight_color[0];
        mesh.material.highlight[1] = w.highlight_color[1];
        mesh.material.highlight[2] = w.highlight_color[2];
      }
      // No binding and not a touchpoint – do nothing
      continue;
    }

    // ---- Parse binding ----
    char type = binding[0];
    int num = 0;
    int hatDir = -1;
    bool isDirection = false;
    int dir = 0;

    if (type == 'b') {
      num = std::stoi(binding.substr(1));
    } else if (type == 'a') {
      if (binding.back() == '+') {
        isDirection = true;
        dir = 1;
        num = std::stoi(binding.substr(1, binding.size() - 2));
      } else if (binding.back() == '-') {
        isDirection = true;
        dir = -1;
        num = std::stoi(binding.substr(1, binding.size() - 2));
      } else {
        num = std::stoi(binding.substr(1));
      }
    } else if (type == 'h') {
      size_t dot = binding.find('.');
      if (dot != std::string::npos) {
        num = std::stoi(binding.substr(1, dot - 1));
        hatDir = std::stoi(binding.substr(dot + 1));
      } else {
        continue;
      }
    } else if (type == 't') {
      // Parse tX_fY_z
      size_t pos1 = binding.find('_');
      if (pos1 == std::string::npos)
        continue;
      size_t pos2 = binding.find('_', pos1 + 1);
      if (pos2 == std::string::npos)
        continue;
      std::string touchStr = binding.substr(1, pos1 - 1);
      std::string fingerStr = binding.substr(pos1 + 1, pos2 - pos1 - 1);
      char axisChar = binding.back();
      int touchpadIdx = std::stoi(touchStr);
      int fingerIdx = std::stoi(fingerStr.substr(1));
      if (touchpadIdx < 0 || touchpadIdx >= 4 || fingerIdx < 0 ||
          fingerIdx >= 2)
        continue;

      // Get value from stored touchpad data
      float val = 0.0f;
      if (axisChar == 'x')
        val = w.touchpad_data[touchpadIdx][fingerIdx].x;
      else if (axisChar == 'y')
        val = w.touchpad_data[touchpadIdx][fingerIdx].y;
      else
        continue;

      // Apply to target based on its type
      if (target.type == Target::AXIS_X || target.type == Target::TOUCH_X) {
        if (target.type == Target::TOUCH_X)
          mesh.touch_X = val;
        else
          mesh.stick_X = val * 32767.0f;
        // propagate to ring/cap for sticks
        if (target.meshIdx == 5) {
          w.model.meshes[7].stick_X = mesh.stick_X;
          w.model.meshes[16].stick_X = mesh.stick_X;
          w.model.meshes[7].highlight_value = fabs(val) * 1.2f;
        } else if (target.meshIdx == 6) {
          w.model.meshes[8].stick_X = mesh.stick_X;
          w.model.meshes[17].stick_X = mesh.stick_X;
          w.model.meshes[8].highlight_value = fabs(val) * 1.2f;
        }
      } else if (target.type == Target::AXIS_Y ||
                 target.type == Target::TOUCH_Y) {
        if (target.type == Target::TOUCH_Y)
          mesh.touch_Y = val;
        else
          mesh.stick_Y = val * 32767.0f;
        if (target.meshIdx == 5) {
          w.model.meshes[7].stick_Y = mesh.stick_Y;
          w.model.meshes[16].stick_Y = mesh.stick_Y;
          w.model.meshes[7].highlight_value = fabs(val) * 1.2f;
        } else if (target.meshIdx == 6) {
          w.model.meshes[8].stick_Y = mesh.stick_Y;
          w.model.meshes[17].stick_Y = mesh.stick_Y;
          w.model.meshes[8].highlight_value = fabs(val) * 1.2f;
        }
      } else if (target.type == Target::TRIGGER) {
        float triggerVal = (val > 0.0f) ? val : 0.0f;
        mesh.pull = triggerVal * 32767.0f;
        mesh.press = triggerVal;
        mesh.highlight_value = triggerVal;
      } else if (target.type == Target::BUTTON) {
        bool pressed = fabs(val) > 0.5f;
        mesh.press = pressed ? 1.0f : 0.0f;
        mesh.highlight_value = pressed ? 1.0f : 0.0f;
      }
      // After processing touchpad binding, skip the rest of the loop

      // Set highlight color for touchpoint meshes
      mesh.material.highlight[0] = w.highlight_color[0];
      mesh.material.highlight[1] = w.highlight_color[1];
      mesh.material.highlight[2] = w.highlight_color[2];

      continue;
    } else {
      continue;
    }

    // ---- Button-like inputs (b, h, axis direction) ----
    if (type == 'b' || type == 'h' || (type == 'a' && isDirection)) {
      bool pressed = false;
      float axisVal = 0.0f;
      if (type == 'b') {
        pressed = getButtonValue(num);
      } else if (type == 'h') {
        Uint8 hatVal = getHatValue(num);
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
        pressed = (hatVal & sdlDir) != 0;
      } else if (type == 'a' && isDirection) {
        float val = getAxisValue(num);
        float threshold = 0.5f;
        pressed = (dir > 0) ? (val > threshold) : (val < -threshold);
        axisVal = (dir > 0) ? val : -val;
      }

      if (target.type == Target::BUTTON || target.type == Target::TOUCH_X ||
          target.type == Target::TOUCH_Y) {
        float pressVal = pressed ? 1.0f : 0.0f;
        if (target.type == Target::TOUCH_X) {
          // For touchpoint, button press sets touch_X to a fixed position? Or
          // just highlight. We'll set highlight and touch_state.
          mesh.highlight_value = pressVal;
          mesh.touch_state = pressed ? 1 : 0;
          // Optionally set touch_X/Y to center or something? We'll leave as is.
        } else if (target.type == Target::TOUCH_Y) {
          mesh.highlight_value = pressVal;
          mesh.touch_state = pressed ? 1 : 0;
        } else {
          mesh.press = pressVal;
          mesh.highlight_value = pressVal;
        }
      } else if (target.type == Target::AXIS_X ||
                 target.type == Target::AXIS_Y) {
        float value = 0.0f;
        if (type == 'a' && isDirection) {
          value = axisVal;
        } else {
          value = pressed ? 1.0f : 0.0f;
        }
        if (target.type == Target::AXIS_X) {
          mesh.stick_X = (dir > 0) ? value * 32767.0f : -value * 32767.0f;
        } else {
          mesh.stick_Y = (dir > 0) ? value * 32767.0f : -value * 32767.0f;
        }
        mesh.highlight_value = value;
        if (target.meshIdx == 5) {
          w.model.meshes[7].stick_X = mesh.stick_X;
          w.model.meshes[7].stick_Y = mesh.stick_Y;
          w.model.meshes[16].stick_X = mesh.stick_X;
          w.model.meshes[16].stick_Y = mesh.stick_Y;
          w.model.meshes[7].highlight_value = value * 1.2f;
        } else if (target.meshIdx == 6) {
          w.model.meshes[8].stick_X = mesh.stick_X;
          w.model.meshes[8].stick_Y = mesh.stick_Y;
          w.model.meshes[17].stick_X = mesh.stick_X;
          w.model.meshes[17].stick_Y = mesh.stick_Y;
          w.model.meshes[8].highlight_value = value * 1.2f;
        }
      } else if (target.type == Target::TRIGGER) {
        float value = 0.0f;
        if (type == 'a' && isDirection) {
          value = axisVal;
        } else {
          value = pressed ? 1.0f : 0.0f;
        }
        mesh.pull = value * 32767.0f;
        mesh.press = value;
        mesh.highlight_value = value;
      }
    }
    // ---- Full axis mapping (no direction) ----
    else if (type == 'a' && !isDirection) {
      float val = getAxisValue(num);
      if (target.type == Target::AXIS_X) {
        mesh.stick_X = val * 32767.0f;
        if (target.meshIdx == 5) {
          w.model.meshes[7].stick_X = mesh.stick_X;
          w.model.meshes[16].stick_X = mesh.stick_X;
          float mag = fabs(val);
          w.model.meshes[7].highlight_value = mag * 1.2f;
        } else if (target.meshIdx == 6) {
          w.model.meshes[8].stick_X = mesh.stick_X;
          w.model.meshes[17].stick_X = mesh.stick_X;
          float mag = fabs(val);
          w.model.meshes[8].highlight_value = mag * 1.2f;
        }
      } else if (target.type == Target::AXIS_Y) {
        mesh.stick_Y = val * 32767.0f;
        if (target.meshIdx == 5) {
          w.model.meshes[7].stick_Y = mesh.stick_Y;
          w.model.meshes[16].stick_Y = mesh.stick_Y;
          float mag = fabs(val);
          w.model.meshes[7].highlight_value = mag * 1.2f;
        } else if (target.meshIdx == 6) {
          w.model.meshes[8].stick_Y = mesh.stick_Y;
          w.model.meshes[17].stick_Y = mesh.stick_Y;
          float mag = fabs(val);
          w.model.meshes[8].highlight_value = mag * 1.2f;
        }
      } else if (target.type == Target::TRIGGER) {
        float triggerVal = (val > 0.0f) ? val : 0.0f;
        mesh.pull = triggerVal * 32767.0f;
        mesh.press = triggerVal;
        mesh.highlight_value = triggerVal;
      }
    }
  }
}

void controller_window_input() {
  SDL_PumpEvents();

  for (auto &w : windows) {
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
              if (isnan(w.gyro_data[0]) || isnan(w.gyro_data[1]) ||
                  isnan(w.gyro_data[2])) {
                if (w.gyro_debug_logging) {
                  spdlog::warn("Gyro data contains NaN, skipping frame");
                }
                continue;
              }
              if (fabs(w.gyro_data[0]) < 1e-6f &&
                  fabs(w.gyro_data[1]) < 1e-6f &&
                  fabs(w.gyro_data[2]) < 1e-6f) {
                continue;
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
                w.gyro_matrix[3][0] = 0.0f;
                w.gyro_matrix[3][1] = 0.0f;
                w.gyro_matrix[3][2] = 0.0f;
                w.gyro_matrix[3][3] = 1.0f;
                w.gyro_time = timestamp;

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

                if (w.reset_gyro_button1 >= 0 && w.reset_gyro_button2 >= 0) {
                  if (get_button_value(w, w.reset_gyro_button1) &&
                      get_button_value(w, w.reset_gyro_button2)) {
                    w.gyro_matrix = glm::mat4(1.0f);
                    if (w.gyro_debug_logging) {
                      spdlog::debug("Gyro reset via button combo");
                    }
                  }
                }

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
                spdlog::debug(
                    "Failed to read gyro data (maybe sensor not ready)");
              }
            }
          } else if (w.gyro_sensor) {
            has_gyro_source = true;
          }

          if (!has_gyro_source) {
            w.gyro_enabled = false;
            w.gyro_debug_logging = false;
            if (w.gyro_debug_logging) {
              spdlog::debug("No gyro source available; disabling gyro.");
            }
          }
        }

        // --- AXES ---
        float lx = get_axis_value(w, 0);
        float ly = get_axis_value(w, 1);
        w.model.meshes[5].stick_X = lx * 32767.0f;
        w.model.meshes[5].stick_Y = ly * 32767.0f;
        w.model.meshes[7].stick_X = lx * 32767.0f;
        w.model.meshes[7].stick_Y = ly * 32767.0f;
        w.model.meshes[16].stick_X = lx * 32767.0f;
        w.model.meshes[16].stick_Y = ly * 32767.0f;
        if (fabs(lx) > w.model.meshes[7].ring_highlight_deadzone * 0.01f ||
            fabs(ly) > w.model.meshes[7].ring_highlight_deadzone * 0.01f) {
          w.model.meshes[7].highlight_value =
              std::max(fabs(lx), fabs(ly)) * 1.2f;
        } else {
          w.model.meshes[7].highlight_value = 0.0f;
        }

        float rx = get_axis_value(w, 2);
        float ry = get_axis_value(w, 3);
        w.model.meshes[6].stick_X = rx * 32767.0f;
        w.model.meshes[6].stick_Y = ry * 32767.0f;
        w.model.meshes[8].stick_X = rx * 32767.0f;
        w.model.meshes[8].stick_Y = ry * 32767.0f;
        w.model.meshes[17].stick_X = rx * 32767.0f;
        w.model.meshes[17].stick_Y = ry * 32767.0f;
        if (fabs(rx) > w.model.meshes[8].ring_highlight_deadzone * 0.01f ||
            fabs(ry) > w.model.meshes[8].ring_highlight_deadzone * 0.01f) {
          w.model.meshes[8].highlight_value =
              std::max(fabs(rx), fabs(ry)) * 1.2f;
        } else {
          w.model.meshes[8].highlight_value = 0.0f;
        }

        float lt = get_axis_value(w, 4);
        float rt = get_axis_value(w, 5);
        w.model.meshes[3].pull = lt * 32767.0f;
        w.model.meshes[3].highlight_value = lt;
        w.model.meshes[3].press = lt;
        w.model.meshes[4].pull = rt * 32767.0f;
        w.model.meshes[4].highlight_value = rt;
        w.model.meshes[4].press = rt;

        for (int b = 0; b < 21; ++b) {
          bool pressed = get_button_value(w, b);
          w.model.meshes[9 + b].press = pressed ? 1.0f : 0.0f;
          w.model.meshes[9 + b].highlight_value = pressed ? 1.0f : 0.0f;
        }

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

        // --- LOGGING ---
        if (g_log_buttons) {
          if (w.is_gamecontroller && w.sdl_controller) {
            SDL_Joystick *joy = SDL_GameControllerGetJoystick(w.sdl_controller);
            if (joy) {
              // ---- Log all axes (unchanged) ----
              int numAxes = SDL_JoystickNumAxes(joy);
              for (int i = 0; i < numAxes; ++i) {
                float val = get_axis_value(w, i);
                std::string label;
                if (i < 6) {
                  switch (i) {
                  case 0:
                    label = "Left X";
                    break;
                  case 1:
                    label = "Left Y";
                    break;
                  case 2:
                    label = "Right X";
                    break;
                  case 3:
                    label = "Right Y";
                    break;
                  case 4:
                    label = "Left Trigger";
                    break;
                  case 5:
                    label = "Right Trigger";
                    break;
                  }
                } else {
                  label = "Axis " + std::to_string(i);
                }
                logAxisChange(w, i, val, label);
              }

              // ---- Log ALL raw joystick buttons (extra ones like grip, second
              // touchpad click) ----
              int numJoyButtons = SDL_JoystickNumButtons(joy);
              for (int b = 0; b < numJoyButtons; ++b) {
                bool pressed = SDL_JoystickGetButton(joy, b);
                if (pressed && !w.last_joy_button_values[b]) {
                  spdlog::info("[b{}] Joystick Button {} pressed", b, b);
                }
                w.last_joy_button_values[b] = pressed;
              }

              // ---- Log gamecontroller buttons (with friendly names) ----
              for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                bool pressed = SDL_GameControllerGetButton(
                    w.sdl_controller, (SDL_GameControllerButton)b);
                if (pressed && !w.last_button_values[b]) {
                  std::string name;
                  if (b >= 0 && b < 21) {
                    name = button_names[b];
                  } else {
                    name = "Button " + std::to_string(b);
                  }
                  spdlog::info("[b{}] {} pressed", b, name);
                }
                w.last_button_values[b] = pressed;
              }

              // ---- NEW: Log touchpad fingers (helps see both touchpads) ----
              int numTouchpads =
                  SDL_GameControllerGetNumTouchpads(w.sdl_controller);
              for (int t = 0; t < numTouchpads; ++t) {
                int numFingers = SDL_GameControllerGetNumTouchpadFingers(
                    w.sdl_controller, t);
                for (int f = 0; f < numFingers; ++f) {
                  Uint8 state;
                  float x, y;
                  if (SDL_GameControllerGetTouchpadFinger(w.sdl_controller, t,
                                                          f, &state, &x, &y,
                                                          nullptr) == 0) {
                    if (state == 1) { // finger down
                      spdlog::info(
                          "Touchpad {} finger {} down at ({:.3f}, {:.3f})", t,
                          f, x, y);
                    } else if (state == 2) { // finger up
                      spdlog::info("Touchpad {} finger {} up", t, f);
                    }
                    // (state 0 = finger not touching)
                  }
                }
              }
            }
          } else if (!w.is_gamecontroller && w.sdl_joystick) {
            // ---- Generic joystick logging (also uses rising‑edge for buttons)
            // ----
            int numButtons = SDL_JoystickNumButtons(w.sdl_joystick);
            for (int i = 0; i < numButtons; ++i) {
              bool pressed = SDL_JoystickGetButton(w.sdl_joystick, i);
              if (pressed && !w.last_joy_button_values[i]) {
                spdlog::info("[b{}] Generic Button {} pressed", i, i);
              }
              w.last_joy_button_values[i] = pressed;
            }

            // Log axes (unchanged)
            int numAxes = SDL_JoystickNumAxes(w.sdl_joystick);
            for (int i = 0; i < numAxes; ++i) {
              float val = SDL_JoystickGetAxis(w.sdl_joystick, i) / 32767.0f;
              std::string label = "Generic Axis " + std::to_string(i);
              logAxisChange(w, i, val, label);
            }

            // Log hats (unchanged)
            int numHats = SDL_JoystickNumHats(w.sdl_joystick);
            for (int i = 0; i < numHats; ++i) {
              Uint8 hatVal = SDL_JoystickGetHat(w.sdl_joystick, i);
              logHatChange(w, i, hatVal);
            }
          }
        }

        // --- APPLY CUSTOM MAPPING ---
        applyMappingToMeshes(w);
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

      if (left_button == GLFW_PRESS && !w.drag_to_move) {
        if (w.mouse_first_click) {
          w.prev_mouse_x = mouse_x;
          w.prev_mouse_y = mouse_y;
          w.mouse_first_click = false;
        }
        double delta_x = mouse_x - w.prev_mouse_x;
        double delta_y = mouse_y - w.prev_mouse_y;
        float sensitivity = 0.5f;

        // Shift+Left drag -> roll
        if (glfwGetKey(w.glfw_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(w.glfw_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
          w.camera_roll += delta_x * sensitivity;
        } else {
          // Normal orbit: yaw and pitch (unlimited)
          w.camera_yaw -= delta_x * sensitivity;
          w.camera_pitch += delta_y * sensitivity;
          // No pitch clamping - allow full rotation
        }
        w.prev_mouse_x = mouse_x;
        w.prev_mouse_y = mouse_y;
      } else {
        w.mouse_first_click = true;
      }

      // Middle-click to reset (keep as is)
      if (middle_button == GLFW_PRESS) {
        w.camera_yaw = 0.0f;
        w.camera_pitch = 0.0f; // reset to 0, not 89.999
        w.camera_distance = 3.5f;
        w.camera_roll = 0.0f;
      }
    }
  } // end for

  // Close windows that should close
  for (int i = (int)windows.size() - 1; i >= 0; --i) {
    if (glfwWindowShouldClose(windows[i].glfw_window)) {
      unsigned id = windows[i].ID;
      glfwDestroyWindow(windows[i].glfw_window);
      windows.erase(windows.begin() + i);
      removeTab(id);
    }
  }
}

// controller_sdl_events() – add sensor events and device added/removed
void controller_sdl_events(SDL_Event *event) {
  if (event->type == SDL_CONTROLLERDEVICEADDED) {
    spdlog::info("Game controller added. Reopening...");
    // Reopen the first controller (or we could update all windows)
    // For simplicity, we'll just log.
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

  // Sensor events
  if (event->type == SDL_SENSORUPDATE) {
    for (auto &w : windows) {
      if (w.gyro_sensor &&
          event->sensor.which == SDL_SensorGetInstanceID(w.gyro_sensor)) {
        // Update gyro data from sensor event
        // event->sensor.data contains float[3]
        // We could integrate this with gyro_matrix update here, but we're
        // already polling. We'll just store for debug.
        spdlog::debug("Gyro update: x={:.3f} y={:.3f} z={:.3f}",
                      event->sensor.data[0], event->sensor.data[1],
                      event->sensor.data[2]);
        // Optionally update gyro_matrix here.
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

// ----------------------------------------------------------------------
// Missing definitions
// ----------------------------------------------------------------------

void controller_window_scroll_callback(GLFWwindow *window, double xoffset,
                                       double yoffset) {
  for (auto &w : windows) {
    if (w.glfw_window == window) {
      if (w.scroll_to_resize) {
        // Resize window (original behavior)
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
        // Zoom camera (if not freelook)
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

      // Update all camera uniforms (view/projection)
      update_camera(w, w.shader, width, height);
      update_camera(w, w.light_source_shader, width, height);
      update_camera(w, w.grid_shader, width, height);

      glEnable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      // Polygon mode
      if (w.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

      // Clear
      glClearColor(w.bg_color[0] * w.bg_color[3], w.bg_color[1] * w.bg_color[3],
                   w.bg_color[2] * w.bg_color[3], 1.0f * w.bg_color[3]);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      // Draw grid
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
                          glm::vec3(0.5f, 0.5f, 0.5f)); // gray
        glDrawElements(GL_LINES, w.grid_length, GL_UNSIGNED_INT, NULL);
      }

      // Draw light sources
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

      // ---------- Draw the main model ----------
      glUseProgram(w.shader);

      // View position
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

      // ---------- FINALLY: draw the model with highlight ----------
      int highlight =
          w.is_import_preview ? w.import_preview.selected_mesh_index : -1;
      if (w.is_import_preview) {
        if (highlight != w.last_highlight_index) {
          spdlog::debug("Preview highlight index: {}", highlight);
          w.last_highlight_index = highlight;
        }
      }
      w.model.motion_matrix = w.gyro_matrix;
      drawModel(w.model, w.shader, highlight);

      // ---- Update touch area mesh (index 32) from touchpad settings ----
      if (w.model.meshes.size() > 32) {
        generateTouchAreaMesh(w); // ensures mesh 32 exists (if not already)
        Mesh &areaMesh = w.model.meshes[32];
        Mesh &touchpad = w.model.meshes[29];

        // Always update scale and position from touchpad
        float tw = touchpad.touch_width;
        float th = touchpad.touch_height;
        if (tw < 0.01f)
          tw = 1.0f;
        if (th < 0.01f)
          th = 1.0f;

        areaMesh.scale[0] = tw;
        areaMesh.scale[1] = 0.02f; // thin plane
        areaMesh.scale[2] = th;

        // Position the rectangle using the touchpad's bounding box (if
        // available)
        if (touchpad.hasBBox && touchpad.elements > 0) {
          // Compute center of the bounding box (X and Z) and topmost Y
          glm::vec3 center = (touchpad.bboxMin + touchpad.bboxMax) * 0.5f;
          float topY = touchpad.bboxMax.y;
          // Place the rectangle at the top center, relative to touchpad's
          // position
          areaMesh.position[0] =
              touchpad.position[0] + center.x + w.touch_area_offset[0];
          areaMesh.position[1] =
              touchpad.position[1] + topY + w.touch_area_offset[1];
          areaMesh.position[2] =
              touchpad.position[2] + center.z + w.touch_area_offset[2];
        } else if (touchpad.elements > 0) {
          // Fallback: use touchpad's position (if no bounding box)
          areaMesh.position[0] = touchpad.position[0] + w.touch_area_offset[0];
          areaMesh.position[1] = touchpad.position[1] + w.touch_area_offset[1];
          areaMesh.position[2] = touchpad.position[2] + w.touch_area_offset[2];
        } else {
          // No touchpad geometry: place at origin with offsets
          areaMesh.position[0] = w.touch_area_offset[0];
          areaMesh.position[1] = w.touch_area_offset[1];
          areaMesh.position[2] = w.touch_area_offset[2];
        }

        // Visibility controlled by checkbox
        areaMesh.visible = w.show_touch_area;
      }

      glUseProgram(0);
      glfwSwapBuffers(w.glfw_window);

      // ---- DRAW TOUCH AREA RECTANGLES (MEGA DEBUG) ----
      // Always draw the test rectangle at origin to verify rendering
      if (!w.touch_area_vao) {
        float verts[] = {-1.0f, -1.0f, 0.0f, 1.0f,  -1.0f, 0.0f,
                         1.0f,  1.0f,  0.0f, -1.0f, 1.0f,  0.0f};
        glGenVertexArrays(1, &w.touch_area_vao);
        glGenBuffers(1, &w.touch_area_vbo);
        glBindVertexArray(w.touch_area_vao);
        glBindBuffer(GL_ARRAY_BUFFER, w.touch_area_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
      }

      glDisable(GL_DEPTH_TEST);
      glUseProgram(w.grid_shader);
      glm::mat4 testModel = glm::mat4(1.0f);
      testModel = glm::scale(testModel, glm::vec3(2.0f, 0.02f, 2.0f));
      shaderUniformMat4(w.grid_shader, "model", testModel);
      shaderUniformVec3(w.grid_shader, "gridColor",
                        glm::vec3(1.0f, 0.0f, 0.0f)); // red
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glBindVertexArray(w.touch_area_vao);
      glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      shaderUniformVec3(w.grid_shader, "gridColor",
                        glm::vec3(1.0f, 1.0f, 0.0f)); // yellow outline
      glDrawArrays(GL_LINE_LOOP, 0, 4);
      glBindVertexArray(0);
      glUseProgram(0);
      glEnable(GL_DEPTH_TEST);

      // Then the touch‑area rectangles (only if show_touch_area)
      if (w.show_touch_area) {
        // ... the existing code that draws per‑touchpoint rectangles ...
      }
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