/**
 * @file status.cpp
 * @brief Implementation of `duckling status`.
 */
#include "app/status.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "app/cli_common.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace duckling::app {

namespace {

/**
 * @brief printf into a std::string.
 * @param out Where to append.
 * @param fmt Format string.
 * @param ... Arguments.
 */
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void appendf(std::string& out, const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    out += buf;
}

/**
 * @brief Read everything about a device that answered the ping.
 * @param rq Receiver.
 * @param d  Device to fill in.
 */
void readDevice(hidpp::Requester& rq, DeviceStatus& d) {
    const std::uint8_t slot = d.pairing.slot;
    auto features = hidpp::discover(rq, slot);
    if (!features) {
        d.problems.push_back("features: " + hidpp::describe(features.failure()));
        return;
    }
    d.features = *features;

    if (features->deviceName != 0) {
        if (auto name = hidpp::getDeviceName(rq, slot, features->deviceName)) {
            d.name = *name;
        } else {
            d.problems.push_back("name: " + hidpp::describe(name.failure()));
        }
    }
    if (auto host = hidpp::getHostInfo(rq, slot, features->changeHost)) {
        d.host = *host;
    } else {
        d.problems.push_back("current host: " + hidpp::describe(host.failure()));
    }
    if (features->hostsInfo != 0) {
        if (auto names = hidpp::getHostNames(rq, slot, features->hostsInfo)) {
            d.hostNames = *names;
        } else if (names.failure().kind != hidpp::FailureKind::Unsupported) {
            d.problems.push_back("host names: " + hidpp::describe(names.failure()));
        }
    }
}

}  // namespace

StatusReport collectStatus(hidpp::Requester& rq, std::string receiver, std::string thisHost) {
    StatusReport report{std::move(receiver), std::move(thisHost), {}, {}};
    for (std::uint8_t slot = 1; slot <= hidpp::kMaxDeviceIndex; ++slot) {
        auto pairing = hidpp::getPairingInfoConfirmed(rq, slot);
        if (!pairing) {
            // r1: empty slots answer HID++ 1.0 error 0x08.
            if (!pairing.failure().unknownDevice()) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "slot %u: %s", slot,
                              hidpp::describe(pairing.failure()).c_str());
                report.problems.emplace_back(buf);
            }
            continue;
        }
        DeviceStatus d;
        d.pairing = *pairing;
        if (auto codename = hidpp::getCodename(rq, slot)) {
            d.codename = *codename;
        } else {
            d.problems.push_back("codename: " + hidpp::describe(codename.failure()));
        }

        if (d.pairing.linkUp) {
            if (auto version = hidpp::ping(rq, slot)) {
                d.version = *version;
                readDevice(rq, d);
            } else {
                d.pingFailure = version.failure();
            }
        }
        report.devices.push_back(std::move(d));
    }
    return report;
}

std::string formatStatus(const StatusReport& report) {
    std::string out;
    appendf(out, "receiver   Logi Bolt  %s\n", report.receiver.c_str());
    appendf(out, "this host  %s\n", report.thisHost.empty() ? "(unknown)" : report.thisHost.c_str());

    if (report.devices.empty()) out += "\nno paired devices\n";
    for (const auto& d : report.devices) {
        const std::string name = printable(d.name.empty() ? d.codename : d.name);
        appendf(out, "\nslot %u  %-8s  %s  (wpid %04X, serial %08X)\n", d.pairing.slot,
                kindName(d.pairing.kind()), name.empty() ? "?" : name.c_str(), d.pairing.wpid,
                static_cast<unsigned>(d.pairing.serial));

        if (!d.pairing.linkUp) {
            out += "        not connected here (on another host, or off)\n";
        } else if (!d.here()) {
            appendf(out, "        not answering: %s\n", hidpp::describe(*d.pingFailure).c_str());
        } else {
            appendf(out, "        connected here, HID++ %u.%u", d.version->major, d.version->minor);
            if (d.host) {
                appendf(out, ", this host is ch%u of %u", d.host->current + 1, d.host->count);
            }
            out += "\n";
        }

        for (const auto& h : d.hostNames) {
            const char* label = !h.paired ? "(not paired)" : !h.nameRead ? "(name not readable)" : "";
            const bool current = d.host && d.host->current == h.host;
            appendf(out, "        ch%u  %s%s%s\n", h.host + 1, printable(h.name).c_str(), label,
                    current ? "   <- this host" : "");
        }
        for (const auto& p : d.problems) appendf(out, "        ! %s\n", p.c_str());
    }
    for (const auto& p : report.problems) appendf(out, "\n! %s\n", p.c_str());
    return out;
}

std::string localHostName() {
    std::string name;
#ifdef _WIN32
    char buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof buf;
    if (GetComputerNameA(buf, &size) != 0) name.assign(buf, size);
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof buf - 1) == 0) name = buf;
#endif
    if (const auto dot = name.find('.'); dot != std::string::npos) name.erase(dot);
    return name;
}

int runStatus() {
    auto rx = openReceiverOrExplain();
    if (!rx) return 1;
    const auto report = collectStatus(*rx, rx->paths().shortPath, localHostName());
    std::fputs(formatStatus(report).c_str(), stdout);
    return report.problems.empty() ? 0 : 1;
}

}  // namespace duckling::app
