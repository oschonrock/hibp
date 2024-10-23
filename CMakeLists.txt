cmake_minimum_required(VERSION 3.15)
project(hibp)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  set(PROJECT_COMPILE_OPTIONS /W3)
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " /fsanitize=address")
else() # unix'ish
  set(PROJECT_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Wshadow -Wextra-semi
    -Wmissing-noreturn -Wconversion -Wsign-conversion -Wno-ignored-attributes)
    if (NOT MINGW) # sanitizers are not working under mingw
      string(APPEND CMAKE_CXX_FLAGS_DEBUG " -fsanitize=address,undefined,leak")
    endif()
endif()

add_subdirectory(ext/fmt)
if (NOT MSVC)
  target_compile_options(fmt INTERFACE -Wno-missing-noreturn)
endif()

add_subdirectory(ext/date)

add_subdirectory(ext/fast_float)

add_library(toolbelt INTERFACE)
target_include_directories(toolbelt INTERFACE ext/toolbelt)
target_compile_features(toolbelt INTERFACE cxx_std_20)
target_link_libraries(toolbelt INTERFACE date fast_float)

add_library(sha1 INTERFACE)
target_include_directories(sha1 INTERFACE ext/sha1)
if (NOT MSVC)
  target_compile_options(sha1 INTERFACE -Wno-sign-conversion)
endif()

add_library(arrcmp INTERFACE)
target_include_directories(arrcmp INTERFACE include)
target_compile_features(arrcmp INTERFACE cxx_std_20)
target_link_libraries(arrcmp INTERFACE fmt)

add_library(hibp INTERFACE)
target_include_directories(hibp INTERFACE include)
target_compile_features(hibp INTERFACE cxx_std_20)
target_link_libraries(hibp INTERFACE fmt arrcmp toolbelt)

find_package(TBB REQUIRED)

add_library(flat_file INTERFACE)
target_include_directories(flat_file INTERFACE include)
target_compile_features(flat_file INTERFACE cxx_std_20)
if (MSVC) # better solutions?
  target_link_libraries(flat_file INTERFACE fmt tBB::tbb)
else()
  target_link_libraries(flat_file INTERFACE fmt tbb)
endif()


set(RESTINIO_EXPLICIT_CPPSTD 20)
set(RESTINIO_ASIO_SOURCE standalone)
set(asio_INCLUDE_DIRS ../asio/include) # relative to ext/restinio/dev/restinio
set(BUILD_STATIC_LIBS ON CACHE BOOL "OVERIIDDEN") # not a great way but required?
add_subdirectory(ext/restinio/dev/nodejs/llhttp)
# add_subdirectory(ext/fmt) # not needed as alredy part of our root CMakelists
add_subdirectory(ext/restinio/dev/expected-lite)
add_subdirectory(ext/restinio/dev/restinio)
if (NOT MSVC)
  target_compile_options(restinio INTERFACE -Wno-shadow -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-extra-semi)
endif()

add_executable(hibp_search app/hibp_search.cpp)
target_compile_options(hibp_search PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_search PRIVATE toolbelt sha1 hibp flat_file)

add_executable(hibp_server app/hibp_server.cpp)
target_compile_options(hibp_server PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_server PRIVATE toolbelt sha1 hibp flat_file restinio)

add_executable(hibp_sort app/hibp_sort.cpp)
target_compile_options(hibp_sort PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_sort PRIVATE toolbelt hibp flat_file)

add_executable(hibp_convert app/hibp_convert.cpp)
target_compile_options(hibp_convert PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_convert PRIVATE toolbelt sha1 hibp flat_file)

add_executable(hibp_join app/hibp_join.cpp)
target_compile_options(hibp_join PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_join PRIVATE hibp flat_file)

option(HIBP_TEST "Enable HIBP tests" OFF)
if(HIBP_TEST)
  enable_testing()
  add_subdirectory(test)
else(HIBP_TEST)
  message(STATUS "HIBP Tests are disabled. Set HIBP_TEST to ON to run tests.")
endif(HIBP_TEST)

# benchmarks
option(HIBP_BENCH "Build HIBP benchmarks" OFF)
if(HIBP_BENCH)
  add_subdirectory(bench)
else(HIBP_BENCH)
  message(STATUS "Building HIBP benchmarks is disabled. Set HIBP_BENCH to ON to build them.")
endif(HIBP_BENCH)

