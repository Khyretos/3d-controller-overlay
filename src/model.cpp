#include "model.h"
#include "shader.h"
#include "stb_image.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <fstream>
#include <functional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <iomanip> // for std::fixed, std::setprecision

// mesh_names is defined in settings_window.cpp – we declare it extern here
extern std::string mesh_names[33];

std::string model_filenames[33] = {
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
    "touch_point1.obj", "touch_point2.obj"};

void writeJson(Model &m, const std::string &path) {
  std::ofstream json(path);
  if (!json) {
    spdlog::error("Failed to write JSON to {}", path);
    return;
  }
  json << std::fixed << std::setprecision(6);
  json << "{\n  \"parts\": [\n";
  for (int i = 0; i < 32; ++i) {
    const Mesh &mesh = m.meshes[i];
    json << "    {\n";
    json << "      \"filename\": \"" << model_filenames[i] << "\",\n";
    json << "      \"position\": [" << mesh.position[0] << ", "
         << mesh.position[1] << ", " << mesh.position[2] << "],\n";
    json << "      \"travel\": [" << mesh.travel[0] << ", " << mesh.travel[1]
         << ", " << mesh.travel[2] << "],\n";
    json << "      \"popup_offset\": [" << mesh.popup_offset[0] << ", "
         << mesh.popup_offset[1] << ", " << mesh.popup_offset[2] << "],\n";
    json << "      \"popup_rotation\": [" << mesh.popup_rotation[0] << ", "
         << mesh.popup_rotation[1] << ", " << mesh.popup_rotation[2] << "],\n";
    json << "      \"trigger_max\": " << mesh.trigger_max << ",\n";
    json << "      \"stick_max\": " << mesh.stick_max << ",\n";
    json << "      \"touch_width\": " << mesh.touch_width << ",\n";
    json << "      \"touch_height\": " << mesh.touch_height << ",\n";
    json << "      \"pivot_offset\": [" << mesh.pivot_offset[0] << ", "
         << mesh.pivot_offset[1] << ", " << mesh.pivot_offset[2] << "],\n";
    json << "      \"rotation\": [" << mesh.rotation[0] << ", "
         << mesh.rotation[1] << ", " << mesh.rotation[2] << "]\n";
    json << "    }" << (i < 31 ? "," : "") << "\n";
  }
  json << "  ]\n}\n";
}

void readInfoJson(Model &m, const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return;
  json data;
  try {
    f >> data;
  } catch (...) {
    spdlog::warn("Failed to parse JSON, falling back to info.txt");
    readInfo(m, path); // fallback
    return;
  }
  if (!data.contains("parts") || !data["parts"].is_array())
    return;
  auto &parts = data["parts"];
  for (int i = 0; i < 32 && i < parts.size(); ++i) {
    auto &p = parts[i];
    Mesh &mesh = m.meshes[i];
    if (p.contains("position")) {
      auto arr = p["position"].get<std::array<float, 3>>();
      mesh.position[0] = arr[0];
      mesh.position[1] = arr[1];
      mesh.position[2] = arr[2];
    }
    if (p.contains("travel")) {
      auto arr = p["travel"].get<std::array<float, 3>>();
      mesh.travel[0] = arr[0];
      mesh.travel[1] = arr[1];
      mesh.travel[2] = arr[2];
    }
    if (p.contains("popup_offset")) {
      auto arr = p["popup_offset"].get<std::array<float, 3>>();
      mesh.popup_offset[0] = arr[0];
      mesh.popup_offset[1] = arr[1];
      mesh.popup_offset[2] = arr[2];
    }
    if (p.contains("popup_rotation")) {
      auto arr = p["popup_rotation"].get<std::array<float, 3>>();
      mesh.popup_rotation[0] = arr[0];
      mesh.popup_rotation[1] = arr[1];
      mesh.popup_rotation[2] = arr[2];
    }
    if (p.contains("trigger_max"))
      mesh.trigger_max = p["trigger_max"].get<float>();
    if (p.contains("stick_max"))
      mesh.stick_max = p["stick_max"].get<float>();
    if (p.contains("touch_width"))
      mesh.touch_width = p["touch_width"].get<float>();
    if (p.contains("touch_height"))
      mesh.touch_height = p["touch_height"].get<float>();
    if (p.contains("pivot_offset")) {
      auto arr = p["pivot_offset"].get<std::array<float, 3>>();
      mesh.pivot_offset[0] = arr[0];
      mesh.pivot_offset[1] = arr[1];
      mesh.pivot_offset[2] = arr[2];
    }
    if (p.contains("rotation")) {
      auto arr = p["rotation"].get<std::array<float, 3>>();
      mesh.rotation[0] = arr[0];
      mesh.rotation[1] = arr[1];
      mesh.rotation[2] = arr[2];
    }
  }
}

