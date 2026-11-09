/**
 * @file test_features.cpp
 * @brief Tests for hidpp feature calls, replayed from the hardware logs (task 1.4).
 *
 * Replies are copied from hw-logs (c3, c4, c5, w1). Only byte 3's software id
 * nibble is changed to duckling's (0x1); the logged requests used Solaar's,
 * Options+'s or the C4 script's.
 */
#include <doctest/doctest.h>

#include <deque>
#include <string>
#include <utility>

#include "hidpp/features.hpp"
#include "hidpp/hex.hpp"
#include "scripted_requester.hpp"

using namespace duckling::hidpp;

using duckling::test::ScriptedRequester;
using duckling::test::kZeros16;

TEST_CASE("ping: version, receiver errors and echo check") {
    ScriptedRequester rq;
    // C4: `11 01 00 1a 04 05 aa` (here with duckling's echo byte a5).
    rq.expect("11 01 00 11 00 00 a5" + kZeros16.substr(0, 13 * 3),
              "11 01 00 11 04 05 a5 00 00 00 00 00 00 00 00 00 00 00 00 00");
    const auto v = ping(rq, 1);
    REQUIRE(v.ok());
    CHECK(*v == ProtocolVersion{4, 5});

    // Task 1.3 probe on carbon: paired, but on Windows.
    rq.expect("11 01 00 11 00 00 a5 00 00 00 00 00 00 00 00 00 00 00 00 00", "10 01 8f 00 11 04 00");
    const auto away = ping(rq, 1);
    REQUIRE_FALSE(away.ok());
    CHECK(away.failure().deviceUnreachable());
    CHECK(describe(away.failure()) == "device not connected (HID++ 1.0 error 0x04)");

    // Task 1.3 probe: empty slot.
    rq.expect("11 03 00 11 00 00 a5 00 00 00 00 00 00 00 00 00 00 00 00 00", "10 03 8f 00 11 09 00");
    CHECK(ping(rq, 3).failure().deviceUnreachable());
    CHECK(describe(Failure{FailureKind::Hidpp10, 0x09}) == "device not responding (HID++ 1.0 error 0x09)");
    CHECK(Failure{FailureKind::Hidpp10, 0x08}.unknownDevice());
    CHECK_FALSE(Failure{FailureKind::Hidpp10, 0x08}.deviceUnreachable());

    rq.expect("11 01 00 11 00 00 a5 00 00 00 00 00 00 00 00 00 00 00 00 00",
              "11 01 00 11 04 05 aa 00 00 00 00 00 00 00 00 00 00 00 00 00");
    CHECK(ping(rq, 1).failure().kind == FailureKind::BadReply);

    rq.expect("11 01 00 11 00 00 a5 00 00 00 00 00 00 00 00 00 00 00 00 00", "");
    CHECK(ping(rq, 1).failure().kind == FailureKind::Timeout);
    CHECK(describe(Failure{}) == "timeout");
    CHECK(rq.done());
}

TEST_CASE("discover: keyboard and mouse feature indexes (C3, C4, W1)") {
    ScriptedRequester kbd;
    kbd.expect("11 01 00 01 18 14" + kZeros16.substr(0, 14 * 3),  // c4: 0x1814 -> 9 v2
               "11 01 00 01 09 00 02" + kZeros16.substr(0, 13 * 3))
        .expect("11 01 00 01 18 15" + kZeros16.substr(0, 14 * 3),  // w1: HOSTS_INFO at 0x0a
                "11 01 00 01 0a 00 01" + kZeros16.substr(0, 13 * 3))
        .expect("11 01 00 01 00 05" + kZeros16.substr(0, 14 * 3),  // c4: 0x0005 -> 3
                "11 01 00 01 03 00 00" + kZeros16.substr(0, 13 * 3));
    const auto k = discover(kbd, 1);
    REQUIRE(k.ok());
    CHECK(*k == DeviceFeatures{0x09, 2, 0x0A, 0x03});
    CHECK(kbd.done());

    ScriptedRequester mouse;
    mouse.expect("11 02 00 01 18 14" + kZeros16.substr(0, 14 * 3),  // c4: 0x1814 -> 10 v1
                 "11 02 00 01 0a 00 01" + kZeros16.substr(0, 13 * 3))
        .expect("11 02 00 01 18 15" + kZeros16.substr(0, 14 * 3),  // c3: `11 02 00 08 0b 00 02`
                "11 02 00 01 0b 00 02" + kZeros16.substr(0, 13 * 3))
        .expect("11 02 00 01 00 05" + kZeros16.substr(0, 14 * 3),
                "11 02 00 01 00 00 00" + kZeros16.substr(0, 13 * 3));  // pretend: no DEVICE_NAME
    const auto m = discover(mouse, 2);
    REQUIRE(m.ok());
    CHECK(*m == DeviceFeatures{0x0A, 1, 0x0B, 0});
    CHECK(mouse.done());
}

