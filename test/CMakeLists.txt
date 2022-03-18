find_package(PkgConfig)
pkg_search_module(GTEST REQUIRED gtest_main)

include_directories(${CMAKE_SOURCE_DIR}/include)
link_libraries(${GTEST_LDFLAGS})
add_compile_options(${GTEST_CFLAGS})
  
function (add_unit_test UNIT_TEST)
    set(UNIT_TESTS ${UNIT_TESTS} ${UNIT_TEST} PARENT_SCOPE)
    add_executable(${UNIT_TEST} ${UNIT_TEST}.cpp)
    target_compile_options(${UNIT_TEST} PRIVATE ${PROJECT_COMPILE_OPTIONS})
    add_test(NAME ${UNIT_TEST} COMMAND ${UNIT_TEST})
endfunction()

# very slow to compile in release mode
add_unit_test(test_arrcmp)

add_custom_target(all_tests ALL DEPENDS ${UNIT_TESTS})

add_custom_command(
  TARGET all_tests
  COMMENT "Run tests"
  POST_BUILD 
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND GTEST_COLOR=1 ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION> --output-on-failure # --verbose
  )