// ------------------------------------------------------------------
// LEGACY OBJ LOADER (32 meshes from folder)
// ------------------------------------------------------------------

void loadModel(Model &m, std::string path) {
  m.path = path;
  m.meshes.clear();
  m.imported_meshes.clear();
  m.has_imported_meshes = false;

  // 1. Load all 32 OBJ meshes
  for (int i = 0; i < 32; i++) {
    Mesh new_mesh;
    new_mesh.material.color[0] = 0.8f;
    new_mesh.material.color[1] = 0.8f;
    new_mesh.material.color[2] = 0.8f;
    new_mesh.material.specular = 0.2f;
    new_mesh.material.shininess = 32.0f;
    std::string file_path = path + "/" + model_filenames[i];
    loadMesh(new_mesh, file_path);
    m.meshes.push_back(new_mesh);
  }

  // 2. Count valid meshes (just for logging)
  int valid_meshes = 0;
  for (auto &mesh : m.meshes) {
    if (mesh.elements > 0)
      valid_meshes++;
  }
  if (valid_meshes == 0) {
    spdlog::error("No valid meshes loaded for model at '{}'.", path);
  } else {
    spdlog::info("Loaded {} valid meshes out of 32 for model at '{}'.",
                 valid_meshes, path);
  }

  // 3. Load info data (JSON preferred, fallback to .txt)
  std::string jsonPath = path + "/info.json";
  std::string txtPath = path + "/info.txt";

  if (std::filesystem::exists(jsonPath)) {
    readInfoJson(m, jsonPath);
    spdlog::info("Loaded info.json for model at '{}'.", path);
  } else if (std::filesystem::exists(txtPath)) {
    readInfo(m, txtPath);
    spdlog::info("Loaded info.txt for model at '{}'.", path);
    // Convert to JSON for future use
    writeJson(m, jsonPath);
    spdlog::info("Converted info.txt -> info.json for model at '{}'.", path);
  } else {
    spdlog::warn("No info file found for model at '{}' – using default values.",
                 path);
  }

  // ---- Copy touch area dimensions from touch_point1 (or touch_point2) ----
  if (m.meshes.size() > 29) {
    Mesh &touchpad = m.meshes[29];
    // Only do this if touchpad's values are zero (or tiny)
    if (touchpad.touch_width < 0.001f || touchpad.touch_height < 0.001f) {
      // Try touch_point1 (index 30)
      if (m.meshes.size() > 30) {
        Mesh &tp1 = m.meshes[30];
        if (tp1.touch_width > 0.001f && tp1.touch_height > 0.001f) {
          touchpad.touch_width = tp1.touch_width;
          touchpad.touch_height = tp1.touch_height;
          spdlog::info(
              "Copied touch width={:.3f}, height={:.3f} from touch_point1",
              touchpad.touch_width, touchpad.touch_height);
        } else if (m.meshes.size() > 31) {
          // Fallback to touch_point2 if touch_point1 is invalid
          Mesh &tp2 = m.meshes[31];
          if (tp2.touch_width > 0.001f && tp2.touch_height > 0.001f) {
            touchpad.touch_width = tp2.touch_width;
            touchpad.touch_height = tp2.touch_height;
            spdlog::info(
                "Copied touch width={:.3f}, height={:.3f} from touch_point2",
                touchpad.touch_width, touchpad.touch_height);
          }
        }
      }
    }
  }

  // ---- If we still have zero touch dimensions, try to read them from info.txt
  // directly ----
  if (m.meshes.size() > 29) {
    Mesh &touchpad = m.meshes[29];
    if (touchpad.touch_width < 0.001f || touchpad.touch_height < 0.001f) {
      std::string txtPath = path + "/info.txt";
      std::ifstream info_file(txtPath);
      if (info_file) {
        std::string line;
        while (std::getline(info_file, line)) {
          // Remove trailing CR
          if (!line.empty() && line.back() == '\r')
            line.pop_back();
          if (line == "touch_point1.obj") {
            // We need to skip 14 lines (positions, travel, popup, rotation,
            // trigger, stick) and then read the 15th and 16th lines as
            // touch_width and touch_height.
            for (int skip = 0; skip < 14; ++skip) {
              if (!std::getline(info_file, line))
                break;
            }
            // Read touch_width
            if (std::getline(info_file, line)) {
              try {
                touchpad.touch_width = std::stof(line);
              } catch (...) {
              }
            }
            // Read touch_height
            if (std::getline(info_file, line)) {
              try {
                touchpad.touch_height = std::stof(line);
              } catch (...) {
              }
            }
            spdlog::info("Read touch width={:.3f}, height={:.3f} from info.txt",
                         touchpad.touch_width, touchpad.touch_height);
            break;
          }
        }
        info_file.close();
      }
    }
  }

  // ---- Auto‑compute touch width/height from touchpad mesh (index 29) ----
  if (m.meshes.size() > 29) {
    Mesh &touchpad = m.meshes[29];
    if (touchpad.hasBBox && touchpad.elements > 0) {
      // If touch_width/height are zero or very small, compute from bbox
      if (touchpad.touch_width < 0.001f || touchpad.touch_height < 0.001f) {
        touchpad.touch_width = touchpad.bboxMax.x - touchpad.bboxMin.x;
        touchpad.touch_height = touchpad.bboxMax.z - touchpad.bboxMin.z;
        spdlog::info(
            "Auto‑computed touchpad width={:.3f}, height={:.3f} from mesh",
            touchpad.touch_width, touchpad.touch_height);
      }
    }
  }
}
bool isFloat(std::string myString) {
  std::istringstream iss(myString);
  float f;
  iss >> std::noskipws >> f;
  return iss.eof() && !iss.fail();
}

