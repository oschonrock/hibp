

add_library(asmlib INTERFACE)
target_include_directories(asmlib INTERFACE ${CMAKE_SOURCE_DIR}/ext/asmlib)
target_link_directories(asmlib INTERFACE ${CMAKE_SOURCE_DIR}/ext/asmlib)
target_link_libraries(asmlib INTERFACE aelf64)

find_package(benchmark REQUIRED)

add_executable(arrcmp arrcmp.cpp)
target_compile_options(arrcmp PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(arrcmp fmt hibp asmlib benchmark::benchmark)

add_executable(ordering ordering.cpp)
target_compile_options(ordering PRIVATE ${PROJECT_COMPILE_OPTIONS})
target_link_libraries(ordering fmt hibp asmlib benchmark::benchmark)


