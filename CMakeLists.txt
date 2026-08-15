cmake_minimum_required(VERSION 3.15)
project(3dco)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_STATIC "Build with static linking (when possible)" OFF)

# Find packages – use pkg-config for dynamic libs, but we'll override with static if needed
find_package(PkgConfig REQUIRED)

if(BUILD_STATIC)
    # For static libraries, we need to find them explicitly and add -static flag
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
    pkg_check_modules(ASSIMP REQUIRED assimp)
    pkg_check_modules(GLFW REQUIRED glfw3)
    pkg_check_modules(SPDLOG REQUIRED spdlog)
    pkg_check_modules(FMT REQUIRED fmt)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    # For OpenGL, we still use dynamic linking (system libraries)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    # Add -static flag to linker
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
else()
    pkg_check_modules(ASSIMP REQUIRED assimp)
    pkg_check_modules(GLFW REQUIRED glfw3)
    pkg_check_modules(SPDLOG REQUIRED spdlog)
    pkg_check_modules(FMT REQUIRED fmt)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
endif()

# Collect sources
file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.c)
list(FILTER SRC_FILES EXCLUDE REGEX "src/imgui/backup/.*")

add_executable(3dco ${SRC_FILES})

# Force inclusion of preinclude.h
target_compile_options(3dco PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/preinclude.h>
)

# Include directories
target_include_directories(3dco PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/imgui
    ${CMAKE_SOURCE_DIR}/third_party/include
    ${GLFW_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
)

# Link libraries
if(BUILD_STATIC)
    # For static linking, we need to link libraries in the right order and add dependencies
    target_link_libraries(3dco
        PRIVATE
        ${GLFW_STATIC_LIBRARIES}
        ${SDL2_STATIC_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ASSIMP_STATIC_LIBRARIES}
        ${SPDLOG_STATIC_LIBRARIES}
        ${FMT_STATIC_LIBRARIES}
        Threads::Threads
        # Additional static dependencies
        -lz
        -lminizip
        -ldraco
        -lrt
        -lm
    )
else()
    target_link_libraries(3dco
        PRIVATE
        ${GLFW_LIBRARIES}
        ${SDL2_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ASSIMP_LIBRARIES}
        ${SPDLOG_LIBRARIES}
        ${FMT_LIBRARIES}
        Threads::Threads
    )
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(3dco ws2_32 winmm)

    # Drop the MinGW/SDL2 runtime DLLs next to the executable for dynamic
    # (non-static) Windows builds, so 3dco(+) .exe runs without extra setup.
    if(NOT BUILD_STATIC)
        file(GLOB WINDOWS_REDIST "${CMAKE_SOURCE_DIR}/packaging/windows/redist/*.dll")
        foreach(dll ${WINDOWS_REDIST})
            add_custom_command(TARGET 3dco POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}" "$<TARGET_FILE_DIR:3dco>"
            )
        endforeach()
    endif()
endif()

# Copy models next to the built executable
add_custom_command(TARGET 3dco POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/models
        ${CMAKE_BINARY_DIR}/models
)

# Install
install(TARGETS 3dco RUNTIME DESTINATION bin)
install(FILES assets/icon.png DESTINATION share/icons/hicolor/256x256/apps)
install(DIRECTORY assets/models/ DESTINATION share/3dco/models)
