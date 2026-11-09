/**
 * @file test_status.cpp
 * @brief Tests for `duckling status` collection and formatting (task 1.5).
 */
#include <doctest/doctest.h>

#include <string>

#include "app/status.hpp"
#include "scripted_requester.hpp"

using namespace duckling;
using duckling::test::kZeros16;
using duckling::test::ScriptedRequester;

namespace {

/**
 * @brief Expect the r1 answers for empty slots 3..6.
 * @param rq Script to extend.
 */
void expectEmptySlots(ScriptedRequester& rq) {
    for (char slot = '3'; slot <= '6'; ++slot) {
        // Read twice: the error can't be tied to one slot (issue #7).
        rq.expect(std::string("10 ff 83 b5 5") + slot + " 00 00", "10 ff 8f 83 b5 08 00");
        rq.expect(std::string("10 ff 83 b5 5") + slot + " 00 00", "10 ff 8f 83 b5 08 00");
    }
}

/**
 * @brief Pad a long frame's hex to 20 bytes with zeros.
 * @param hex Leading bytes of the frame.
 * @return The full 20-byte frame.
 */
std::string longFrame(const std::string& hex) {
    const std::size_t bytes = (hex.size() + 1) / 3;
    return hex + kZeros16.substr(0, (20 - bytes) * 3);
}

}  // namespace

TEST_CASE("status on carbon while both devices are on Windows (r1)") {
    ScriptedRequester rq;
    rq.expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 41 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00")
        .expect("10 ff 83 b5 61 01 00", "11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00")
        .expect("10 ff 83 b5 52 00 00", "11 ff 83 b5 52 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")
        .expect("10 ff 83 b5 62 01 00", "11 ff 83 b5 62 01 0c 4d 58 20 4d 61 73 74 65 72 20 33 53 00");
    expectEmptySlots(rq);

    const auto report = app::collectStatus(rq, "/dev/hidraw2", "carbon");
    CHECK(rq.done());  // link down: no pings, no timeouts
    REQUIRE(report.devices.size() == 2);
    CHECK_FALSE(report.devices[0].here());
    CHECK(report.problems.empty());

    CHECK(app::formatStatus(report) ==
          "receiver   Logi Bolt  /dev/hidraw2\n"
          "this host  carbon\n"
          "\n"
          "slot 1  keyboard  MX MCHNCL  (wpid B366, serial 284C459C)\n"
          "        not connected here (on another host, or off)\n"
          "\n"
          "slot 2  mouse     MX Master 3S  (wpid B034, serial E576CD4A)\n"
          "        not connected here (on another host, or off)\n");
}