void loadMesh(Mesh &m, std::string path) {
  std::ifstream ifs = std::ifstream(path);
  if (!ifs.is_open()) {
    spdlog::warn("Could not open mesh file: {}", path);
    m.elements = 0;
    m.vao = 0;
    m.vbo = 0;
    m.ebo = 0;
    return;
  }

  std::vector<vertex_position> positions;
  std::vector<vertex_normal> normals;
  std::vector<vertex_texcoord> texcoords;
  std::vector<Vertex> vertices;
  std::vector<int> indices;

  while (ifs) {
    std::vector<std::string> words;
    std::string line;
    std::string word;

    std::getline(ifs, line);
    std::stringstream line_stream(line);

    while (std::getline(line_stream, word, ' ')) {
      if (!word.empty())
        words.push_back(word);
    }

    if (words.size() > 3 && words[0] == "v") {
      vertex_position pos;
      try {
        pos.x = std::stof(words[1]);
        pos.y = std::stof(words[2]);
        pos.z = std::stof(words[3]);
      } catch (...) {
        spdlog::warn("Invalid vertex data in {}", path);
        continue;
      }
      positions.push_back(pos);
    }

    if (words.size() > 3 && words[0] == "vn") {
      vertex_normal norm;
      try {
        norm.x = std::stof(words[1]);
        norm.y = std::stof(words[2]);
        norm.z = std::stof(words[3]);
      } catch (...) {
        spdlog::warn("Invalid normal data in {}", path);
        continue;
      }
      normals.push_back(norm);
    }

    if (words.size() > 2 && words[0] == "vt") {
      vertex_texcoord tex;
      try {
        tex.x = std::stof(words[1]);
        tex.y = std::stof(words[2]);
      } catch (...) {
        spdlog::warn("Invalid texcoord data in {}", path);
        continue;
      }
      texcoords.push_back(tex);
    }

    if (words.size() > 3 && words[0] == "f") {
      for (unsigned long i = 1; i < words.size() - 2; i++) {
        int num_verts = vertices.size();
        indices.push_back(num_verts);
        indices.push_back(num_verts + i);
        indices.push_back(num_verts + i + 1);
      }
      for (unsigned long i = 1; i < words.size(); i++) {
        std::vector<int> ind;
        std::string value;
        std::stringstream word_stream(words[i]);
        while (std::getline(word_stream, value, '/')) {
          if (value == "") {
            ind.push_back(-1);
          } else {
            try {
              ind.push_back(std::stoi(value) - 1);
            } catch (...) {
              ind.push_back(-1);
            }
          }
        }
        Vertex v;
        v.position = ind[0];
        v.texcoord = (ind.size() > 1 && ind[1] >= 0) ? ind[1] : 0;
        v.normal = (ind.size() > 2 && ind[2] >= 0) ? ind[2] : 0;
        vertices.push_back(v);
      }
    }
  }

  if (vertices.empty() || indices.empty()) {
    spdlog::warn("No valid geometry in {}", path);
    m.elements = 0;
    return;
  }

  // After reading all vertices, compute bounding box
  if (!positions.empty()) {
    m.hasBBox = true;
    m.bboxMin = glm::vec3(FLT_MAX);
    m.bboxMax = glm::vec3(-FLT_MAX);
    for (const auto &v : positions) {
      m.bboxMin.x = std::min(m.bboxMin.x, v.x);
      m.bboxMin.y = std::min(m.bboxMin.y, v.y);
      m.bboxMin.z = std::min(m.bboxMin.z, v.z);
      m.bboxMax.x = std::max(m.bboxMax.x, v.x);
      m.bboxMax.y = std::max(m.bboxMax.y, v.y);
      m.bboxMax.z = std::max(m.bboxMax.z, v.z);
    }
  } else {
    m.hasBBox = false;
  }

  GLfloat vertex_data[vertices.size() * 8];
  for (unsigned long i = 0; i < vertices.size(); i++) {
    int pos_idx = vertices[i].position;
    int norm_idx = vertices[i].normal;
    int tex_idx = vertices[i].texcoord;
    if (pos_idx >= 0 && pos_idx < (int)positions.size()) {
      vertex_data[0 + (8 * i)] = positions[pos_idx].x;
      vertex_data[1 + (8 * i)] = positions[pos_idx].y;
      vertex_data[2 + (8 * i)] = positions[pos_idx].z;
    } else {
      vertex_data[0 + (8 * i)] = 0.0f;
      vertex_data[1 + (8 * i)] = 0.0f;
      vertex_data[2 + (8 * i)] = 0.0f;
    }
    if (norm_idx >= 0 && norm_idx < (int)normals.size()) {
      vertex_data[3 + (8 * i)] = normals[norm_idx].x;
      vertex_data[4 + (8 * i)] = normals[norm_idx].y;
      vertex_data[5 + (8 * i)] = normals[norm_idx].z;
    } else {
      vertex_data[3 + (8 * i)] = 0.0f;
      vertex_data[4 + (8 * i)] = 1.0f;
      vertex_data[5 + (8 * i)] = 0.0f;
    }
    if (tex_idx >= 0 && tex_idx < (int)texcoords.size()) {
      vertex_data[6 + (8 * i)] = texcoords[tex_idx].x;
      vertex_data[7 + (8 * i)] = texcoords[tex_idx].y;
    } else {
      vertex_data[6 + (8 * i)] = 0.0f;
      vertex_data[7 + (8 * i)] = 0.0f;
    }
  }

  m.elements = indices.size();
  GLuint index_data[m.elements];
  for (unsigned long i = 0; i < m.elements; i++) {
    index_data[i] = indices[i];
  }

  glGenVertexArrays(1, &m.vao);
  glGenBuffers(1, &m.vbo);
  glBindVertexArray(m.vao);

  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                        (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                        (void *)(3 * sizeof(GLfloat)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat),
                        (void *)(6 * sizeof(GLfloat)));
  glEnableVertexAttribArray(2);

  glGenBuffers(1, &m.ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_data), index_data,
               GL_STATIC_DRAW);

  glBindVertexArray(0);
}