TEST_CASE("discover: CHANGE_HOST is required, other failures propagate") {
    ScriptedRequester none;
    none.expect("11 01 00 01 18 14" + kZeros16.substr(0, 14 * 3),
                "11 01 00 01 00 00 00" + kZeros16.substr(0, 13 * 3));
    CHECK(discover(none, 1).failure().kind == FailureKind::Unsupported);

    ScriptedRequester gone;
    gone.expect("11 01 00 01 18 14" + kZeros16.substr(0, 14 * 3),
                "11 01 00 01 09 00 02" + kZeros16.substr(0, 13 * 3))
        .expect("11 01 00 01 18 15" + kZeros16.substr(0, 14 * 3), "10 01 8f 00 01 04 00");
    CHECK(discover(gone, 1).failure().deviceUnreachable());
    CHECK(gone.done());
}

TEST_CASE("getHostInfo and setCurrentHost (C3, C5)") {
    ScriptedRequester rq;
    // C3: mouse CHANGE_HOST at 0x0a, `11 02 0a 07 03 02` = 3 hosts, current ch3.
    rq.expect("11 02 0a 01" + kZeros16, "11 02 0a 01 03 02" + kZeros16.substr(0, 14 * 3));
    const auto info = getHostInfo(rq, 2, 0x0A);
    REQUIRE(info.ok());
    CHECK(*info == HostInfo{3, 2});

    rq.expect("11 02 0a 01" + kZeros16, "11 02 0a 01 03 03" + kZeros16.substr(0, 14 * 3));
    CHECK(getHostInfo(rq, 2, 0x0A).failure().kind == FailureKind::BadReply);

    // C5: `11 01 09 1a 02` acknowledged with `11 01 09 1a 00`.
    rq.expect("11 01 09 11 02" + kZeros16.substr(0, 15 * 3), "11 01 09 11 00" + kZeros16.substr(0, 15 * 3));
    CHECK(setCurrentHost(rq, 1, 0x09, 2).ok());

    // HID++ 2.0 error from the device: invalid argument.
    rq.expect("11 01 09 11 07" + kZeros16.substr(0, 15 * 3), "11 01 ff 09 11 02" + kZeros16.substr(0, 14 * 3));
    const auto bad = setCurrentHost(rq, 1, 0x09, 7);
    CHECK(bad.failure() == Failure{FailureKind::Hidpp20, 0x02});
    CHECK(describe(bad.failure()) == "invalid argument (HID++ 2.0 error 0x02)");
    CHECK(rq.done());
}

TEST_CASE("getHostNames: mouse table with a two-piece name (C3)") {
    ScriptedRequester rq;
    // caps / ? / nbHost / current, as the keyboard reported in W1: 13 08 03 00.
    rq.expect("11 02 0b 01" + kZeros16, "11 02 0b 01 13 08 03 02" + kZeros16.substr(0, 12 * 3))
        // host 0: paired, 14 bytes (W1 descriptor after the rename: 00 01 05 01 0e 18)
        .expect("11 02 0b 11" + kZeros16, "11 02 0b 11 00 01 05 01 0e 18" + kZeros16.substr(0, 10 * 3))
        .expect("11 02 0b 31" + kZeros16,
                "11 02 0b 31 00 00 44 45 53 4b 54 4f 50 2d 43 41 52 4c 4f 53")  // "DESKTOP-CARLOS"
        // host 1: "fedora"
        .expect("11 02 0b 11 01" + kZeros16.substr(0, 15 * 3),
                "11 02 0b 11 01 01 05 01 06 18" + kZeros16.substr(0, 10 * 3))
        .expect("11 02 0b 31 01" + kZeros16.substr(0, 15 * 3),
                "11 02 0b 31 01 00 66 65 64 6f 72 61 00 00 00 00 00 00 00 00")
        // host 2: 22 bytes in two pieces, "Logitech Bolt " + "receiver" (C3, before Solaar renamed it)
        .expect("11 02 0b 11 02" + kZeros16.substr(0, 15 * 3),
                "11 02 0b 11 02 01 05 01 16 18" + kZeros16.substr(0, 10 * 3))
        .expect("11 02 0b 31 02" + kZeros16.substr(0, 15 * 3),
                "11 02 0b 31 02 00 4c 6f 67 69 74 65 63 68 20 42 6f 6c 74 20")
        .expect("11 02 0b 31 02 0e" + kZeros16.substr(0, 14 * 3),
                "11 02 0b 31 02 0e 72 65 63 65 69 76 65 72 00 00 00 00 00 00");
    const auto names = getHostNames(rq, 2, 0x0B);
    REQUIRE(names.ok());
    REQUIRE(names->size() == 3);
    CHECK((*names)[0] == HostName{0, true, "DESKTOP-CARLOS", true});
    CHECK((*names)[1] == HostName{1, true, "fedora", true});
    CHECK((*names)[2] == HostName{2, true, "Logitech Bolt receiver", true});
    CHECK(rq.done());
}

