include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/release-1.11.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

include_directories(${CMAKE_SOURCE_DIR}/include)
  
function (add_unit_test UNIT_TEST)
    set(UNIT_TESTS ${UNIT_TESTS} ${UNIT_TEST} PARENT_SCOPE)
    add_executable(${UNIT_TEST} ${UNIT_TEST}.cpp)
    target_compile_options(${UNIT_TEST} PRIVATE ${PROJECT_COMPILE_OPTIONS})
    target_compile_features(${UNIT_TEST} PRIVATE cxx_std_20)
    target_link_libraries(${UNIT_TEST} PRIVATE gtest_main)
    gtest_discover_tests(${UNIT_TEST})
endfunction()

# very slow to compile in release mode
add_unit_test(test_arrcmp)

add_custom_target(all_tests ALL DEPENDS ${UNIT_TESTS})

add_custom_command(
  TARGET all_tests
  COMMENT "Run tests"
  POST_BUILD 
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION> --output-on-failure # --verbose
  )