void readInfo(Model &m, std::string path) {
  std::ifstream info_file(path);
  if (!info_file) {
    spdlog::warn("Info file not found: {}", path);
    return;
  }
  for (int i = 0; i < 32; ++i) {
    std::string filename;
    if (!std::getline(info_file, filename))
      break;
    // Remove trailing CR if present (Windows line endings)
    if (!filename.empty() && filename.back() == '\r')
      filename.pop_back();

    // Read exactly 16 numbers per mesh
    float vals[16];
    int count = 0;
    std::string line;
    while (count < 16 && std::getline(info_file, line)) {
      if (line.empty())
        continue;
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      try {
        vals[count] = std::stof(line);
      } catch (...) {
        vals[count] = 0.0f;
      }
      count++;
    }
    // If we didn't get 16 values, break (malformed file)
    if (count < 16)
      break;

    // Assign to mesh i
    Mesh &mesh = m.meshes[i];
    mesh.position[0] = vals[0];
    mesh.position[1] = vals[1];
    mesh.position[2] = vals[2];
    mesh.travel[0] = vals[3];
    mesh.travel[1] = vals[4];
    mesh.travel[2] = vals[5];
    mesh.popup_offset[0] = vals[6];
    mesh.popup_offset[1] = vals[7];
    mesh.popup_offset[2] = vals[8];
    mesh.popup_rotation[0] = vals[9];
    mesh.popup_rotation[1] = vals[10];
    mesh.popup_rotation[2] = vals[11];
    mesh.trigger_max = vals[12];
    mesh.stick_max = vals[13];
    mesh.touch_width = vals[14];
    mesh.touch_height = vals[15];
    // pivot_offset and rotation remain 0 (default)
  }
}

