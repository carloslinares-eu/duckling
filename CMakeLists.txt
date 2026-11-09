cmake_minimum_required(VERSION 3.25)

project(duckling
  VERSION 0.1.0
  DESCRIPTION "Logitech mouse and keyboard follow each other across hosts"
  HOMEPAGE_URL "https://github.com/carloslinares-eu/duckling"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DUCKLING_BUILD_TESTS "Build the unit tests" ON)
option(DUCKLING_STATIC_RUNTIME "Link the C/C++ runtime statically" ON)

# Static runtime for every target (deps, duckling, tests) so they all agree.
if(DUCKLING_STATIC_RUNTIME)
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  elseif(MINGW)
    add_link_options(-static)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # glibc and libudev stay dynamic; libstdc++/libgcc are baked in.
    add_link_options(-static-libstdc++ -static-libgcc)
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Deps)
include(Warnings)

# --- generated version header -------------------------------------------------
configure_file(src/version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/duckling/version.hpp" @ONLY)

# --- core library: everything except main(), so tests can link it -------------
add_library(duckling_lib STATIC
  src/app/build_info.cpp
  src/app/status.cpp
  src/hidpp/events.cpp
  src/hidpp/features.cpp
  src/hidpp/hex.cpp
  src/hidpp/receiver_info.cpp
  src/hidpp/report.cpp
  src/io/receiver.cpp
)
target_include_directories(duckling_lib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(duckling_lib PUBLIC
  duckling::hidapi
  tomlplusplus::tomlplusplus)
duckling_set_warnings(duckling_lib)

# --- executable ----------------------------------------------------------------
add_executable(duckling src/main.cpp)
target_link_libraries(duckling PRIVATE duckling_lib)
duckling_set_warnings(duckling)

install(TARGETS duckling RUNTIME DESTINATION bin)

# --- tests ---------------------------------------------------------------------
if(DUCKLING_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
