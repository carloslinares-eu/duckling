/**
 * @file build_info.hpp
 * @brief Human-readable build information for `duckling version`.
 */
#pragma once

#include <string>

/**
 * @namespace duckling::app
 * @brief Command-line front end: argument parsing and subcommands.
 */
namespace duckling::app {

/**
 * @brief Describe this build in one line.
 *
 * Includes the duckling version, the target platform and the version of the
 * statically linked hidapi, which also proves the dependency links.
 *
 * @return For example `"duckling 0.1.0 (linux, hidapi 0.14.0)"`.
 */
[[nodiscard]] std::string buildInfo();

}  // namespace duckling::app
