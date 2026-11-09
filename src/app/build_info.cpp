/**
 * @file build_info.cpp
 * @brief Implementation of duckling::app::buildInfo().
 */
#include "app/build_info.hpp"

#include <duckling/version.hpp>
#include <hidapi.h>

namespace duckling::app {

namespace {

/// Name of the platform this binary was compiled for.
constexpr const char* kPlatform =
#if defined(_WIN32)
    "windows";
#elif defined(__linux__)
    "linux";
#else
    "unknown";
#endif

}  // namespace

std::string buildInfo() {
    std::string info = "duckling ";
    info += kVersion;
    info += " (";
    info += kPlatform;
    info += ", hidapi ";
    info += hid_version_str();
    info += ")";
    return info;
}

}  // namespace duckling::app