void writeInfo(Model &m, std::string path) {
  std::string file_path = path + "/info.txt";
  std::ofstream info_file(file_path);
  for (int i = 0; i < 32; i++) {
    info_file << model_filenames[i] << "\n";
    info_file << m.meshes[i].position[0] << "\n";
    info_file << m.meshes[i].position[1] << "\n";
    info_file << m.meshes[i].position[2] << "\n";
    info_file << m.meshes[i].travel[0] << "\n";
    info_file << m.meshes[i].travel[1] << "\n";
    info_file << m.meshes[i].travel[2] << "\n";
    info_file << m.meshes[i].popup_offset[0] << "\n";
    info_file << m.meshes[i].popup_offset[1] << "\n";
    info_file << m.meshes[i].popup_offset[2] << "\n";
    info_file << m.meshes[i].popup_rotation[0] << "\n";
    info_file << m.meshes[i].popup_rotation[1] << "\n";
    info_file << m.meshes[i].popup_rotation[2] << "\n";
    info_file << m.meshes[i].trigger_max << "\n";
    info_file << m.meshes[i].stick_max << "\n";
    info_file << m.meshes[i].touch_width << "\n";
    info_file << m.meshes[i].touch_height << "\n";
  }
  info_file.close();

  // Also write JSON (so future loads use the modern format)
  std::string json_path = path + "/info.json";
  writeJson(m, json_path);
}

void deleteTexture(GLuint &id) {
  glDeleteTextures(1, &id);
  id = 0;
}

void loadTexture(GLuint &id, std::string path) {
  if (id == 0)
    glGenTextures(1, &id);

  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  int width, height, nrChannels;
  unsigned char *data =
      stbi_load(path.c_str(), &width, &height, &nrChannels, 0);

  GLenum format;
  if (nrChannels == 1)
    format = GL_RED;
  else if (nrChannels == 3)
    format = GL_RGB;
  else if (nrChannels == 4)
    format = GL_RGBA;
  else {
    spdlog::error("Unknown channel count {} for texture {}", nrChannels, path);
    return;
  }

  if (data) {
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format,
                 GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
  } else {
    spdlog::error("Failed to load texture: {}", path);
  }
  stbi_image_free(data);
}

void drawMesh(const Mesh &mesh, const glm::mat4 &modelMatrix, GLuint shader) {
  if (!mesh.vao || mesh.elements == 0)
    return;

  glBindVertexArray(mesh.vao);

  shaderUniformInt(shader, "num_textures", mesh.textures.size());
  for (size_t i = 0; i < mesh.textures.size(); i++) {
    std::string name = "textures[";
    name.append(std::to_string(i));
    name.append("]");
    shaderUniformInt(shader, std::string(name).append(".id").c_str(), i);
    shaderUniformInt(shader, std::string(name).append(".type").c_str(),
                     mesh.textures[i].type);
    shaderUniformFloat(shader, std::string(name).append(".offsetX").c_str(),
                       mesh.textures[i].offsetX);
    shaderUniformFloat(shader, std::string(name).append(".offsetY").c_str(),
                       mesh.textures[i].offsetY);
    shaderUniformFloat(shader, std::string(name).append(".scaleX").c_str(),
                       mesh.textures[i].scaleX);
    shaderUniformFloat(shader, std::string(name).append(".scaleY").c_str(),
                       mesh.textures[i].scaleY);
    shaderUniformFloat(shader, std::string(name).append(".rotation").c_str(),
                       mesh.textures[i].rotation);
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, mesh.textures[i].id);
  }

  shaderUniformFloat(shader, "material.ambient", mesh.material.ambient);
  shaderUniformFloat(shader, "material.diffuse", mesh.material.diffuse);
  shaderUniformFloat(shader, "material.specular", mesh.material.specular);
  shaderUniformVec3(shader, "material.color",
                    glm::vec3(mesh.material.color[0], mesh.material.color[1],
                              mesh.material.color[2]));
  shaderUniformFloat(shader, "material.shininess", mesh.material.shininess);
  shaderUniformFloat(shader, "material.alpha", mesh.material.alpha);

  shaderUniformVec3(shader, "highlight_color",
                    glm::vec3(mesh.material.highlight[0],
                              mesh.material.highlight[1],
                              mesh.material.highlight[2]));
  shaderUniformFloat(shader, "highlight_value", mesh.highlight_value);

  shaderUniformMat4(shader, "model", modelMatrix);
  glm::mat3 normal = glm::mat3(modelMatrix);
  shaderUniformMat3(shader, "normal_model",
                    glm::transpose(glm::inverse(normal)));

  if (mesh.visible) {
    glDrawElements(GL_TRIANGLES, mesh.elements, GL_UNSIGNED_INT, 0);
  }
}

