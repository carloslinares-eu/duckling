/**
 * @file test_events.cpp
 * @brief Tests for duckling::hidpp::EventParser, replaying the hardware-check
 *        recordings (PLAN.md C2, C3, C5).
 */
#include <doctest/doctest.h>

#include <string_view>
#include <vector>

#include "fixtures.hpp"
#include "hidpp/events.hpp"
#include "hidpp/hex.hpp"

using namespace duckling::hidpp;

namespace {

/** @brief Keyboard slot and CHANGE_HOST index on carbon and Windows (C1, C4). */
constexpr std::uint8_t kKbd = 1, kKbdChangeHost = 9;
/** @brief Mouse slot and CHANGE_HOST index on carbon and Windows (C1, C4). */
constexpr std::uint8_t kMouse = 2, kMouseChangeHost = 10;
/** @brief Wireless product ids (C2 connection notices). */
constexpr std::uint16_t kKbdWpid = 0xB366, kMouseWpid = 0xB034;

/**
 * @brief A parser set up like duckling will be for the user's two devices.
 * @return Parser with CHANGE_HOST registered for keyboard and mouse.
 */
EventParser configuredParser() {
    EventParser p;
    p.setChangeHostFeature(kKbd, kKbdWpid, kKbdChangeHost);
    p.setChangeHostFeature(kMouse, kMouseWpid, kMouseChangeHost);
    return p;
}

/**
 * @brief Run a parser over one embedded recording.
 * @param parser Parser to use (copied: parsing updates its state).
 * @param prefix Log file name prefix, e.g. "c2-".
 * @return Events in recording order.
 */
std::vector<Event> eventsOf(EventParser parser, std::string_view prefix) {
    std::vector<Event> events;
    bool found = false;
    for (const auto& log : duckling::test::embeddedLogs()) {
        if (!log.name.starts_with(prefix)) continue;
        found = true;
        for (const auto& f : duckling::test::scanLog(log.name, log.text).frames) {
            const auto r = Report::decode(f.bytes);
            REQUIRE(r.has_value());
            if (auto e = parser.parse(*r)) events.push_back(*e);
        }
    }
    REQUIRE(found);
    return events;
}

/**
 * @brief Shorthand for a connection event of one of the user's devices.
 * @param index  Device slot.
 * @param online Link state.
 * @return The expected DeviceConnection.
 */
Event conn(std::uint8_t index, bool online) {
    DeviceConnection c;
    c.deviceIndex = index;
    c.online = online;
    c.rawKind = index == kKbd ? 0x01 : 0x02;
    c.wpid = index == kKbd ? kKbdWpid : kMouseWpid;
    return c;
}

/**
 * @brief Decode a frame given as hex text. The test fails if it doesn't decode.
 * @param hex Frame as hex text.
 * @return The decoded report.
 */
Report decodeHex(std::string_view hex) {
    const auto bytes = parseHex(hex);
    REQUIRE(bytes.has_value());
    const auto r = Report::decode(*bytes);
    REQUIRE(r.has_value());
    return *r;
}

}  // namespace

TEST_CASE("C2 labeled run: exact event sequence for switch vs power, both devices") {
    const std::vector<Event> expected = {
        // Mouse_switch_1: no announcement (CHANGE_HOST V1), just down/up.
        conn(kMouse, false), conn(kMouse, true),
        // Mouse_power: indistinguishable from the switch.
        conn(kMouse, false), conn(kMouse, true),
        // kdb_switch_1: announced 37 ms before the drop.
        HostChange{kKbd, 2, 0}, conn(kKbd, false), conn(kKbd, true),
        // kdb_switch_2: same, different target.
        HostChange{kKbd, 2, 1}, conn(kKbd, false), conn(kKbd, true),
        // kdb_power: no announcement.
        conn(kKbd, false), conn(kKbd, true),
    };
    CHECK(eventsOf(configuredParser(), "c2-") == expected);
}

