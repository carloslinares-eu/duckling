/**
 * @file sniff.cpp
 * @brief Implementation of `duckling sniff`.
 */
#include "app/sniff.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>

#include "app/cli_common.hpp"
#include "hidpp/features.hpp"
#include "hidpp/hex.hpp"
#include "hidpp/receiver_info.hpp"

namespace duckling::app {

namespace {

using Clock = std::chrono::steady_clock;

/**
 * @brief Who sent the request a reply answers, from its software id.
 *
 * duckling uses 0x1; Options+ uses 0xA/0xB (C4, C5, W1); Solaar cycles
 * 0x2..0xF (C2, C3, s1), so 0xA/0xB can be either.
 *
 * @param swId Software id.
 * @return "duckling", "Options+ or Solaar" or "Solaar?".
 * @see PLAN.md C4, C5 and the "Software id" note in the code design
 */
const char* sender(std::uint8_t swId) {
    if (swId == hidpp::kDucklingSwId) return "duckling";
    if (swId == 0xA || swId == 0xB) return "Options+ or Solaar";
    return "Solaar?";
}

/** @brief A line typed on stdin, with the time Enter was pressed. */
struct Marker {
    Clock::time_point when;  ///< When the line was read.
    std::string text;        ///< The line.
};

/** @brief Markers handed from the stdin thread to the main loop. */
struct Markers {
    std::mutex mutex;          ///< Guards lines.
    std::deque<Marker> lines;  ///< Not yet printed, in time order.
};

/**
 * @brief Whether a token is exactly two hex digits.
 * @param t Token.
 * @return True for e.g. "10", "ab".
 */
bool isHexPair(std::string_view t) {
    return t.size() == 2 && std::isxdigit(static_cast<unsigned char>(t[0])) != 0 &&
           std::isxdigit(static_cast<unsigned char>(t[1])) != 0;
}

}  // namespace

void SniffLabeler::addDevice(std::uint8_t slot, hidpp::DeviceKind kind, std::uint16_t wpid,
                             std::uint8_t changeHost) {
    if (slot < 1 || slot > hidpp::kMaxDeviceIndex) return;
    kinds_[slot] = kind;
    wpids_[slot] = wpid;
    known_[slot] = true;
    parser_.setChangeHostFeature(slot, wpid, changeHost);
}

void SniffLabeler::setKind(std::uint8_t slot, hidpp::DeviceKind kind, std::uint16_t wpid) noexcept {
    if (slot < 1 || slot > hidpp::kMaxDeviceIndex) return;
    if (known_[slot] && wpids_[slot] != wpid) known_[slot] = false;
    kinds_[slot] = kind;
    wpids_[slot] = wpid;
}

bool SniffLabeler::knows(std::uint8_t slot) const noexcept {
    return slot >= 1 && slot <= hidpp::kMaxDeviceIndex && known_[slot];
}

const char* SniffLabeler::kindOf(std::uint8_t slot) const noexcept {
    return slot <= hidpp::kMaxDeviceIndex ? kindName(kinds_[slot]) : "device";
}

std::string SniffLabeler::label(const hidpp::Report& r) {
    const std::uint8_t slot = r.deviceIndex();

    // 10 <slot> 41 <?> <status> <wpid lo> <wpid hi>; status bit 6 = link down (C2).
    if (auto c = hidpp::parseDeviceConnection(r)) {
        // A different wpid means re-pairing: EventParser forgets the index too.
        if (known_[slot] && c->wpid != wpids_[slot]) known_[slot] = false;
        kinds_[slot] = c->kind();
        wpids_[slot] = c->wpid;
        (void)parser_.parse(r);
        return formatText("%s %s (slot %u, wpid %04X)", kindName(c->kind()), c->online ? "online" : "OFFLINE",
                      slot, c->wpid);
    }
    // 11 <slot> <CHANGE_HOST> 00 <from> <to> ..., swId 0, button presses only (C2, W1, s2, s3).
    if (auto e = parser_.parse(r)) {
        if (const auto* h = std::get_if<hidpp::HostChange>(&*e)) {
            return formatText("%s switching ch%u -> ch%u (Easy-Switch)", kindOf(slot), h->fromHost + 1,
                          h->toHost + 1);
        }
    }
    if (r.id() == hidpp::ReportId::Short && r.featureIndex() == hidpp::kDeviceUnpairedSubId &&
        slot >= 1 && slot <= hidpp::kMaxDeviceIndex) {
        known_[slot] = false;
        return formatText("slot %u unpaired", slot);
    }
    if (r.featureIndex() == hidpp::kHidpp10ErrorSubId) {
        // 10 i 8F <sub/feat> <addr/fn|sw> <code> 00 (task 1.3 probe, r1)
        return formatText("receiver error for request 0x%02x/0x%02x: %s", r.functionByte(), r.param(0),
                      hidpp::describe(hidpp::Failure{hidpp::FailureKind::Hidpp10, r.param(1)}).c_str());
    }
    if (r.featureIndex() == hidpp::kHidpp20ErrorFeature) {
        // 11 i FF <feat> <fn|sw> <code> ...
        return formatText("%s error for feature 0x%02x function 0x%x: %s", kindOf(slot), r.functionByte(),
                      r.param(0) >> 4,
                      hidpp::describe(hidpp::Failure{hidpp::FailureKind::Hidpp20, r.param(1)}).c_str());
    }
    // Bolt register 0x2B5 replies, from anyone (r1, W1).
    if (const auto p = hidpp::parsePairingInfo(r)) {
        setKind(p->slot, p->kind(), p->wpid);
        return formatText("pairing info of slot %u: %s, link %s", p->slot, kindName(p->kind()),
                          p->linkUp ? "up" : "down");
    }
    if (const auto c = hidpp::parseCodename(r)) {
        return formatText("codename of slot %u: '%s'", c->slot, printable(c->name).c_str());
    }
    if (slot == hidpp::kReceiverIndex && r.featureIndex() == hidpp::kGetLongRegisterSubId &&
        r.functionByte() == hidpp::kReceiverInfoRegister) {
        return "receiver info register";
    }
    if (hidpp::isRegisterAccess(r.featureIndex())) return "receiver register access";
    // HID++ 1.0 notifications use sub-ids 0x40..0x7F in short reports (0x41
    // connection and 0x40 unpair are handled above).
    if (r.id() == hidpp::ReportId::Short && r.featureIndex() >= 0x40 && r.featureIndex() < 0x80) {
        return formatText("receiver notification 0x%02x", r.featureIndex());
    }
    if (r.swId() == 0) return formatText("%s notification", kindOf(slot));
    return formatText("%s reply, swId 0x%x (%s)", kindOf(slot), r.swId(), sender(r.swId()));
}

std::string fixtureSafe(std::string_view text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ' || text[i] == '\t') {
            out += text[i++];
            continue;
        }
        const std::size_t end = std::min(text.find_first_of(" \t", i), text.size());
        const std::string_view token = text.substr(i, end - i);
        if (isHexPair(token)) out += '#';
        out += printable(std::string(token));
        i = end;
    }
    return out;
}