glm::mat4 computeMeshTransform(const Model &m, int meshIndex,
                               const glm::mat4 &parentMatrix) {
  const Mesh &mesh = m.meshes[meshIndex];
  glm::mat4 model = parentMatrix;

  // Apply mesh position
  model = glm::translate(
      model, glm::vec3(mesh.position[0], mesh.position[1], mesh.position[2]));

  // Translate to pivot point
  model = glm::translate(model,
                         glm::vec3(mesh.pivot_offset[0], mesh.pivot_offset[1],
                                   mesh.pivot_offset[2]));

  // Apply Euler rotation (in radians, convert from degrees stored in
  // mesh.rotation)
  model =
      glm::rotate(model, glm::radians(mesh.rotation[0]), glm::vec3(1, 0, 0));
  model =
      glm::rotate(model, glm::radians(mesh.rotation[1]), glm::vec3(0, 1, 0));
  model =
      glm::rotate(model, glm::radians(mesh.rotation[2]), glm::vec3(0, 0, 1));

  // Translate back from pivot
  model = glm::translate(model,
                         -glm::vec3(mesh.pivot_offset[0], mesh.pivot_offset[1],
                                    mesh.pivot_offset[2]));

  if (mesh.useCustomScale) {
    model = glm::scale(model,
                       glm::vec3(mesh.scale[0], mesh.scale[1], mesh.scale[2]));
  }

  // ---- Now apply popup or normal stick/trigger/button transforms ----
  if (mesh.popup) {
    model = glm::translate(model,
                           glm::vec3(mesh.popup_offset[0], mesh.popup_offset[1],
                                     mesh.popup_offset[2]));
    model =
        glm::rotate(model, mesh.popup_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f));
    model =
        glm::rotate(model, mesh.popup_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model =
        glm::rotate(model, mesh.popup_rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
  } else {
    // Stick rotation (always applied; zero for non‑sticks)
    model = glm::rotate(model, mesh.stick_X / -32767 * mesh.stick_max,
                        glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, mesh.stick_Y / 32767 * mesh.stick_max,
                        glm::vec3(1.0f, 0.0f, 0.0f));
    // Trigger rotation
    model = glm::rotate(model, mesh.pull / -32767 * mesh.trigger_max,
                        glm::vec3(1.0f, 0.0f, 0.0f));
    // Button travel
    model = glm::translate(model, glm::vec3(mesh.travel[0] * mesh.press,
                                            mesh.travel[1] * mesh.press,
                                            mesh.travel[2] * mesh.press));
  }

  // Touchpad offset
  if (mesh.touch_state > 0) {
    model = glm::translate(
        model,
        glm::vec3((mesh.touch_X * mesh.touch_width) - mesh.touch_width * 0.5, 0,
                  (mesh.touch_Y * mesh.touch_height) -
                      mesh.touch_height * 0.5));
  }

  return model;
}

void drawModel(Model m, GLuint shader, int highlight_mesh_index) {
  int num_meshes = m.meshes.size();

  // ============================================================
  // LEGACY MODELS – use original matrix construction (unchanged)
  // ============================================================
  if (!m.has_imported_meshes) {
    for (int i = 0; i < (int)m.meshes.size(); ++i) {

      Mesh mesh = m.meshes[i]; // copy to modify popup

      // ---- Set popup flags (exactly as original) ----
      mesh.popup = false;
      if (m.popup_bumpers && (i == 18 || i == 19))
        mesh.popup = true;
      if (m.popup_triggers && (i == 3 || i == 4))
        mesh.popup = true;
      if (m.popup_paddles && (i >= 25 && i <= 28))
        mesh.popup = true;

      // ---- Build matrix (EXACT original order and operations) ----
      glm::mat4 model = glm::mat4(1.0f);
      model *= m.motion_matrix;
      model =
          glm::translate(model, glm::vec3(mesh.position[0], mesh.position[1],
                                          mesh.position[2]));

      // Apply custom scale if enabled
      if (mesh.useCustomScale) {
        model = glm::scale(
            model, glm::vec3(mesh.scale[0], mesh.scale[1], mesh.scale[2]));
      }

      // Stick rotation (always applied; zero for non‑sticks)
      model = glm::rotate(model, mesh.stick_X / -32767 * mesh.stick_max,
                          glm::vec3(0.0f, 0.0f, 1.0f));
      model = glm::rotate(model, mesh.stick_Y / 32767 * mesh.stick_max,
                          glm::vec3(1.0f, 0.0f, 0.0f));

      if (mesh.popup) {
        model = glm::translate(model, glm::vec3(mesh.popup_offset[0],
                                                mesh.popup_offset[1],
                                                mesh.popup_offset[2]));
        model = glm::rotate(model, mesh.popup_rotation[0],
                            glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, mesh.popup_rotation[1],
                            glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, mesh.popup_rotation[2],
                            glm::vec3(0.0f, 0.0f, 1.0f));
      } else {
        // Button travel
        model = glm::translate(model, glm::vec3(mesh.travel[0] * mesh.press,
                                                mesh.travel[1] * mesh.press,
                                                mesh.travel[2] * mesh.press));
        // Trigger rotation
        model = glm::rotate(model, mesh.pull / -32767 * mesh.trigger_max,
                            glm::vec3(1.0f, 0.0f, 0.0f));
      }

      // Touchpad offset
      if (mesh.touch_state > 0) {
        model = glm::translate(
            model,
            glm::vec3(
                (mesh.touch_X * mesh.touch_width) - mesh.touch_width * 0.5, 0,
                (mesh.touch_Y * mesh.touch_height) - mesh.touch_height * 0.5));
      }

      // ---- Highlight ----
      if (i == highlight_mesh_index) {
        mesh.material.color[0] = 0.0f;
        mesh.material.color[1] = 1.0f;
        mesh.material.color[2] = 0.0f;
      }

      drawMesh(mesh, model, shader);
    }
    return;
  }

  // ============================================================
  // IMPORTED MODELS – use parent‑child logic
  // ============================================================
  std::vector<glm::mat4> finalMatrices(num_meshes, glm::mat4(1.0f));
  std::vector<bool> computed(num_meshes, false);

  // First pass: root meshes (parentIndex == -1)
  for (int i = 0; i < num_meshes; ++i) {
    if (m.meshes[i].parentIndex == -1) {
      finalMatrices[i] = computeMeshTransform(m, i, m.motion_matrix);
      computed[i] = true;
    }
  }

  // Second pass: children
  for (int i = 0; i < num_meshes; ++i) {
    if (computed[i])
      continue;
    int parent = m.meshes[i].parentIndex;
    if (parent >= 0 && parent < num_meshes && computed[parent]) {
      finalMatrices[i] = computeMeshTransform(m, i, finalMatrices[parent]);
      computed[i] = true;
    }
  }

  // Fallback
  for (int i = 0; i < num_meshes; ++i) {
    if (!computed[i]) {
      finalMatrices[i] = computeMeshTransform(m, i, glm::mat4(1.0f));
    }
  }

  // Draw all imported meshes
  for (int i = 0; i < num_meshes; ++i) {
    Mesh mesh = m.meshes[i];
    mesh.popup = false;
    if (m.popup_bumpers && (i == 18 || i == 19))
      mesh.popup = true;
    if (m.popup_triggers && (i == 3 || i == 4))
      mesh.popup = true;
    if (m.popup_paddles && (i >= 25 && i <= 28))
      mesh.popup = true;

    if (i == highlight_mesh_index) {
      mesh.material.color[0] = 0.0f;
      mesh.material.color[1] = 1.0f;
      mesh.material.color[2] = 0.0f;
    }
    drawMesh(mesh, finalMatrices[i], shader);
  }
}

// ------------------------------------------------------------------
// NEW: CUSTOM MODEL IMPORT (using Assimp) and MAPPING
// ------------------------------------------------------------------

void importModelFile(Model &m, const std::string &filepath) {
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(
      filepath, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                    aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
  if (!scene || !scene->mRootNode) {
    spdlog::error("Failed to import model: {}", importer.GetErrorString());
    return;
  }

  m.imported_meshes.clear();

  // Recursively collect all meshes from the scene
  std::function<void(aiNode *)> processNode = [&](aiNode *node) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
      aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
      ImportedMesh imported;
      imported.name = mesh->mName.length ? mesh->mName.C_Str() : "Unnamed";
      imported.assigned_part = -1;

      // Copy vertex data
      for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
        imported.positions.push_back(glm::vec3(
            mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z));
        if (mesh->HasNormals())
          imported.normals.push_back(glm::vec3(
              mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z));
        else
          imported.normals.push_back(glm::vec3(0, 1, 0));
        if (mesh->HasTextureCoords(0))
          imported.texcoords.push_back(glm::vec2(mesh->mTextureCoords[0][v].x,
                                                 mesh->mTextureCoords[0][v].y));
        else
          imported.texcoords.push_back(glm::vec2(0, 0));
      }
      // Copy indices
      for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        aiFace face = mesh->mFaces[f];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
          imported.indices.push_back(face.mIndices[j]);
      }
      m.imported_meshes.push_back(imported);
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
      processNode(node->mChildren[i]);
  };
  processNode(scene->mRootNode);

  m.has_imported_meshes = !m.imported_meshes.empty();
  if (m.has_imported_meshes)
    spdlog::info("Imported {} meshes from {}", m.imported_meshes.size(),
                 filepath);
}