TEST_CASE("status on Windows with the keyboard connected (task 1.4 probe)") {
    ScriptedRequester rq;
    rq.expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 01 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00")
        .expect("10 ff 83 b5 61 01 00", "11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00")
        .expect(longFrame("11 01 00 11 00 00 a5"), longFrame("11 01 00 11 04 05 a5"))
        // discover: CHANGE_HOST 9 v2, HOSTS_INFO 0x0a, DEVICE_NAME 3
        .expect(longFrame("11 01 00 01 18 14"), longFrame("11 01 00 01 09 00 02"))
        .expect(longFrame("11 01 00 01 18 15"), longFrame("11 01 00 01 0a 00 01"))
        .expect(longFrame("11 01 00 01 00 05"), longFrame("11 01 00 01 03 00 00"))
        // name
        .expect(longFrame("11 01 03 01"), longFrame("11 01 03 01 0d"))
        .expect(longFrame("11 01 03 11"), "11 01 03 11 4d 58 20 4d 65 63 68 61 6e 69 63 61 6c 00 00 00")
        // current host: ch1 of 3
        .expect(longFrame("11 01 09 01"), longFrame("11 01 09 01 03 00"))
        // host names: ch1 DESKTOP-CARLOS, ch2 unpaired, ch3 refuses its name
        .expect(longFrame("11 01 0a 01"), longFrame("11 01 0a 01 13 08 03 00"))
        .expect(longFrame("11 01 0a 11"), longFrame("11 01 0a 11 00 01 05 01 0e 18"))
        .expect(longFrame("11 01 0a 31"), "11 01 0a 31 00 00 44 45 53 4b 54 4f 50 2d 43 41 52 4c 4f 53")
        .expect(longFrame("11 01 0a 11 01"), longFrame("11 01 0a 11 01 00 00 00 00 18"))
        .expect(longFrame("11 01 0a 11 02"), longFrame("11 01 0a 11 02 01 05 01 06 18"))
        .expect(longFrame("11 01 0a 31 02"), longFrame("11 01 ff 0a 31 02"))
        // slot 2: mouse, link up but the ping times out
        .expect("10 ff 83 b5 52 00 00", "11 ff 83 b5 52 02 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")
        .expect("10 ff 83 b5 62 01 00", "10 ff 8f 83 b5 07 00")  // codename read fails: busy
        .expect(longFrame("11 02 00 11 00 00 a5"), "");
    expectEmptySlots(rq);

    const auto report = app::collectStatus(rq, "COL01", "DESKTOP-CARLOS");
    CHECK(rq.done());
    REQUIRE(report.devices.size() == 2);
    CHECK(report.devices[0].here());
    CHECK(report.devices[0].host == hidpp::HostInfo{3, 0});

    CHECK(app::formatStatus(report) ==
          "receiver   Logi Bolt  COL01\n"
          "this host  DESKTOP-CARLOS\n"
          "\n"
          "slot 1  keyboard  MX Mechanical  (wpid B366, serial 284C459C)\n"
          "        connected here, HID++ 4.5, this host is ch1 of 3\n"
          "        ch1  DESKTOP-CARLOS   <- this host\n"
          "        ch2  (not paired)\n"
          "        ch3  (name not readable)\n"
          "\n"
          "slot 2  mouse     ?  (wpid B034, serial E576CD4A)\n"
          "        not answering: timeout\n"
          "        ! codename: receiver error (HID++ 1.0 error 0x07)\n");
}

TEST_CASE("status reports unexpected receiver errors and empty receivers") {
    ScriptedRequester rq;
    rq.expect("10 ff 83 b5 51 00 00", "")
        .expect("10 ff 83 b5 52 00 00", "10 ff 8f 83 b5 08 00")
        .expect("10 ff 83 b5 52 00 00", "10 ff 8f 83 b5 08 00");
    expectEmptySlots(rq);
    const auto report = app::collectStatus(rq, "/dev/hidraw2", "");
    CHECK(rq.done());
    CHECK(app::formatStatus(report) ==
          "receiver   Logi Bolt  /dev/hidraw2\n"
          "this host  (unknown)\n"
          "\n"
          "no paired devices\n"
          "\n"
          "! slot 1: timeout\n");
}

TEST_CASE("status confirms an empty slot and escapes device-supplied names") {
    ScriptedRequester rq;
    // Slot 1: the first read gets another program's empty-slot error (issue #7);
    // the second read gets the real answer. Its codename contains ESC and a newline.
    rq.expect("10 ff 83 b5 51 00 00", "10 ff 8f 83 b5 08 00")
        .expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 41 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00")
        .expect("10 ff 83 b5 61 01 00", "11 ff 83 b5 61 01 05 4d 1b 5b 0a 58 00 00 00 00 00 00 00 00")
        .expect("10 ff 83 b5 52 00 00", "10 ff 8f 83 b5 08 00")
        .expect("10 ff 83 b5 52 00 00", "10 ff 8f 83 b5 08 00");
    expectEmptySlots(rq);
    const auto report = app::collectStatus(rq, "/dev/hidraw2", "carbon");
    CHECK(rq.done());
    REQUIRE(report.devices.size() == 1);
    CHECK(report.devices[0].codename == "M\x1b[\nX");
    CHECK(app::formatStatus(report).find("slot 1  keyboard  M?[?X  (wpid B366") != std::string::npos);
}

TEST_CASE("localHostName has no domain part") {
    CHECK(app::localHostName().find('.') == std::string::npos);
}
