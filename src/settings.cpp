#include <iostream>
#include <vector>
#include <sys/stat.h>
#include <fstream>
#include <string>
#include <filesystem>
#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>
#include "settings.h"

char* base_path = SDL_GetBasePath();
std::filesystem::path file_path;
std::ofstream ofs;
std::ifstream ifs;	

namespace {

bool is_usable_dir(const std::filesystem::path &p) {
  std::error_code ec;
  return std::filesystem::exists(p, ec) &&
         std::filesystem::is_directory(p, ec) &&
         !std::filesystem::is_empty(p, ec);
}

} // namespace

std::string get_models_root() {
  namespace fs = std::filesystem;

  fs::path exe_dir(base_path);
  fs::path portable_models = exe_dir / "models";
  if (is_usable_dir(portable_models)) {
    return portable_models.string();
  }

  fs::path installed_models = exe_dir / ".." / "share" / "3dco" / "models";

  char *pref = SDL_GetPrefPath("3dco", "3dco+");
  if (pref) {
    fs::path user_models = fs::path(pref) / "models";
    SDL_free(pref);

    if (is_usable_dir(user_models)) {
      return user_models.string();
    }

    // First run: bootstrap the writable copy from whatever default is
    // actually available (portable next to the exe, or a system install).
    fs::path source;
    std::error_code ec;
    if (fs::exists(portable_models, ec) && fs::is_directory(portable_models, ec)) {
      source = portable_models;
    } else if (fs::exists(installed_models, ec) && fs::is_directory(installed_models, ec)) {
      source = installed_models;
    }

    if (!source.empty()) {
      try {
        fs::create_directories(user_models);
        fs::copy(source, user_models,
                  fs::copy_options::recursive |
                  fs::copy_options::overwrite_existing);
        spdlog::info("First run: copied default models from '{}' to '{}'",
                     source.string(), user_models.string());
        return user_models.string();
      } catch (const std::exception &e) {
        spdlog::warn("Could not copy default models into '{}': {}",
                     user_models.string(), e.what());
      }
    }

    // Nothing to bootstrap from, but still give the caller a writable,
    // guaranteed-to-exist directory to work with.
    fs::create_directories(user_models);
    return user_models.string();
  }

  // SDL_GetPrefPath failed (very unusual) — fall back to the portable
  // path, creating it so callers can always assume it exists.
  fs::create_directories(portable_models);
  return portable_models.string();
}
	
void write_int(std::string label, int value){
    ofs << label.append("\n").c_str();
    ofs << std::to_string(value).append("\n").c_str();
}

void write_float(std::string label, float value){
    ofs << label.append("\n").c_str();
    ofs << std::to_string(value).append("\n").c_str();
}

void write_3_floats(std::string label, float value1, float value2, float value3){
    ofs << label.append("\n").c_str();
    ofs << std::to_string(value1).append("\n").c_str();
    ofs << std::to_string(value2).append("\n").c_str();
    ofs << std::to_string(value3).append("\n").c_str();
}

void write_string(std::string label, std::string value){
    label.append("\n");
    value.append("\n");
    ofs << label.c_str();
    ofs << value.c_str();
}

void write_line(std::string line){
    line.append("\n");
    ofs << line.c_str();
}

void open_ifstream(std::filesystem::path path){
    file_path = std::filesystem::path(base_path);
	std::filesystem::path sub_path(path);
    file_path /= sub_path;
	std::filesystem::create_directory(file_path.parent_path());

	ifs = std::ifstream(file_path);
    if (!ifs){
        std::cout << "Uh oh, file could not be opened for reading!" << std::endl;
    }
}

void open_ofstream(std::filesystem::path path){
    file_path = std::filesystem::path(base_path);
	std::filesystem::path sub_path(path);
    file_path /= sub_path;
	std::filesystem::create_directory(file_path.parent_path());

	ofs = std::ofstream(file_path);
    if (!ofs){
        std::cout << "Uh oh, file could not be opened for writing!" << std::endl;
    }
}

void read_file(std::vector<std::string> *lines){
    while (ifs){
        std::string line;
        std::getline(ifs, line);
        lines->push_back(line);
	}
}

void get_directory_contents(std::vector<std::filesystem::path> *files, std::string path){
    std::string dir_path = base_path;
    dir_path.append("/");
    dir_path.append(path);
    
    std::string p_str = dir_path;
    struct stat sb;
    if (stat(dir_path.c_str(), &sb) == 0){
        for (const auto & entry : std::filesystem::directory_iterator(dir_path))
            files->push_back(entry.path());
    }else{
        std::filesystem::create_directory(dir_path);
    }
}

void list_directory(std::string path){
    std::string dir_path = base_path;
    dir_path.append("/");
    dir_path.append(path);
    
    struct stat sb;
    if (stat(dir_path.c_str(), &sb) == 0){
        for (const auto & entry : std::filesystem::directory_iterator(dir_path))
            std::cout << entry.path() << std::endl;
    }else{
        std::filesystem::create_directory(dir_path);
    }
}

void clear_directory(std::string dir)
{
    std::string dir_path = base_path;
    dir_path.append("/");
    dir_path.append(dir);
    
    struct stat sb;
    if (stat(dir_path.c_str(), &sb) == 0){
        for (const auto& entry : std::filesystem::directory_iterator(dir_path)) 
            std::filesystem::remove_all(entry.path());
    }
}

void close_ifstream(){
    ifs.close();
}

void close_ofstream(){
    ofs.close();
}
