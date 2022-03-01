cmake_minimum_required(VERSION 3.15)
project(SoftBody)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  set(PROJECT_COMPILE_OPTIONS /Wall /analyze)
  string(APPEND CMAKE_CXX_FLAGS_DEBUG          " /fsanitize=address")
  string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " /fsanitize=address")
else()
  set(PROJECT_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Wshadow -Wextra-semi
    -Wmissing-noreturn -Wconversion -Wsign-conversion)
  string(APPEND CMAKE_CXX_FLAGS_DEBUG          " -fsanitize=address,undefined,leak")
  string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -fsanitize=address,undefined,leak")
endif()

if (WIN32)
  # combine everthing into one binary on windows
  set(BUILD_SHARED_LIBS OFF)
  # -static removes dependencies on libgcc.dll, libstdc++.dll and winthread.dll on windows
  set(PROJECT_STATIC_OPTIONS "-static")
else()
  # on linux there is not much point being statically linked
  set(PROJECT_STATIC_OPTIONS "")
endif()

set(SFML_BUILD_AUDIO OFF)
set(SFML_BUILD_NETWORK OFF)
add_subdirectory(SFML)

add_library(sfml INTERFACE)
target_include_directories(sfml INTERFACE SFML/include)
target_link_directories(sfml INTERFACE SFML/build/lib)
target_link_libraries(sfml INTERFACE sfml-graphics sfml-window sfml-system Threads::Threads)

# imgui doesn't do cmake and doesn't need compiling
add_library(imgui INTERFACE)
target_include_directories(imgui INTERFACE imgui)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
set(IMGUI_SFML_FIND_SFML OFF)
add_subdirectory(imgui-sfml)

add_library(imgui-sfml INTERFACE)
target_include_directories(imgui-sfml INTERFACE imgui-sfml)
target_link_directories(imgui-sfml INTERFACE imgui-sfml/build)
target_link_libraries(imgui-sfml INTERFACE ImGui-SFML sfml imgui)

find_package(Threads REQUIRED)
add_executable(softbody main.cpp include/video.cpp include/arial.cpp include/point_png.cpp)
target_include_directories(softbody PRIVATE include)
target_link_libraries(softbody PRIVATE imgui-sfml ${PROJECT_STATIC_OPTIONS})
target_compile_options(softbody PRIVATE ${PROJECT_COMPILE_OPTIONS})

add_executable(dangling dangling.cpp)
target_link_libraries(dangling PRIVATE imgui-sfml)

add_executable(derived derived.cpp)

add_executable(arch arch.cpp)
target_link_libraries(arch PRIVATE imgui-sfml)

add_subdirectory(test)

# find_package(benchmark REQUIRED)
