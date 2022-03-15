cmake_minimum_required(VERSION 3.15)
project(hibp)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  set(PROJECT_COMPILE_OPTIONS /Wall /analyze)
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " /fsanitize=address")
else() # unix'ish
  set(PROJECT_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Wshadow -Wextra-semi
    -Wmissing-noreturn -Wconversion -Wsign-conversion)
    if (NOT MINGW) # sanitizers are not working under mingw
      string(APPEND CMAKE_CXX_FLAGS_DEBUG " -fsanitize=address,undefined,leak")
    endif()
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

add_library(hibp INTERFACE)
target_include_directories(hibp INTERFACE include)
target_link_libraries(toolbelt INTERFACE fmt)

add_executable(hibp_search app/hibp_search.cpp)
target_compile_options(hibp_search PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_search PRIVATE toolbelt sha1 hibp tbb)

add_executable(hibp_sort app/hibp_sort.cpp)
target_compile_options(hibp_sort PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_sort PRIVATE toolbelt hibp tbb)

add_executable(hibp_convert app/hibp_convert.cpp)
target_compile_options(hibp_convert PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_convert PRIVATE toolbelt sha1 hibp tbb)

# add_executable(heap_primitives app/heap_primitives.cpp)
# target_compile_options(heap_primitives PRIVATE ${PROJECT_COMPILE_OPTIONS})
# target_link_libraries(heap_primitives PRIVATE toolbelt fmt)

# add_executable(vectorized_strcmp app/vectorized_strcmp.cpp)
# target_compile_options(vectorized_strcmp PRIVATE ${PROJECT_COMPILE_OPTIONS})
# target_link_libraries(vectorized_strcmp PRIVATE toolbelt fmt hibp)

option(HIBP_TEST "Enable tests" OFF)
if(HIBP_TEST)
  enable_testing()
  add_subdirectory(test)
else(HIBP_TEST)
  message(STATUS "Tests are disabled. Set HIBP_TEST to ON to run tests.")
endif(HIBP_TEST)

# benchmarks
option(HIBP_BENCH "Enable tests" OFF)
if(HIBP_BENCH)
  add_subdirectory(bench)
else(HIBP_BENCH)
  message(STATUS "Benchmarks are disabled. Set HIBP_BENCH to ON to run tests.")
endif(HIBP_BENCH)