std::string formatSniffLine(double seconds, const hidpp::Report& report, const std::string& label) {
    std::string line = formatText("%.3f ", seconds) + hidpp::toHex(report.bytes());
    if (!label.empty()) line += "  <- " + fixtureSafe(label);
    return line;
}

int runSniff(int seconds) {
    auto rx = openReceiverOrExplain();
    if (!rx) return 1;

    const auto start = Clock::now();
    const auto at = [&](Clock::time_point t) { return std::chrono::duration<double>(t - start).count(); };
    const auto now = [&] { return at(Clock::now()); };
    SniffLabeler labeler;

    // Ask a device for its CHANGE_HOST index so its switch announcements can
    // be labelled. Reports that arrive meanwhile are set aside by request()
    // and printed afterwards under a note with the time window (issue #9).
    std::array<Clock::time_point, hidpp::kMaxDeviceIndex + 1> learnAt{};
    const auto learn = [&](std::uint8_t slot) {
        learnAt[slot] = {};
        const double from = now();
        const auto pairing = hidpp::getPairingInfo(*rx, slot);
        if (pairing) {
            labeler.setKind(slot, pairing->kind(), pairing->wpid);
            if (pairing->linkUp) {
                if (const auto feature = hidpp::getFeature(*rx, slot, hidpp::kFeatureChangeHost)) {
                    labeler.addDevice(slot, pairing->kind(), pairing->wpid, feature->index);
                    std::printf("%.3f # %s in slot %u: CHANGE_HOST at index 0x%02x (asked by duckling)\n", now(),
                                kindName(pairing->kind()), slot, feature->index);
                }
            }
        }
        if (rx->hasPending()) {
            std::printf("%.3f # these arrived between %.3f and %.3f, while duckling was asking:\n", now(), from,
                        now());
        }
    };

    std::printf("%.3f # duckling sniff on %s\n", now(), rx->paths().shortPath.c_str());
    for (std::uint8_t slot = 1; slot <= hidpp::kMaxDeviceIndex; ++slot) learn(slot);
    std::printf("%.3f # recording; type a label + Enter to insert a marker, Ctrl+C to stop\n", now());
    std::fflush(stdout);

    // Markers from stdin, timed when Enter was pressed. The thread blocks in
    // getline and is detached: it ends with the process.
    auto markers = std::make_shared<Markers>();
    std::thread([markers] {
        std::string line;
        while (std::getline(std::cin, line)) {
            const std::lock_guard<std::mutex> lock(markers->mutex);
            markers->lines.push_back({Clock::now(), line.empty() ? "MARK" : line});
        }
    }).detach();
    // Print the markers typed up to time t, in order.
    const auto flushMarkers = [&](Clock::time_point t) {
        const std::lock_guard<std::mutex> lock(markers->mutex);
        while (!markers->lines.empty() && markers->lines.front().when <= t) {
            const auto& m = markers->lines.front();
            std::printf("%.3f ===== %s =====\n", at(m.when), fixtureSafe(m.text).c_str());
            markers->lines.pop_front();
        }
    };

    const auto deadline = start + std::chrono::seconds(seconds);
    const auto timeLeft = [&] { return seconds <= 0 || Clock::now() < deadline; };
    while (timeLeft()) {
        const auto r = rx->read(std::chrono::milliseconds{100});
        const auto readAt = Clock::now();
        if (!rx->isOpen()) {
            std::fprintf(stderr, "duckling: the receiver went away\n");
            return 1;
        }
        flushMarkers(readAt);
        if (r) {
            std::printf("%s\n", formatSniffLine(at(readAt), *r, labeler.label(*r)).c_str());
            // A device that just connected: learn its CHANGE_HOST index once
            // its connect burst is over.
            if (const auto c = hidpp::parseDeviceConnection(*r); c && c->online && !labeler.knows(c->deviceIndex)) {
                learnAt[c->deviceIndex] = readAt + kLearnDelay;
            }
        }
        for (std::uint8_t slot = 1; slot <= hidpp::kMaxDeviceIndex; ++slot) {
            const auto due = learnAt[slot];
            if (due != Clock::time_point{} && Clock::now() >= due && timeLeft()) learn(slot);
        }
        std::fflush(stdout);
    }
    flushMarkers(Clock::now());
    return 0;
}

}  // namespace duckling::app
