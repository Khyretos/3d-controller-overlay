cmake_minimum_required(VERSION 3.15)
project(3dco)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(SDL2 REQUIRED)
find_package(assimp REQUIRED)
find_package(spdlog REQUIRED)

# Collect sources (excluding backup/)
file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.c)
list(FILTER SRC_FILES EXCLUDE REGEX "src/imgui/backup/.*")

add_executable(3dco ${SRC_FILES})

# Copy the models folder to the build directory
add_custom_command(TARGET 3dco POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/models
        ${CMAKE_BINARY_DIR}/models
    COMMENT "Copying models folder to build directory"
)

# Force inclusion of preinclude.h for all C++ sources
target_compile_options(3dco PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/preinclude.h>
)

# Include directories
target_include_directories(3dco PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/imgui
    ${CMAKE_SOURCE_DIR}/include
    ${GLFW3_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(3dco
    PRIVATE
    glfw
    SDL2::SDL2   # or ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    assimp::assimp
    spdlog::spdlog
    ${CMAKE_THREAD_LIBS_INIT}
    ${ASSIMP_LIBRARIES}
)

# Platform-specific
if(WIN32)
    target_link_libraries(3dco ws2_32 winmm)
endif()