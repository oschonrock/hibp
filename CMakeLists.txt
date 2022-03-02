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


add_subdirectory(ext/fmt)
target_compile_options(fmt INTERFACE -Wno-missing-noreturn)

add_subdirectory(ext/date)

add_subdirectory(ext/fast_float)

add_library(toolbelt INTERFACE)
target_include_directories(toolbelt INTERFACE ext/toolbelt)
target_link_libraries(toolbelt INTERFACE date fast_float)

add_library(sha1 INTERFACE)
target_include_directories(sha1 INTERFACE ext/sha1)

add_library(hibp_lib INTERFACE)
target_include_directories(hibp_lib INTERFACE include)
target_link_libraries(toolbelt INTERFACE fmt)

add_executable(hibp app/hibp.cpp)
target_compile_options(hibp PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp PRIVATE toolbelt sha1 fmt hibp_lib)

add_executable(hibp_sort app/hibp_sort.cpp)
target_compile_options(hibp_sort PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_sort PRIVATE toolbelt sha1 fmt hibp_lib)

add_executable(hibp_convert app/hibp_convert.cpp)
target_compile_options(hibp_convert PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_convert PRIVATE toolbelt sha1 fmt hibp_lib)


add_subdirectory(test)

# find_package(benchmark REQUIRED)
