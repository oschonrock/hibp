cmake_minimum_required(VERSION 3.16)
project(hibp)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
endif()

# LTO -- quite slow, enable if you think you benefit
option(HIBP_LTO "Enable link time optimisation" OFF)
if(HIBP_LTO)
  set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  # add the `local` paths common on FreeBSD
  include_directories(/usr/local/include)
  link_directories(/usr/local/lib)
endif()

# consitently we go for static to reduce dependencies when distributing binaries
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # for restioni which has an option()
set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)

set(CLI11_PRECOMPILED ON) # improves compile speed by ~2x
add_subdirectory(ext/CLI11)

add_library(sha1 ext/hash-library/sha1.cpp)
target_include_directories(sha1 INTERFACE ext/hash-library)
if (NOT MSVC)
  target_compile_options(sha1 INTERFACE -Wno-sign-conversion)
endif()

add_subdirectory(ext/fmt EXCLUDE_FROM_ALL)

add_library(arrcmp INTERFACE)
target_include_directories(arrcmp INTERFACE include)
target_compile_features(arrcmp INTERFACE cxx_std_20)

add_library(hibp INTERFACE)
target_include_directories(hibp INTERFACE include)
target_compile_features(hibp INTERFACE cxx_std_20)
target_link_libraries(hibp INTERFACE arrcmp)

option(HIBP_WITH_PSTL "Use parallel STL for sorting. Requires libtbb." OFF)
if (HIBP_WITH_PSTL)
  add_compile_definitions(HIBP_USE_PSTL)
  find_package(TBB REQUIRED)
endif()

add_library(flat_file INTERFACE)
target_include_directories(flat_file INTERFACE include)
target_compile_features(flat_file INTERFACE cxx_std_20)
target_link_libraries(flat_file INTERFACE fmt::fmt)

if (HIBP_WITH_PSTL)
  if (MSVC)
    target_link_libraries(flat_file INTERFACE tBB::tbb)
  elseif (MINGW)
    target_link_libraries(flat_file INTERFACE tbb12)
  else()
    target_link_libraries(flat_file INTERFACE tbb)
  endif()
endif()

set(RESTINIO_EXPLICIT_CPPSTD 20)
set(RESTINIO_ASIO_SOURCE standalone)
set(asio_INCLUDE_DIRS ../asio/include) # relative to ext/restinio/dev/restinio
add_subdirectory(ext/restinio/dev/nodejs/llhttp EXCLUDE_FROM_ALL)
# using our own fmtlib which restinio will reuse
# add_subdirectory(ext/restinio/dev/fmt)
add_subdirectory(ext/restinio/dev/expected-lite EXCLUDE_FROM_ALL)
add_subdirectory(ext/restinio/dev/restinio)
# be more forgiving with errors for restinion
if (NOT MSVC)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(restinio INTERFACE -Wno-shadow -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-extra-semi -Wno-conversion -Wno-dangling-reference)
  else()
    target_compile_options(restinio INTERFACE -Wno-shadow -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-extra-semi -Wno-conversion)
  endif()
  target_compile_options(expected-lite INTERFACE -Wno-missing-noreturn)
endif()

add_library(ntlm src/ntlm.cpp src/md4.c)
target_compile_features(ntlm PRIVATE cxx_std_20)
target_include_directories(ntlm PRIVATE include)

add_library(toc src/toc.cpp)
target_compile_features(toc PRIVATE cxx_std_20)
target_include_directories(toc PRIVATE include)
target_compile_options(toc PRIVATE -Wno-ignored-attributes) # non-sensical warning from gcc?
target_link_libraries(toc PRIVATE hibp flat_file fmt::fmt)

add_executable(hibp_search app/hibp_search.cpp)
set_target_properties(hibp_search PROPERTIES OUTPUT_NAME hibp-search)
target_compile_features(hibp_search PRIVATE cxx_std_20)
target_compile_options(hibp_search PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_search PRIVATE CLI11 sha1 ntlm hibp toc flat_file fmt::fmt)

find_package(Threads)

add_executable(hibp_server app/hibp_server.cpp src/srv/server.cpp)
set_target_properties(hibp_server PROPERTIES OUTPUT_NAME hibp-server)
target_compile_options(hibp_server PRIVATE ${PROJECT_COMPILE_OPTIONS})
if (MINGW)
  target_link_libraries(hibp_server PRIVATE CLI11 sha1 ntlm hibp toc flat_file fmt::fmt restinio gdi32 wsock32 ws2_32)
else()
  target_link_libraries(hibp_server PRIVATE CLI11 sha1 ntlm hibp toc flat_file fmt::fmt restinio ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable(hibp_sort app/hibp_sort.cpp)
set_target_properties(hibp_sort PROPERTIES OUTPUT_NAME hibp-sort)
target_compile_options(hibp_sort PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_sort PRIVATE CLI11 hibp flat_file fmt::fmt)

add_executable(hibp_topn app/hibp_topn.cpp)
set_target_properties(hibp_topn PROPERTIES OUTPUT_NAME hibp-topn)
target_compile_options(hibp_topn PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_topn PRIVATE CLI11 sha1 hibp flat_file fmt::fmt)

add_executable(hibp_convert app/hibp_convert.cpp)
set_target_properties(hibp_convert PROPERTIES OUTPUT_NAME hibp-convert)
target_compile_options(hibp_convert PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_convert PRIVATE CLI11 sha1 hibp flat_file fmt::fmt)

add_executable(hibp_download
  app/hibp_download.cpp
  src/dnl/resume.cpp
  src/dnl/queuemgt.cpp
  src/dnl/requests.cpp
  src/dnl/shared.cpp)
set_target_properties(hibp_download PROPERTIES OUTPUT_NAME hibp-download)
target_compile_features(hibp_download PRIVATE cxx_std_20)
target_compile_options(hibp_download PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(hibp_download PRIVATE CLI11 curl event hibp flat_file fmt::fmt ${CMAKE_THREAD_LIBS_INIT})

option(NOPCH "Disable the use of precompiled headers" OFF)

if (NOT NOPCH AND NOT CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  target_precompile_headers(CLI11 INTERFACE [["CLI/CLI.hpp"]])
  target_precompile_headers(flat_file INTERFACE [["flat_file.hpp"]])
  target_precompile_headers(restinio INTERFACE [["restinio/core.hpp"]])

  target_precompile_headers(hibp_sort REUSE_FROM hibp_search)
  target_precompile_headers(hibp_convert REUSE_FROM hibp_search)
  target_precompile_headers(hibp_topn REUSE_FROM hibp_search)
  target_precompile_headers(hibp_download REUSE_FROM hibp_search)
endif()

# testing

option(HIBP_TEST "Enable HIBP tests" OFF)
if(HIBP_TEST)
  
  add_executable(mock_api_server test/mock_api_server.cpp)
  target_compile_options(mock_api_server PRIVATE ${PROJECT_COMPILE_OPTIONS})
  if (MINGW)
    target_link_libraries(mock_api_server PRIVATE fmt::fmt restinio gdi32 wsock32 ws2_32)
  else()
    target_link_libraries(mock_api_server PRIVATE fmt::fmt restinio ${CMAKE_THREAD_LIBS_INIT})
  endif()

  get_property(all_targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
  
  enable_testing()
  add_subdirectory(test)
else(HIBP_TEST)
  message(STATUS "HIBP Tests are disabled. Set HIBP_TEST to ON to run tests.")
endif(HIBP_TEST)

install(TARGETS hibp_download hibp_sort hibp_search hibp_convert hibp_server hibp_topn RUNTIME)