void applyMeshMapping(Model &m) {
  // For each imported mesh that has an assigned part, replace the corresponding
  // mesh in m.meshes
  for (auto &imported : m.imported_meshes) {
    if (imported.assigned_part < 0 || imported.assigned_part >= 32)
      continue;

    // Build vertex_data from imported data
    std::vector<float> vertex_data;
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

    Mesh &target = m.meshes[imported.assigned_part];
    // Delete old GL objects
    if (target.vao)
      glDeleteVertexArrays(1, &target.vao);
    if (target.vbo)
      glDeleteBuffers(1, &target.vbo);
    if (target.ebo)
      glDeleteBuffers(1, &target.ebo);

    // Upload new data
    glGenVertexArrays(1, &target.vao);
    glGenBuffers(1, &target.vbo);
    glGenBuffers(1, &target.ebo);
    glBindVertexArray(target.vao);
    glBindBuffer(GL_ARRAY_BUFFER, target.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float),
                 vertex_data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, target.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 imported.indices.size() * sizeof(unsigned int),
                 imported.indices.data(), GL_STATIC_DRAW);
    target.elements = imported.indices.size();
    glBindVertexArray(0);

    // Preserve existing material and motion data (they are kept as-is)
    spdlog::info("Assigned mesh '{}' to part '{}'", imported.name,
                 mesh_names[imported.assigned_part]);
  }

  // Clear imported list to indicate mapping applied
  m.imported_meshes.clear();
  m.has_imported_meshes = false;
}

void convertImportedToMeshes(Model &m) {
  m.meshes.clear();

  for (auto &imported : m.imported_meshes) {
    Mesh mesh;
    mesh.material.ambient = 0.2f;
    mesh.material.diffuse = 1.0f;
    mesh.material.specular = 0.1f;
    mesh.material.shininess = 32.0f;
    mesh.material.color[0] = 0.8f;
    mesh.material.color[1] = 0.8f;
    mesh.material.color[2] = 0.8f;
    mesh.material.highlight[0] = 0.0f;
    mesh.material.highlight[1] = 1.0f;
    mesh.material.highlight[2] = 0.0f;
    // parentIndex will be set when mapping is applied; we don't set it here.

    std::vector<float> vertex_data;
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)0);
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
    glBindVertexArray(0);

    m.meshes.push_back(mesh);
  }

  m.has_imported_meshes = true;
}