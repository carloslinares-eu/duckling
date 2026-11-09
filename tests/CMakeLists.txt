add_executable(duckling_tests
  test_main.cpp
  test_build_info.cpp
)
target_link_libraries(duckling_tests PRIVATE duckling_lib doctest::doctest)
duckling_set_warnings(duckling_tests)

add_test(NAME duckling_tests COMMAND duckling_tests)