TEST_CASE("getHostNames: rejects a name piece for another host or offset") {
    ScriptedRequester rq;
    rq.expect("11 02 0b 01" + kZeros16, "11 02 0b 01 13 08 01 00" + kZeros16.substr(0, 12 * 3))
        .expect("11 02 0b 11" + kZeros16, "11 02 0b 11 00 01 05 01 06 18" + kZeros16.substr(0, 10 * 3))
        .expect("11 02 0b 31" + kZeros16,  // answer for host 1: another program's reply
                "11 02 0b 31 01 00 66 65 64 6f 72 61 00 00 00 00 00 00 00 00");
    CHECK(getHostNames(rq, 2, 0x0B).failure().kind == FailureKind::BadReply);

    ScriptedRequester tooMany;
    tooMany.expect("11 02 0b 01" + kZeros16, "11 02 0b 01 13 08 ff 00" + kZeros16.substr(0, 12 * 3));
    CHECK(getHostNames(tooMany, 2, 0x0B).failure().kind == FailureKind::BadReply);

    ScriptedRequester noNames;
    noNames.expect("11 02 0b 01" + kZeros16, "11 02 0b 01 00 08 03 00" + kZeros16.substr(0, 12 * 3));
    CHECK(getHostNames(noNames, 2, 0x0B).failure().kind == FailureKind::Unsupported);
}

TEST_CASE("getHostNames: unpaired channels and refused names don't lose the table") {
    ScriptedRequester rq;
    rq.expect("11 02 0b 01" + kZeros16, "11 02 0b 01 13 08 03 00" + kZeros16.substr(0, 12 * 3))
        .expect("11 02 0b 11" + kZeros16, "11 02 0b 11 00 01 05 01 06 18" + kZeros16.substr(0, 10 * 3))
        .expect("11 02 0b 31" + kZeros16, "11 02 0b 31 00 00 66 65 64 6f 72 61 00 00 00 00 00 00 00 00")
        // ch2 unpaired: no name read.
        .expect("11 02 0b 11 01" + kZeros16.substr(0, 15 * 3),
                "11 02 0b 11 01 00 00 00 00 18" + kZeros16.substr(0, 10 * 3))
        // ch3 paired, but the device refuses the name (HID++ 2.0 error 0x02).
        .expect("11 02 0b 11 02" + kZeros16.substr(0, 15 * 3),
                "11 02 0b 11 02 01 05 01 06 18" + kZeros16.substr(0, 10 * 3))
        .expect("11 02 0b 31 02" + kZeros16.substr(0, 15 * 3), "11 02 ff 0b 31 02" + kZeros16.substr(0, 14 * 3));
    const auto names = getHostNames(rq, 2, 0x0B);
    REQUIRE(names.ok());
    REQUIRE(names->size() == 3);
    CHECK((*names)[0] == HostName{0, true, "fedora", true});
    CHECK((*names)[1] == HostName{1, false, "", false});
    CHECK((*names)[2] == HostName{2, true, "", false});
    CHECK(rq.done());

    // A timeout while reading a name means the device is gone: fail the call.
    ScriptedRequester gone;
    gone.expect("11 02 0b 01" + kZeros16, "11 02 0b 01 13 08 01 00" + kZeros16.substr(0, 12 * 3))
        .expect("11 02 0b 11" + kZeros16, "11 02 0b 11 00 01 05 01 06 18" + kZeros16.substr(0, 10 * 3))
        .expect("11 02 0b 31" + kZeros16, "");
    CHECK(getHostNames(gone, 2, 0x0B).failure().kind == FailureKind::Timeout);
}

TEST_CASE("getDeviceName (C4)") {
    ScriptedRequester rq;
    rq.expect("11 01 03 01" + kZeros16, "11 01 03 01 0d" + kZeros16.substr(0, 15 * 3))
        .expect("11 01 03 11" + kZeros16,
                "11 01 03 11 4d 58 20 4d 65 63 68 61 6e 69 63 61 6c 00 00 00");
    const auto name = getDeviceName(rq, 1, 0x03);
    REQUIRE(name.ok());
    CHECK(*name == "MX Mechanical");

    // A name longer than one 16-byte piece is read at increasing offsets.
    rq.expect("11 02 03 01" + kZeros16, "11 02 03 01 14" + kZeros16.substr(0, 15 * 3))
        .expect("11 02 03 11" + kZeros16,
                "11 02 03 11 4c 6f 67 69 74 65 63 68 20 4d 58 20 4d 61 73 74")
        .expect("11 02 03 11 10" + kZeros16.substr(0, 15 * 3),
                "11 02 03 11 65 72 20 33 00 00 00 00 00 00 00 00 00 00 00 00");
    CHECK(*getDeviceName(rq, 2, 0x03) == "Logitech MX Master 3");
    CHECK(rq.done());
}
