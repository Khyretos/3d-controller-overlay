#include "model.h"
#include "shader.h"
#include "stb_image.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <spdlog/spdlog.h>
#include <sstream>

// mesh_names is defined in settings_window.cpp – we declare it extern here
extern std::string mesh_names[32];

std::string model_filenames[32] = {
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

// ------------------------------------------------------------------
// LEGACY OBJ LOADER (32 meshes from folder)
// ------------------------------------------------------------------

void loadModel(Model &m, std::string path) {
  m.path = path;
  m.meshes.clear();
  m.imported_meshes.clear();
  m.has_imported_meshes = false;

  // Always ensure we have 32 meshes (even if files are missing)
  for (int i = 0; i < 32; i++) {
    Mesh new_mesh;
    // Default material values
    new_mesh.material.color[0] = 0.8f;
    new_mesh.material.color[1] = 0.8f;
    new_mesh.material.color[2] = 0.8f;
    new_mesh.material.specular = 0.2f;
    new_mesh.material.shininess = 32.0f;

    std::string file_path = path;
    file_path.append("/");
    file_path.append(model_filenames[i]);
    loadMesh(new_mesh, file_path);
    m.meshes.push_back(new_mesh);
  }

  int valid_meshes = 0;
  for (auto &mesh : m.meshes) {
    if (mesh.elements > 0)
      valid_meshes++;
  }
  if (valid_meshes == 0) {
    spdlog::error("No valid meshes loaded for model at '{}'. Check if all OBJ "
                  "files are present.",
                  path);
  } else {
    spdlog::info("Loaded {} valid meshes out of 32 for model at '{}'.",
                 valid_meshes, path);
  }

  std::string info_file_path = path;
  info_file_path.append("/info.txt");
  readInfo(m, info_file_path);
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
  std::ifstream info_file = std::ifstream(path);
  if (!info_file) {
    spdlog::warn("Info file not found: {}", path);
    return;
  }
  while (info_file) {
    std::string line;
    std::getline(info_file, line);
    for (int i = 0; i < 32; i++) {
      if (line == model_filenames[i]) {
        for (int p = 0; p < 3; p++) {
          std::getline(info_file, line);
          if (isFloat(line))
            m.meshes[i].position[p] = std::stof(line);
        }
        for (int t = 0; t < 3; t++) {
          std::getline(info_file, line);
          if (isFloat(line))
            m.meshes[i].travel[t] = std::stof(line);
        }
        for (int po = 0; po < 3; po++) {
          std::getline(info_file, line);
          if (isFloat(line))
            m.meshes[i].popup_offset[po] = std::stof(line);
        }
        for (int pr = 0; pr < 3; pr++) {
          std::getline(info_file, line);
          if (isFloat(line))
            m.meshes[i].popup_rotation[pr] = std::stof(line);
        }
        std::getline(info_file, line);
        if (isFloat(line))
          m.meshes[i].trigger_max = std::stof(line);
        std::getline(info_file, line);
        if (isFloat(line))
          m.meshes[i].stick_max = std::stof(line);
        std::getline(info_file, line);
        if (isFloat(line))
          m.meshes[i].touch_width = std::stof(line);
        std::getline(info_file, line);
        if (isFloat(line))
          m.meshes[i].touch_height = std::stof(line);
      }
    }
  }
}

void writeInfo(Model &m, std::string path) {
  std::string file_path = path + "/info.txt";
  std::ofstream info_file(file_path);
  for (int i = 0; i < 32; i++) {
    info_file << model_filenames[i] << "\n";
    for (int pos = 0; pos < 3; pos++) {
      info_file << m.meshes[i].position[pos] << "\n";
    }
    for (int t = 0; t < 3; t++) {
      info_file << m.meshes[i].travel[t] << "\n";
    }
    for (int po = 0; po < 3; po++) {
      info_file << m.meshes[i].popup_offset[po] << "\n";
    }
    for (int pr = 0; pr < 3; pr++) {
      info_file << m.meshes[i].popup_rotation[pr] << "\n";
    }
    info_file << m.meshes[i].trigger_max << "\n";
    info_file << m.meshes[i].stick_max << "\n";
    info_file << m.meshes[i].touch_width << "\n";
    info_file << m.meshes[i].touch_height << "\n";
  }
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

void drawMesh(Mesh m, glm::mat4 motion, GLuint shader) {
  if (!m.vao || m.elements == 0)
    return; // skip empty meshes

  glBindVertexArray(m.vao);

  shaderUniformInt(shader, "num_textures", m.textures.size());
  for (size_t i = 0; i < m.textures.size(); i++) {
    std::string name = "textures[";
    name.append(std::to_string(i));
    name.append("]");
    shaderUniformInt(shader, std::string(name).append(".id").c_str(), i);
    shaderUniformInt(shader, std::string(name).append(".type").c_str(),
                     m.textures[i].type);
    shaderUniformFloat(shader, std::string(name).append(".offsetX").c_str(),
                       m.textures[i].offsetX);
    shaderUniformFloat(shader, std::string(name).append(".offsetY").c_str(),
                       m.textures[i].offsetY);
    shaderUniformFloat(shader, std::string(name).append(".scaleX").c_str(),
                       m.textures[i].scaleX);
    shaderUniformFloat(shader, std::string(name).append(".scaleY").c_str(),
                       m.textures[i].scaleY);
    shaderUniformFloat(shader, std::string(name).append(".rotation").c_str(),
                       m.textures[i].rotation);
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, m.textures[i].id);
  }

  shaderUniformFloat(shader, "material.ambient", m.material.ambient);
  shaderUniformFloat(shader, "material.diffuse", m.material.diffuse);
  shaderUniformFloat(shader, "material.specular", m.material.specular);
  shaderUniformVec3(
      shader, "material.color",
      glm::vec3(m.material.color[0], m.material.color[1], m.material.color[2]));
  shaderUniformFloat(shader, "material.shininess", m.material.shininess);

  glm::mat4 model = glm::mat4(1.0f);
  model *= motion;
  model = glm::translate(
      model, glm::vec3(m.position[0], m.position[1], m.position[2]));
  model = glm::rotate(model, m.stick_X / -32767 * m.stick_max,
                      glm::vec3(0.0f, 0.0f, 1.0f));
  model = glm::rotate(model, m.stick_Y / 32767 * m.stick_max,
                      glm::vec3(1.0f, 0.0f, 0.0f));
  if (m.popup) {
    model =
        glm::translate(model, glm::vec3(m.popup_offset[0], m.popup_offset[1],
                                        m.popup_offset[2]));
    model =
        glm::rotate(model, m.popup_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f));
    model =
        glm::rotate(model, m.popup_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model =
        glm::rotate(model, m.popup_rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
  } else {
    model = glm::translate(model, glm::vec3(m.travel[0] * m.press,
                                            m.travel[1] * m.press,
                                            m.travel[2] * m.press));
    model = glm::rotate(model, m.pull / -32767 * m.trigger_max,
                        glm::vec3(1.0f, 0.0f, 0.0f));
  }
  if (m.touch_state > 0) {
    model = glm::translate(
        model, glm::vec3((m.touch_X * m.touch_width) - m.touch_width * 0.5, 0,
                         (m.touch_Y * m.touch_height) - m.touch_height * 0.5));
  }

  shaderUniformVec3(shader, "highlight_color",
                    glm::vec3(m.material.highlight[0], m.material.highlight[1],
                              m.material.highlight[2]));
  shaderUniformFloat(shader, "highlight_value", m.highlight_value);

  shaderUniformMat4(shader, "model", model);
  glm::mat3 normal = glm::mat3(model);
  shaderUniformMat3(shader, "normal_model",
                    glm::transpose(glm::inverse(normal)));
  if (m.visible) {
    glDrawElements(GL_TRIANGLES, m.elements, GL_UNSIGNED_INT, 0);
  }
}

void drawModel(Model m, GLuint shader, int highlight_mesh_index) {
  for (int i = 0; i < m.meshes.size(); ++i) {
    Mesh mesh = m.meshes[i];
    // ... set popup flags ...
    if (i == highlight_mesh_index) {
      mesh.material.color[0] = 1.0f;
      mesh.material.color[1] = 0.0f;
      mesh.material.color[2] = 0.0f;
    }
    drawMesh(mesh, m.motion_matrix, shader);
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