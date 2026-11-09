/**
 * @file hw_probe.cpp
 * @brief Manual hardware check for io::Receiver (task 1.3). Not run by ctest
 *        because CI has no receiver.
 *
 * Usage: `duckling_hwprobe [listen_seconds]`
 *  1. Lists Bolt receivers and their HID++ collection paths.
 *  2. Opens the first one and pings device slots 1..6. For each device that
 *     answers, reads its features, current host, host name table and name
 *     (task 1.4). Read-only: it never switches a device.
 *  3. Optionally prints every report and parsed event for N seconds.
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "hidpp/events.hpp"
#include "hidpp/features.hpp"
#include "hidpp/hex.hpp"
#include "io/receiver.hpp"

using namespace duckling;
using namespace std::chrono_literals;

/**
 * @brief Print what the hidpp feature calls report for one device.
 * @param rq    Requester (the open receiver).
 * @param index Device slot that answered the ping.
 */
static void printFeatures(hidpp::Requester& rq, std::uint8_t index) {
    const auto f = hidpp::discover(rq, index);
    if (!f) {
        std::printf("    features: %s\n", hidpp::describe(f.failure()).c_str());
        return;
    }
    std::printf("    CHANGE_HOST index 0x%02x v%u, HOSTS_INFO 0x%02x, DEVICE_NAME 0x%02x\n", f->changeHost,
                f->changeHostVersion, f->hostsInfo, f->deviceName);
    if (f->deviceName != 0) {
        const auto name = hidpp::getDeviceName(rq, index, f->deviceName);
        std::printf("    name: %s\n", name ? name->c_str() : hidpp::describe(name.failure()).c_str());
    }
    if (const auto host = hidpp::getHostInfo(rq, index, f->changeHost)) {
        std::printf("    hosts: %u, current: ch%u\n", host->count, host->current + 1);
    } else {
        std::printf("    host info: %s\n", hidpp::describe(host.failure()).c_str());
    }
    if (f->hostsInfo != 0) {
        const auto names = hidpp::getHostNames(rq, index, f->hostsInfo);
        if (!names) {
            std::printf("    host names: %s\n", hidpp::describe(names.failure()).c_str());
            return;
        }
        for (const auto& h : *names) {
            std::printf("    ch%u: %s%s\n", h.host + 1, h.name.c_str(),
                        !h.paired ? "(not paired)" : h.nameRead ? "" : "(name not readable)");
        }
    }
}

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

    for (std::uint8_t i = 1; i <= hidpp::kMaxDeviceIndex; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto version = hidpp::ping(*rx, i);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        if (!version) {
            std::printf("  slot %u: %s (%lld ms)\n", i, hidpp::describe(version.failure()).c_str(),
                        static_cast<long long>(ms));
            continue;
        }
        std::printf("  slot %u: HID++ %u.%u (%lld ms)\n", i, version->major, version->minor,
                    static_cast<long long>(ms));
        printFeatures(*rx, i);
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
