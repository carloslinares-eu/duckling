# Third-party dependencies, fetched at configure time and built statically.
#
# Provides:
#   duckling::hidapi            - hidapi (hidraw backend on Linux, native on Windows)
#   tomlplusplus::tomlplusplus  - toml++ (header-only)
#   doctest::doctest            - doctest (header-only, tests only)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# hidapi 0.14.0 and doctest 2.4.11 declare cmake_minimum_required() < 3.5,
# which CMake 4.x rejects outright (issue #1). Let them configure with 3.5
# policies; CMake 3.x ignores this variable. Unset again at the end of file.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# --- hidapi 0.14.0 ---------------------------------------------------------------
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)   # Linux: hidraw only
set(HIDAPI_WITH_HIDRAW ON  CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
set(HIDAPI_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(hidapi
  GIT_REPOSITORY https://github.com/libusb/hidapi.git
  GIT_TAG        hidapi-0.14.0
  GIT_SHALLOW    TRUE
  SYSTEM)
# hidapi declares option(BUILD_SHARED_LIBS ... ON) under an old policy level;
# force a static build for hidapi only, without touching the user's cache.
block(SCOPE_FOR VARIABLES)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_MakeAvailable(hidapi)
endblock()

add_library(duckling_hidapi INTERFACE)
if(WIN32)
  target_link_libraries(duckling_hidapi INTERFACE hidapi::winapi)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(duckling_hidapi INTERFACE hidapi::hidraw)
else()
  message(FATAL_ERROR "duckling supports Linux and Windows only")
endif()
add_library(duckling::hidapi ALIAS duckling_hidapi)

# --- toml++ 3.4.0 ----------------------------------------------------------------
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE
  SYSTEM)
FetchContent_MakeAvailable(tomlplusplus)

# --- doctest 2.4.11 (tests only) -------------------------------------------------
if(DUCKLING_BUILD_TESTS)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
    SYSTEM)
  FetchContent_MakeAvailable(doctest)
endif()

unset(CMAKE_POLICY_VERSION_MINIMUM)
