include(EmbedFixtures)
duckling_embed_fixtures("${CMAKE_CURRENT_BINARY_DIR}/hw_logs_data.cpp"
  "${PROJECT_SOURCE_DIR}/hw-logs/*.txt")

add_executable(duckling_tests
  test_main.cpp
  test_build_info.cpp
  test_events.cpp
  test_features.cpp
  test_hex.cpp
  test_receiver.cpp
  test_receiver_info.cpp
  test_report.cpp
  test_sniff.cpp
  test_status.cpp
  test_switch.cpp
  fixtures.cpp
  test_fixtures.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/hw_logs_data.cpp"
)
target_include_directories(duckling_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duckling_tests PRIVATE duckling_lib doctest::doctest)
duckling_set_warnings(duckling_tests)

# Make doctest include the real <ostream> etc. instead of forward-declaring
# std types; newer MSVC STLs need the complete types (issue #2).
target_compile_definitions(duckling_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

add_test(NAME duckling_tests COMMAND duckling_tests)

# Manual hardware probe for the receiver I/O (task 1.3). Needs a real Bolt
# receiver, so it is built but not registered with ctest.
add_executable(duckling_hwprobe hw_probe.cpp)
target_link_libraries(duckling_hwprobe PRIVATE duckling_lib)
duckling_set_warnings(duckling_hwprobe)
