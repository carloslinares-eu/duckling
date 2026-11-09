/**
 * @file cli_common.cpp
 * @brief Implementation of the shared CLI helpers.
 */
#include "app/cli_common.hpp"

#include <cstdio>

namespace duckling::app {

const char* kindName(hidpp::DeviceKind kind) noexcept {
    switch (kind) {
        case hidpp::DeviceKind::Keyboard:
            return "keyboard";
        case hidpp::DeviceKind::Mouse:
            return "mouse";
        default:
            return "device";
    }
}

std::string printable(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) c = '?';
    }
    return out;
}

std::optional<io::Receiver> openReceiverOrExplain() {
    const auto receivers = io::findReceivers();
    if (receivers.empty()) {
        std::fprintf(stderr, "duckling: no Logi Bolt receiver found\n");
        return std::nullopt;
    }
    for (const auto& paths : receivers) {
        if (auto rx = io::Receiver::open(paths)) return rx;
    }
    std::fprintf(stderr, "duckling: found a Logi Bolt receiver but can't open it:\n");
    for (const auto& paths : receivers) {
        std::fprintf(stderr, "  %s\n", paths.shortPath.c_str());
        if (!paths.sharedHandle()) std::fprintf(stderr, "  %s\n", paths.longPath.c_str());
    }
    std::fprintf(stderr, "On Linux, the user needs access to its hidraw node (a udev rule).\n");
    return std::nullopt;
}

}  // namespace duckling::app
