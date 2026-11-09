/**
 * @file test_fixtures.cpp
 * @brief Tests for the hw-logs frame scanner itself, so it can't silently
 *        drop or invent frames.
 */
#include <doctest/doctest.h>

#include "fixtures.hpp"
#include "hidpp/hex.hpp"

using duckling::hidpp::toHex;
using duckling::test::scanLog;

TEST_CASE("all hw-logs recordings are embedded") {
    const auto& logs = duckling::test::embeddedLogs();
    REQUIRE(logs.size() >= 3);
    bool c3 = false, c4 = false, c5 = false;
    for (const auto& log : logs) {
        CHECK_FALSE(log.text.empty());
        c3 = c3 || log.name.starts_with("c3-");
        c4 = c4 || log.name.starts_with("c4-");
        c5 = c5 || log.name.starts_with("c5-");
    }
    CHECK(c3);
    CHECK(c4);
    CHECK(c5);
}

TEST_CASE("scanLog reads both recorder formats") {
    const auto scan = scanLog("x.txt",
                              "7956.539 10 01 41 10 41 66 b3  <- keyboard OFFLINE\n"
                              "16:11:40.478 >> 11 01 00 1a 00 00 aa 00 00 00 00 00 00 00 00 00 00 00 00 00\n");
    REQUIRE(scan.frames.size() == 2);
    CHECK(scan.malformed.empty());
    CHECK(toHex(scan.frames[0].bytes) == "10 01 41 10 41 66 b3");
    CHECK(scan.frames[0].line == 1);
    CHECK(scan.frames[1].bytes.size() == 20);
    CHECK(scan.frames[1].line == 2);
}

TEST_CASE("scanLog is not fooled by hex-looking words around a frame") {
    // "C4" and "ab" are valid hex pairs but don't start a frame. "be" after a
    // complete frame doesn't extend it.
    const auto scan = scanLog("x.txt", "# C4 ab 10 01 41 10 01 66 b3 be online\n");
    REQUIRE(scan.frames.size() == 1);
    CHECK(toHex(scan.frames[0].bytes) == "10 01 41 10 01 66 b3");
    CHECK(scan.malformed.empty());
}

TEST_CASE("scanLog reports truncated frames as malformed") {
    const auto scan = scanLog("x.txt", "1.0 11 01 09 00 02 01\n2.0 10 01 41\n");
    CHECK(scan.frames.empty());
    REQUIRE(scan.malformed.size() == 2);
    CHECK(scan.malformed[0].line == 1);
    CHECK(scan.malformed[1].line == 2);
}

TEST_CASE("scanLog ignores marker and text lines") {
    const auto scan = scanLog("x.txt",
                              "7953.131 ===== kdb to ch2 (off) =====\n"
                              "reading /dev/hidraw2; type a label\n"
                              "  2: MX Master 3S\n");
    CHECK(scan.frames.empty());
    CHECK(scan.malformed.empty());
}
