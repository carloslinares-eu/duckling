add_executable(duckling_tests
  test_main.cpp
  test_build_info.cpp
)
target_link_libraries(duckling_tests PRIVATE duckling_lib doctest::doctest)
duckling_set_warnings(duckling_tests)

# Make doctest include the real <ostream> etc. instead of forward-declaring
# std types; newer MSVC STLs need the complete types (issue #2).
target_compile_definitions(duckling_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

add_test(NAME duckling_tests COMMAND duckling_tests)
