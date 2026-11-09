/**
 * @file hw_probe.cpp
 * @brief Manual hardware check for io::Receiver (task 1.3). Not run by ctest
 *        because CI has no receiver.
 *
 * Usage: `duckling_hwprobe [listen_seconds]`
 *  1. Lists Bolt receivers and their HID++ collection paths.
 *  2. Opens the first one and pings device slots 1..6 (ROOT getProtocolVersion).
 *  3. Optionally prints every report and parsed event for N seconds.
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "hidpp/events.hpp"
#include "hidpp/hex.hpp"
#include "io/receiver.hpp"

using namespace duckling;
using namespace std::chrono_literals;

/**
 * @brief Entry point.
 * @param argc Argument count.
 * @param argv `argv[1]`: optional seconds to listen for reports.
 * @return 0 if a receiver was opened, 1 otherwise.
 */
int main(int argc, char* argv[]) {
    const int listenSeconds = argc > 1 ? std::atoi(argv[1]) : 0;

    const auto receivers = io::findReceivers();
    std::printf("receivers found: %zu\n", receivers.size());
    for (const auto& r : receivers) {
        std::printf("  pid %04x serial '%s' shared handle: %s\n    short: %s\n    long:  %s\n",
                    r.productId, r.serial.c_str(), r.sharedHandle() ? "yes" : "no",
                    r.shortPath.c_str(), r.longPath.c_str());
    }

    auto rx = io::Receiver::openFirst();
    if (!rx) {
        std::printf("could not open a receiver (permissions?)\n");
        return 1;
    }
    std::printf("opened\n");

    const std::uint8_t ping[] = {0x00, 0x00, 0xAA};
    for (std::uint8_t i = 1; i <= hidpp::kMaxDeviceIndex; ++i) {
        const auto req = hidpp::Report::make(hidpp::ReportId::Long, i, 0x00, 1, hidpp::kDucklingSwId, ping);
        const auto t0 = std::chrono::steady_clock::now();
        const auto reply = rx->request(req);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        if (!reply) {
            std::printf("  slot %u: no reply (%lld ms)\n", i, static_cast<long long>(ms));
        } else if (reply->isError()) {
            std::printf("  slot %u: error %s (%lld ms)\n", i, hidpp::toHex(reply->bytes()).c_str(),
                        static_cast<long long>(ms));
        } else {
            std::printf("  slot %u: HID++ %u.%u, echo %02x (%lld ms)\n", i, reply->param(0),
                        reply->param(1), reply->param(2), static_cast<long long>(ms));
        }
    }

    if (listenSeconds > 0) {
        std::printf("listening %d s...\n", listenSeconds);
        hidpp::EventParser parser;
        parser.setChangeHostFeature(1, 0xB366, 9);   // MX Mechanical (C1/C4)
        parser.setChangeHostFeature(2, 0xB034, 10);  // MX Master 3S
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(listenSeconds);
        while (std::chrono::steady_clock::now() < end && rx->isOpen()) {
            auto r = rx->read(200ms);
            if (!r) continue;
            std::printf("  %s", hidpp::toHex(r->bytes()).c_str());
            if (auto e = parser.parse(*r)) {
                if (auto* c = std::get_if<hidpp::DeviceConnection>(&*e)) {
                    std::printf("   <- slot %u %s", c->deviceIndex, c->online ? "online" : "OFFLINE");
                } else if (auto* h = std::get_if<hidpp::HostChange>(&*e)) {
                    std::printf("   <- slot %u switching host %u -> %u", h->deviceIndex, h->fromHost, h->toHost);
                }
            }
            std::printf("\n");
        }
    }
    std::printf("receiver %s\n", rx->isOpen() ? "still open" : "closed (I/O error)");
    return 0;
}
