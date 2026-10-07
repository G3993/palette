cmake_minimum_required(VERSION 3.20)
project(Palette VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
endif()

# Xcode-license shim: /usr/bin/python3 may be blocked, glad's generator needs Python.
if(NOT DEFINED PYTHON_EXECUTABLE AND EXISTS /opt/homebrew/bin/python3)
    set(PYTHON_EXECUTABLE /opt/homebrew/bin/python3 CACHE FILEPATH "" FORCE)
    set(Python_EXECUTABLE /opt/homebrew/bin/python3 CACHE FILEPATH "" FORCE)
    set(Python3_EXECUTABLE /opt/homebrew/bin/python3 CACHE FILEPATH "" FORCE)
endif()

include(cmake/Deps.cmake)

file(GLOB_RECURSE PALETTE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

add_executable(palette ${PALETTE_SOURCES})
target_include_directories(palette PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${stb_SOURCE_DIR})
target_link_libraries(palette PRIVATE imgui glad glm::glm nlohmann_json::nlohmann_json SDL3::SDL3)
target_compile_definitions(palette PRIVATE
    PALETTE_VERSION="${PROJECT_VERSION}"
    PALETTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(palette PRIVATE ${OPENGL_FRAMEWORK})
    # Easel's codesign-for-TCC trick: a stable identity keeps camera/mic grants across rebuilds.
    add_custom_command(TARGET palette POST_BUILD
        COMMAND sh -c "codesign --force --sign \"${PALETTE_SIGN_IDENTITY}\" \"$<TARGET_FILE:palette>\" 2>/dev/null || codesign --force --sign - \"$<TARGET_FILE:palette>\""
        VERBATIM)
elseif(WIN32)
    target_link_libraries(palette PRIVATE opengl32)
    target_compile_definitions(palette PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(palette PRIVATE OpenGL::GL)
endif()
set(PALETTE_SIGN_IDENTITY "Easel Dev" CACHE STRING "codesign identity (falls back to ad-hoc)")