TEST_CASE("C3 recording: dead-target switch, then real hand-offs to Windows") {
    // The recorder kept running after C3 (log lines 184-313) and captured the
    // user moving both devices to Windows by hand during C4/C5: mouse first,
    // then keyboard Easy-Switch 1. That's exactly the "mouse leaves, keyboard
    // should follow" case duckling automates.
    const std::vector<Event> expected = {
        // C3 proper: kbd Easy-Switch 2 while fedora (ch2) is off, so it's stranded.
        HostChange{kKbd, 2, 1}, conn(kKbd, false),
        // User presses Easy-Switch 3: kbd back on carbon.
        conn(kKbd, true),
        // Hand-off 1: mouse leaves, ~2 s later the keyboard is sent to ch1.
        conn(kMouse, false), HostChange{kKbd, 2, 0}, conn(kKbd, false),
        // Both come back to carbon.
        conn(kKbd, true), conn(kMouse, true),
        // Hand-off 2: mouse leaves, ~14 s later the keyboard follows to ch1.
        conn(kMouse, false), HostChange{kKbd, 2, 0}, conn(kKbd, false),
    };
    CHECK(eventsOf(configuredParser(), "c3-") == expected);
}

TEST_CASE("C5 (Windows): replies and button events are not HostChange") {
    // The log has getHostInfo/setCurrentHost replies (swId 0xA), a receiver
    // register read, and mouse REPROG CONTROLS events on feature 9 (the
    // keyboard's CHANGE_HOST index): none of them are events for duckling.
    CHECK(eventsOf(configuredParser(), "c5-").empty());
}

TEST_CASE("HostChange needs CHANGE_HOST to be registered for the device") {
    const auto events = eventsOf(EventParser{}, "c2-");
    for (const auto& e : events) CHECK(std::holds_alternative<DeviceConnection>(e));
    CHECK(events.size() == 10);  // the 12 of the full sequence minus 2 HostChange
}

TEST_CASE("recorded CHANGE_HOST replies are never reported as host switches") {
    // The recordings hold real getHostInfo replies on the keyboard's
    // CHANGE_HOST index (Solaar swId 2, hidpp-win.py swId 0xA). Make sure
    // they exist, so this test actually exercises something, and that none
    // of them become events.
    EventParser parser = configuredParser();
    int replies = 0;
    for (const auto& f : duckling::test::scanAllLogs().frames) {
        const auto r = Report::decode(f.bytes);
        REQUIRE(r.has_value());
        const auto e = parser.parse(*r);
        if (r->deviceIndex() == kKbd && r->featureIndex() == kKbdChangeHost && r->swId() != 0) {
            ++replies;
            CAPTURE(f.file);
            CAPTURE(f.line);
            CHECK_FALSE(e.has_value());
        }
    }
    CHECK(replies >= 5);
}

TEST_CASE("HostChange count per recording") {
    const auto count = [](std::string_view prefix) {
        int n = 0;
        for (const auto& e : eventsOf(configuredParser(), prefix)) n += std::holds_alternative<HostChange>(e);
        return n;
    };
    CHECK(count("c2-") == 2);
    CHECK(count("c3-") == 3);
    CHECK(count("c4-") == 0);
    CHECK(count("c5-") == 0);
}

TEST_CASE("parseDeviceConnection decodes every field") {
    const auto c = parseDeviceConnection(decodeHex("10 01 41 10 41 66 b3"));
    REQUIRE(c.has_value());
    CHECK(c->deviceIndex == 1);
    CHECK_FALSE(c->online);
    CHECK(c->kind() == DeviceKind::Keyboard);
    CHECK(c->wpid == 0xB366);

    const auto m = parseDeviceConnection(decodeHex("10 02 41 10 02 34 b0"));
    REQUIRE(m.has_value());
    CHECK(m->online);
    CHECK(m->kind() == DeviceKind::Mouse);
    CHECK(m->wpid == 0xB034);

    const auto other = parseDeviceConnection(decodeHex("10 03 41 10 49 00 40"));
    REQUIRE(other.has_value());
    CHECK(other->kind() == DeviceKind::Unknown);
    CHECK(other->rawKind == 0x09);
    CHECK_FALSE(other->online);
}

