cmake_minimum_required(VERSION 3.15)
project(3dco+)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_STATIC "Build with static linking (when possible)" OFF)

find_package(PkgConfig REQUIRED)

if(BUILD_STATIC)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(NOT APPLE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    endif()
endif()

pkg_check_modules(ASSIMP REQUIRED assimp)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(SPDLOG REQUIRED spdlog)
pkg_check_modules(FMT REQUIRED fmt)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

if(NOT APPLE)
    find_package(Threads REQUIRED)
    set(THREAD_LIBRARY Threads::Threads)
else()
    if(NOT TARGET Threads::Threads)
        add_library(Threads::Threads INTERFACE IMPORTED)
    endif()
    set(THREAD_LIBRARY "")
endif()

# ------------------------------------------------------------------
# Collect all source files (including ImGui, miniz, glad, etc.)
# ------------------------------------------------------------------
file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.c)
# Exclude backup folder if any
list(FILTER SRC_FILES EXCLUDE REGEX "src/imgui/backup/.*")

# ------------------------------------------------------------------
# Generate models_zip_data.h from assets/models folder
# ------------------------------------------------------------------
set(GEN_SCRIPT "${CMAKE_SOURCE_DIR}/tools/generate_models_zip.py")
set(GEN_HEADER "${CMAKE_SOURCE_DIR}/src/models_zip_data.h")

add_custom_command(
    OUTPUT ${GEN_HEADER}
    COMMAND python3 ${GEN_SCRIPT}
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/models
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating models_zip_data.h from assets/models"
)

# Mark header as generated
set_source_files_properties(${GEN_HEADER} PROPERTIES GENERATED TRUE)

# ------------------------------------------------------------------
# Executable – uses the collected sources
# ------------------------------------------------------------------
add_executable(3dco+ ${SRC_FILES} ${GEN_HEADER})

# Include the source directory (and optionally build dir if needed)
target_include_directories(3dco+ PRIVATE
    "${CMAKE_SOURCE_DIR}/src"               # for models_zip_data.h
)

target_compile_options(3dco+ PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/preinclude.h>
)

if(APPLE)
    target_compile_options(3dco+ PRIVATE -pthread)
endif()

target_include_directories(3dco+ PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/imgui
    ${CMAKE_SOURCE_DIR}/third_party/include
    ${GLFW_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
)

if(BUILD_STATIC)
    target_link_libraries(3dco+
        PRIVATE
        ${GLFW_STATIC_LIBRARIES}
        ${SDL2_STATIC_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ASSIMP_STATIC_LIBRARIES}
        ${SPDLOG_STATIC_LIBRARIES}
        ${FMT_STATIC_LIBRARIES}
        ${THREAD_LIBRARY}
    )
else()
    target_link_libraries(3dco+
        PRIVATE
        ${GLFW_LIBRARIES}
        ${SDL2_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ASSIMP_LIBRARIES}
        ${SPDLOG_LIBRARIES}
        ${FMT_LIBRARIES}
        ${THREAD_LIBRARY}
    )
endif()

if(APPLE)
    target_link_options(3dco+ PRIVATE -L${CMAKE_PREFIX_PATH}/lib)

    target_link_libraries(3dco+ PRIVATE
        ${BASE_LIBS}
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework ForceFeedback"
        "-framework CoreVideo"
        "-framework CoreMedia"
        "-framework GameController"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Carbon"
        "-framework CoreHaptics"
        "-framework Foundation"
        "-lobjc"
        "-lresolv"
        ZLIB::ZLIB
    )
else()
    target_link_libraries(3dco+ PRIVATE
        ${BASE_LIBS}
        ${OPENGL_LIBRARIES}
        ZLIB::ZLIB
    )
endif()

if(WIN32)
    target_link_libraries(3dco+ PRIVATE ws2_32 winmm)
    if(NOT BUILD_STATIC)
        file(GLOB WINDOWS_REDIST "${CMAKE_SOURCE_DIR}/packaging/windows/redist/*.dll")
        foreach(dll ${WINDOWS_REDIST})
            add_custom_command(TARGET 3dco+ POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}" "$<TARGET_FILE_DIR:3dco+>"
            )
        endforeach()
    endif()
endif()

# No need to copy assets/models – they are embedded in the zip and extracted at runtime.

install(TARGETS 3dco+ RUNTIME DESTINATION bin)
install(FILES assets/icon.png DESTINATION share/icons/hicolor/256x256/apps)