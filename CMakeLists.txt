cmake_minimum_required(VERSION 3.28)
project(hibp)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  set(PROJECT_COMPILE_OPTIONS /W3)
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " /fsanitize=address")
else() # unix'ish
  set(PROJECT_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Wshadow -Wextra-semi
    # -ftime-trace # for compile time reporting
    -Wmissing-noreturn -Wconversion -Wsign-conversion -Wno-ignored-attributes)

  # sanatizers
  if(CMAKE_BUILD_TYPE STREQUAL "debug")
    if (NOT MINGW) # sanitizers are not working under mingw
      if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	message(WARNING "FreeBSD detected: skipping santizers which cause weird errors in OpenSSL")
      else()
	string(APPEND CMAKE_CXX_FLAGS_DEBUG " -fsanitize=address,undefined,leak")
	# string(APPEND CMAKE_CXX_FLAGS_DEBUG " -fsanitize=thread") # alternative to above
      endif()
    endif()
  endif()
  

  # mold
  if (NOT (MINGW OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")) # mold not "compatible with mingw|FreeBSD toolchain"
    if(CMAKE_VERSION VERSION_LESS "3.29") 
      message(WARNING "Please consider upgrading to CMake 3.30 to enable use of faster `mold` linker!")
    else()
      set(CMAKE_LINKER_TYPE MOLD) 
    endif()
  endif()
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  # add the `local` paths common on FreeBSD
  include_directories(/usr/local/include)
  link_directories(/usr/local/lib)
endif()

# consitently we go for static
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # for restioni which has an option()
set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)

set(CLI11_PRECOMPILED ON) # improves compile speed by ~2x
add_subdirectory(ext/CLI11)
target_precompile_headers(CLI11 INTERFACE [["CLI/CLI.hpp"]]) # INTERFACE, because perpetuate to clients of this lib

add_library(sha1 ext/hash-library/sha1.cpp)
target_include_directories(sha1 INTERFACE ext/hash-library)
if (NOT MSVC)
  target_compile_options(sha1 INTERFACE -Wno-sign-conversion)
endif()

add_library(arrcmp INTERFACE)
target_include_directories(arrcmp INTERFACE include)
target_compile_features(arrcmp INTERFACE cxx_std_20)

add_library(hibp INTERFACE)
target_include_directories(hibp INTERFACE include)
target_compile_features(hibp INTERFACE cxx_std_20)
target_link_libraries(hibp INTERFACE arrcmp)

find_package(TBB REQUIRED)

add_library(flat_file INTERFACE)
target_include_directories(flat_file INTERFACE include)
target_compile_features(flat_file INTERFACE cxx_std_20)
target_precompile_headers(flat_file INTERFACE [["flat_file.hpp"]]) # INTERFACE, because perpetuate to clients of this lib

if (MSVC)
  target_link_libraries(flat_file INTERFACE tBB::tbb)
elseif (MINGW)
  target_link_libraries(flat_file INTERFACE tbb12)
else()
  target_link_libraries(flat_file INTERFACE tbb)
endif()

set(RESTINIO_EXPLICIT_CPPSTD 20)
set(RESTINIO_ASIO_SOURCE standalone)
set(asio_INCLUDE_DIRS ../asio/include) # relative to ext/restinio/dev/restinio
add_subdirectory(ext/restinio/dev/nodejs/llhttp)
add_subdirectory(ext/restinio/dev/fmt)
add_subdirectory(ext/restinio/dev/expected-lite)
add_subdirectory(ext/restinio/dev/restinio)
# be more forgiving with errors
if (NOT MSVC)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(restinio INTERFACE -Wno-shadow -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-extra-semi -Wno-conversion -Wno-dangling-reference)
  else()
    target_compile_options(restinio INTERFACE -Wno-shadow -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-extra-semi -Wno-conversion)
  endif()
  target_compile_options(expected-lite INTERFACE -Wno-missing-noreturn)
endif()
target_precompile_headers(restinio INTERFACE [["restinio/core.hpp"]]) # INTERFACE, because perpetuate to clients of this lib

add_executable(hibp_search app/hibp_search.cpp src/toc.cpp)
target_compile_features(hibp_search PRIVATE cxx_std_20)
target_compile_options(hibp_search PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_search PRIVATE CLI11 sha1 hibp flat_file)

find_package(Threads)

add_executable(hibp_server app/hibp_server.cpp src/toc.cpp)
target_compile_options(hibp_server PRIVATE ${PROJECT_COMPILE_OPTIONS})
if (MINGW)
  target_link_libraries(hibp_server PRIVATE CLI11 sha1 hibp flat_file restinio gdi32 wsock32 ws2_32)
else()
  target_link_libraries(hibp_server PRIVATE CLI11 sha1 hibp flat_file restinio ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(hibp_sort app/hibp_sort.cpp)
target_compile_options(hibp_sort PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_sort PRIVATE hibp flat_file)

add_executable(hibp_convert app/hibp_convert.cpp)
target_compile_options(hibp_convert PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_convert PRIVATE sha1 hibp flat_file)

add_executable(hibp_join app/hibp_join.cpp)
target_compile_options(hibp_join PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_join PRIVATE hibp flat_file)

# don't seem to need these? 
# find_package(CURL REQUIRED)
# pkg_search_module(LIBEVENT libevent)

add_executable(hibp_download
  app/hibp_download.cpp
  src/download/download.cpp
  src/download/queuemgt.cpp
  src/download/requests.cpp
  src/download/shared.cpp)
target_compile_features(hibp_download PRIVATE cxx_std_20)
target_compile_options(hibp_download PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_download PRIVATE CLI11 curl event hibp flat_file ${CMAKE_THREAD_LIBS_INIT})

option(HIBP_TEST "Enable HIBP tests" OFF)
if(HIBP_TEST)
  enable_testing()
  add_subdirectory(test)
else(HIBP_TEST)
  message(STATUS "HIBP Tests are disabled. Set HIBP_TEST to ON to run tests.")
endif(HIBP_TEST)