TEST_CASE("connection notices outside device slots are ignored") {
    CHECK_FALSE(parseDeviceConnection(decodeHex("10 ff 41 10 41 66 b3")).has_value());
    CHECK_FALSE(parseDeviceConnection(decodeHex("10 00 41 10 41 66 b3")).has_value());
    CHECK_FALSE(parseDeviceConnection(decodeHex("10 07 41 10 41 66 b3")).has_value());
    CHECK_FALSE(parseDeviceConnection(decodeHex("10 ff 8f 80 03 02 00")).has_value());
}

TEST_CASE("a long frame on feature index 0x41 is not a connection notice") {
    // HID++ 2.0 device report that happens to use feature index 0x41.
    const Report r = decodeHex("11 01 41 00 40 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
    CHECK_FALSE(parseDeviceConnection(r).has_value());
    EventParser p = configuredParser();
    CHECK_FALSE(p.parse(r).has_value());
}

TEST_CASE("CHANGE_HOST frames that are not events") {
    EventParser p = configuredParser();
    // getHostInfo reply to Solaar (swId 2) and to hidpp-win.py (swId 0xA).
    CHECK_FALSE(p.parse(decodeHex("11 01 09 02 03 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    CHECK_FALSE(p.parse(decodeHex("11 01 09 0a 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    // setCurrentHost ack (function 1).
    CHECK_FALSE(p.parse(decodeHex("11 01 09 1a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    // Function 1 with swId 0 is not the switch event either.
    CHECK_FALSE(p.parse(decodeHex("11 01 09 10 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    // HID++ 2.0 error from the device.
    CHECK_FALSE(p.parse(decodeHex("11 01 ff 09 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    // Short frame on the CHANGE_HOST index.
    CHECK_FALSE(p.parse(decodeHex("10 01 09 00 02 00 00")));
}

TEST_CASE("setChangeHostFeature / forgetDevice") {
    const Report ev = decodeHex("11 01 09 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
    EventParser p;
    CHECK_FALSE(p.parse(ev));
    p.setChangeHostFeature(1, kKbdWpid, 9);
    CHECK(p.parse(ev) == std::optional<Event>{HostChange{1, 2, 0}});
    p.forgetDevice(1);
    CHECK_FALSE(p.parse(ev));
    // Out-of-range slots are ignored rather than corrupting state.
    p.setChangeHostFeature(0, kKbdWpid, 9);
    p.setChangeHostFeature(7, kKbdWpid, 9);
    p.setChangeHostFeature(0xFF, kKbdWpid, 9);
    p.forgetDevice(200);
    CHECK_FALSE(p.parse(ev));
}

TEST_CASE("a different device in the slot drops the registration (re-pairing)") {
    const Report ev = decodeHex("11 01 09 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
    EventParser p = configuredParser();
    // Same keyboard reconnects: registration kept.
    CHECK(p.parse(decodeHex("10 01 41 10 01 66 b3")) == conn(kKbd, true));
    CHECK(p.parse(ev) == std::optional<Event>{HostChange{1, 2, 0}});
    // Another device (wpid 4000) shows up in slot 1: its feature 9 is unknown.
    const auto other = p.parse(decodeHex("10 01 41 10 02 00 40"));
    REQUIRE(other.has_value());
    CHECK(std::get<DeviceConnection>(*other).wpid == 0x4000);
    CHECK_FALSE(p.parse(ev));
    // The mouse slot is untouched.
    CHECK(p.parse(decodeHex("11 02 0a 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          std::optional<Event>{HostChange{2, 2, 0}});
}

TEST_CASE("an unpair notice drops the registration") {
    const Report ev = decodeHex("11 01 09 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
    EventParser p = configuredParser();
    CHECK(p.parse(ev).has_value());
    CHECK_FALSE(p.parse(decodeHex("10 01 40 02 00 00 00")).has_value());
    CHECK_FALSE(p.parse(ev).has_value());
}

TEST_CASE("HostChange host numbers must be valid Easy-Switch hosts") {
    EventParser p = configuredParser();
    CHECK(p.parse(decodeHex("11 01 09 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          std::optional<Event>{HostChange{1, 0, 2}});
    CHECK_FALSE(p.parse(decodeHex("11 01 09 00 02 ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    CHECK_FALSE(p.parse(decodeHex("11 01 09 00 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    CHECK_FALSE(p.parse(decodeHex("11 01 09 00 02 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
}
