/**
 * @file test_build_info.cpp
 * @brief Smoke tests for the build skeleton: version header and hidapi link.
 */
#include <doctest/doctest.h>

#include <duckling/version.hpp>
#include <hidapi.h>

#include "app/build_info.hpp"

TEST_CASE("version constants match the version string") {
    const std::string expected = std::to_string(duckling::kVersionMajor) + "." +
                                 std::to_string(duckling::kVersionMinor) + "." +
                                 std::to_string(duckling::kVersionPatch);
    CHECK(duckling::kVersion == expected);
}

TEST_CASE("buildInfo names the version and the linked hidapi") {
    const std::string info = duckling::app::buildInfo();
    CHECK(info.starts_with("duckling "));
    CHECK(info.find(duckling::kVersion) != std::string::npos);
    CHECK(info.find(std::string("hidapi ") + HID_API_VERSION_STR) != std::string::npos);
}
