/**
 * @file test_sniff.cpp
 * @brief Tests for `duckling sniff` labels and line format (task 1.7),
 *        run over the hardware recordings.
 */
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "app/sniff.hpp"
#include "fixtures.hpp"
#include "hidpp/hex.hpp"

using namespace duckling;

namespace {

/**
 * @brief Decode a hex frame.
 * @param hex Frame bytes.
 * @return The report.
 */
hidpp::Report frame(std::string_view hex) {
    const auto bytes = hidpp::parseHex(hex);
    REQUIRE(bytes.has_value());
    const auto r = hidpp::Report::decode(*bytes);
    REQUIRE(r.has_value());
    return *r;
}

/**
 * @brief A labeler that knows both devices (C1/C4 feature indexes).
 * @return The labeler.
 */
app::SniffLabeler configured() {
    app::SniffLabeler l;
    l.addDevice(1, hidpp::DeviceKind::Keyboard, 0xB366, 0x09);
    l.addDevice(2, hidpp::DeviceKind::Mouse, 0xB034, 0x0A);
    return l;
}

/**
 * @brief Count labels containing @p needle over one recording.
 * @param prefix Log name prefix, e.g. "c2-".
 * @param needle Text to look for.
 * @return Number of frames whose label contains it.
 */
int countLabels(std::string_view prefix, std::string_view needle) {
    auto labeler = configured();
    int n = 0;
    for (const auto& log : test::embeddedLogs()) {
        if (!log.name.starts_with(prefix)) continue;
        for (const auto& f : test::scanLog(log.name, log.text).frames) {
            const auto r = hidpp::Report::decode(f.bytes);
            REQUIRE(r.has_value());
            n += labeler.label(*r).find(needle) != std::string::npos;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("labels for the frames seen in the hardware checks") {
    auto l = configured();
    // C2: mouse Easy-Switch, receiver notice.
    CHECK(l.label(frame("10 02 41 10 42 34 b0")) == "mouse OFFLINE (slot 2, wpid B034)");
    CHECK(l.label(frame("10 01 41 10 01 66 b3")) == "keyboard online (slot 1, wpid B366)");
    // W1: keyboard Easy-Switch 3 on Windows.
    CHECK(l.label(frame("11 01 09 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          "keyboard switching ch1 -> ch3 (Easy-Switch)");
    // C2: receiver errors after a device left.
    CHECK(l.label(frame("10 ff 8f 80 03 02 00")) ==
          "receiver error for request 0x80/0x03: receiver error (HID++ 1.0 error 0x02)");
    // Task 1.3 probe: empty slot.
    CHECK(l.label(frame("10 03 8f 00 11 09 00")) ==
          "receiver error for request 0x00/0x11: device not responding (HID++ 1.0 error 0x09)");
    // W1 / r1: Options+ reading the receiver's codename for slot 1.
    CHECK(l.label(frame("11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00")) ==
          "codename of slot 1: 'MX MCHNCL'");
    CHECK(l.label(frame("11 ff 83 b5 52 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")) == "pairing info of slot 2: mouse, link down");
    // C5: Options+'s getHostInfo reply, and a reply to duckling.
    CHECK(l.label(frame("11 01 09 0a 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          "keyboard reply, swId 0xa (Options+ or Solaar)");
    CHECK(l.label(frame("11 02 0a 11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          "mouse reply, swId 0x1 (duckling)");
    // HID++ 2.0 error from a device.
    CHECK(l.label(frame("11 01 ff 09 11 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          "keyboard error for feature 0x09 function 0x1: invalid argument (HID++ 2.0 error 0x02)");
    // Device notification (swId 0) that isn't a switch.
    CHECK(l.label(frame("11 02 04 00 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00")) == "mouse notification");
    // Unpaired notice: the slot's CHANGE_HOST index is forgotten.
    CHECK(l.knows(2));
    CHECK(l.label(frame("10 02 40 02 00 00 00")) == "slot 2 unpaired");
    CHECK_FALSE(l.knows(2));
    CHECK(l.label(frame("10 ff 40 02 00 00 00")) == "receiver notification 0x40");
    CHECK(l.label(frame("10 ff 4a 01 00 00 00")) == "receiver notification 0x4a");
}

TEST_CASE("switch announcements are only labelled for known devices") {
    app::SniffLabeler unknown;
    CHECK(unknown.label(frame("11 01 09 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          "device notification");
    CHECK(countLabels("c2-", "switching") == 2);
    CHECK(countLabels("c3-", "switching") == 3);
    CHECK(countLabels("w1-", "switching") == 1);
    CHECK(countLabels("c4-", "switching") == 0);
    // s1: duckling sniff's own capture of a mouse round trip done by `duckling switch`.
    CHECK(countLabels("s1-", "switching") == 0);
    CHECK(countLabels("s1-", "mouse online") == 1);
    CHECK(countLabels("s1-", "mouse OFFLINE") == 1);
    CHECK(countLabels("s1-", "(duckling)") == 14);
    // s2/s3: the user pressed keyboard Easy-Switch 3 then 1 (and the mouse's)
    // while duckling sniff recorded on carbon (s2) and Windows (s3).
    CHECK(countLabels("s2-", "keyboard switching ch3 -> ch1") == 1);
    CHECK(countLabels("s3-", "keyboard switching ch1 -> ch3") == 1);
    CHECK(countLabels("s2-", "mouse switching") == 0);
    CHECK(countLabels("s3-", "mouse switching") == 0);
}

TEST_CASE("fixtureSafe keeps labels and markers from looking like frames") {
    CHECK(app::fixtureSafe("press 10 times") == "press #10 times");
    CHECK(app::fixtureSafe("11 ab 1 abc 0x10") == "#11 #ab 1 abc 0x10");
    CHECK(app::fixtureSafe("bad\x1b[2Jname") == "bad?[2Jname");
    // A marker or a label with such tokens reads back as no frame at all.
    const auto scan = test::scanLog("m", "12.000 ===== " + app::fixtureSafe("press 10 times then 11") + " =====");
    CHECK(scan.frames.empty());
    CHECK(scan.malformed.empty());
    // A codename containing " 11 " (device-supplied text).
    app::SniffLabeler l;
    const std::string line = app::formatSniffLine(
        1.0, frame("11 ff 83 b5 61 01 05 41 20 31 31 20 00 00 00 00 00 00 00 00"),
        l.label(frame("11 ff 83 b5 61 01 05 41 20 31 31 20 00 00 00 00 00 00 00 00")));
    CHECK(line.ends_with("<- codename of slot 1: 'A #11 '"));
    CHECK(test::scanLog("c", line).frames.size() == 1);
}

TEST_CASE("kinds of devices that aren't connected come from pairing info") {
    app::SniffLabeler l;
    l.setKind(1, hidpp::DeviceKind::Keyboard, 0xB366);
    CHECK_FALSE(l.knows(1));
    CHECK(l.label(frame("10 ff 8f 83 b5 08 00")).starts_with("receiver error"));
    CHECK(l.label(frame("11 01 09 0a 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")) ==
          "keyboard reply, swId 0xa (Options+ or Solaar)");
    // ...and from pairing-info replies seen on the wire (r1).
    app::SniffLabeler wire;
    (void)wire.label(frame("11 ff 83 b5 52 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00"));
    CHECK(wire.label(frame("11 02 04 00 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00")) == "mouse notification");
}

TEST_CASE("sniff lines can be read back as fixtures: labels add no frames") {
    for (const auto& log : test::embeddedLogs()) {
        auto labeler = configured();
        for (const auto& f : test::scanLog(log.name, log.text).frames) {
            const auto r = hidpp::Report::decode(f.bytes);
            REQUIRE(r.has_value());
            const std::string line = app::formatSniffLine(123.456, *r, labeler.label(*r));
            const auto again = test::scanLog("line", line);
            CAPTURE(line);
            REQUIRE(again.frames.size() == 1);
            CHECK(again.frames[0].bytes == f.bytes);
            CHECK(again.malformed.empty());
        }
    }
    CHECK(app::formatSniffLine(1.5, frame("10 02 41 10 42 34 b0"), "") == "1.500 10 02 41 10 42 34 b0");
}
