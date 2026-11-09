/**
 * @file test_report.cpp
 * @brief Tests for duckling::hidpp::Report, built on frames recorded during
 *        the hardware checks (PLAN.md C2–C5).
 */
#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>

#include "fixtures.hpp"
#include "hidpp/hex.hpp"
#include "hidpp/report.hpp"

using namespace duckling::hidpp;

namespace {

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

TEST_CASE("every frame in hw-logs decodes and re-encodes byte-identically") {
    const auto scan = duckling::test::scanAllLogs();
    // c3 + c4 + c5 hold well over 50 frames; guard against a broken loader.
    REQUIRE(scan.frames.size() > 50);
    for (const auto& m : scan.malformed) {
        CAPTURE(m.file);
        CAPTURE(m.line);
        FAIL("truncated frame in recording: " << toHex(m.bytes));
    }
    for (const auto& f : scan.frames) {
        CAPTURE(f.file);
        CAPTURE(f.line);
        const auto r = Report::decode(f.bytes);
        REQUIRE(r.has_value());
        CHECK(r->encode() == f.bytes);
        CHECK(r->length() == f.bytes.size());
    }
}

TEST_CASE("keyboard CHANGE_HOST event before Easy-Switch (C2)") {
    // Keyboard pressed Easy-Switch 2 while on ch3: "from host 2 to host 1" (0-based).
    const Report r = decodeHex("11 01 09 00 02 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
    CHECK(r.id() == ReportId::Long);
    CHECK(r.deviceIndex() == 1);
    CHECK(r.featureIndex() == 9);
    CHECK(r.function() == 0);
    CHECK(r.swId() == 0);  // swId 0 = unsolicited device event
    CHECK(r.param(0) == 2);
    CHECK(r.param(1) == 1);
    CHECK(r.params().size() == Report::kLongParams);
    CHECK_FALSE(r.isError());
}

TEST_CASE("receiver connection notice is a short HID++ 1.0 report (C2)") {
    const Report r = decodeHex("10 01 41 10 41 66 b3");
    CHECK(r.id() == ReportId::Short);
    CHECK(r.length() == kShortLength);
    CHECK(r.deviceIndex() == 1);
    CHECK(r.featureIndex() == 0x41);  // sub-id: device connection
    CHECK(r.functionByte() == 0x10);
    CHECK(r.params().size() == Report::kShortParams);
    CHECK(r.param(0) == 0x41);  // bit 6 set = link down; low bits 1 = keyboard
    CHECK(r.param(1) == 0x66);
    CHECK(r.param(2) == 0xB3);
    CHECK(r.param(3) == 0);  // out of range reads as 0
    CHECK(r.param(SIZE_MAX) == 0);
    CHECK(r.param(SIZE_MAX - 1) == 0);  // no wrap-around into the header
}

TEST_CASE("errors are recognised (C2: Solaar's register write rejected)") {
    CHECK(decodeHex("10 ff 8f 80 03 02 00").isError());
    CHECK(decodeHex("10 ff 8f 80 03 02 00").deviceIndex() == kReceiverIndex);
    CHECK(decodeHex("11 01 ff 1d 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00").isError());
    CHECK_FALSE(decodeHex("11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00").isError());
}

TEST_CASE("make builds the exact frames sent in C4/C5") {
    // C4 ping: ROOT (feature 0) function 1, swId 0xA, ping byte 0xAA.
    const std::uint8_t ping[] = {0x00, 0x00, 0xAA};
    CHECK(toHex(Report::make(ReportId::Long, 1, 0x00, 1, 0xA, ping).bytes()) ==
          "11 01 00 1a 00 00 aa 00 00 00 00 00 00 00 00 00 00 00 00 00");

    // C5 setCurrentHost(ch3): CHANGE_HOST (index 9) function 1, swId 0xA.
    const std::uint8_t host[] = {0x02};
    const Report set = Report::make(ReportId::Long, 1, 0x09, 1, 0xA, host);
    CHECK(toHex(set.bytes()) == "11 01 09 1a 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
    CHECK(set.function() == 1);
    CHECK(set.swId() == 0xA);

    CHECK(Report::make(ReportId::Short, 0xFF, 0x81, 0, 2).encode() ==
          std::vector<std::uint8_t>{0x10, 0xFF, 0x81, 0x02, 0, 0, 0});
}

TEST_CASE("make validates its arguments") {
    const std::uint8_t four[4] = {};
    const std::uint8_t seventeen[17] = {};
    CHECK_THROWS_AS((void)Report::make(ReportId::Long, 1, 0, 16, 0), std::invalid_argument);
    CHECK_THROWS_AS((void)Report::make(ReportId::Long, 1, 0, 0, 16), std::invalid_argument);
    CHECK_THROWS_AS((void)Report::make(ReportId::Short, 1, 0, 0, 0, four), std::invalid_argument);
    CHECK_THROWS_AS((void)Report::make(ReportId::Long, 1, 0, 0, 0, seventeen), std::invalid_argument);
    CHECK_THROWS_AS((void)Report::make(static_cast<ReportId>(0x12), 1, 0, 0, 0), std::invalid_argument);
    const std::uint8_t three[3] = {1, 2, 3};
    const std::uint8_t sixteen[16] = {};
    CHECK_NOTHROW((void)Report::make(ReportId::Short, 1, 0, 15, 15, three));
    CHECK_NOTHROW((void)Report::make(ReportId::Long, 1, 0, 0, 0, sixteen));
}

TEST_CASE("decode rejects unknown IDs and truncated frames") {
    using V = std::vector<std::uint8_t>;
    CHECK_FALSE(Report::decode(V{}).has_value());
    CHECK_FALSE(Report::decode(V{0x12, 0, 0, 0, 0, 0, 0}).has_value());  // very long: unsupported
    CHECK_FALSE(Report::decode(V{0x10, 1, 0x41, 0x10, 0x41, 0x66}).has_value());  // 6 bytes
    CHECK_FALSE(Report::decode(V(19, 0x11)).has_value());
}

TEST_CASE("decode ignores trailing bytes from fixed-size HID buffers") {
    // A short report read into a 20-byte buffer (Windows may hand out padded reads).
    std::vector<std::uint8_t> buf{0x10, 0x02, 0x41, 0x10, 0x02, 0x34, 0xB0};
    buf.resize(20, 0xEE);
    const auto r = Report::decode(buf);
    REQUIRE(r.has_value());
    CHECK(r->length() == kShortLength);
    CHECK(toHex(r->bytes()) == "10 02 41 10 02 34 b0");
}

TEST_CASE("isReplyTo matches replies and errors to their request") {
    const std::uint8_t ping[] = {0x00, 0x00, 0xAA};
    const Report req = Report::make(ReportId::Long, 1, 0x00, 1, 0xA, ping);
    // C4: ping answered.
    CHECK(isReplyTo(req, decodeHex("11 01 00 1a 04 05 aa 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    // Task 1.3 probe: device unreachable / empty slot -> HID++ 1.0 error from the receiver.
    CHECK(isReplyTo(Report::make(ReportId::Long, 3, 0x00, 1, 0xD, ping), decodeHex("10 03 8f 00 1d 09 00")));
    // HID++ 2.0 error from the device itself.
    CHECK(isReplyTo(req, decodeHex("11 01 ff 00 1a 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    // Not replies: other device, other swId, other function, other feature, event.
    CHECK_FALSE(isReplyTo(req, decodeHex("11 02 00 1a 04 05 aa 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    CHECK_FALSE(isReplyTo(req, decodeHex("11 01 00 1d 04 05 aa 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    CHECK_FALSE(isReplyTo(req, decodeHex("11 01 00 0a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    CHECK_FALSE(isReplyTo(req, decodeHex("11 01 09 1a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")));
    CHECK_FALSE(isReplyTo(req, decodeHex("10 01 41 10 41 66 b3")));
    // An error for a different request on the same device.
    CHECK_FALSE(isReplyTo(req, decodeHex("10 01 8f 09 1d 04 00")));
    CHECK_FALSE(isReplyTo(req, decodeHex("10 01 8f 00 1b 04 00")));
}

TEST_CASE("isReplyTo checks the sub-register of HID++ 1.0 long register reads") {
    const std::uint8_t sub62[] = {0x62};
    const Report req = Report::make(ReportId::Short, 0xFF, kGetLongRegisterSubId, 0xB, 0x5, sub62);
    CHECK(toHex(req.bytes()) == "10 ff 83 b5 62 00 00");
    CHECK(isReplyTo(req, decodeHex("11 ff 83 b5 62 02 0a 4d 58 20 4d 61 73 74 65 72 00 00 00 00")));
    // W1: Options+ reading slot 1's name on the same interface.
    CHECK_FALSE(isReplyTo(req, decodeHex("11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00")));
    CHECK(isReplyTo(req, decodeHex("10 ff 8f 83 b5 03 00")));
}

TEST_CASE("isRegisterAccess and duckling's software id") {
    CHECK(isRegisterAccess(0x80));
    CHECK(isRegisterAccess(0x83));
    CHECK_FALSE(isRegisterAccess(0x41));
    CHECK_FALSE(isRegisterAccess(0x8F));
    CHECK_FALSE(isRegisterAccess(0x00));
    // Solaar uses 0x2..0xF and Options+ 0xA/0xB (C2, C3, W1); 0 is for events.
    CHECK(kDucklingSwId == 0x1);
}

TEST_CASE("frameLength and equality") {
    CHECK(frameLength(0x10) == kShortLength);
    CHECK(frameLength(0x11) == kLongLength);
    CHECK_FALSE(frameLength(0x20).has_value());
    CHECK(decodeHex("10 01 41 10 41 66 b3") == decodeHex("10 01 41 10 41 66 b3"));
    CHECK_FALSE(decodeHex("10 01 41 10 41 66 b3") == decodeHex("10 01 41 10 01 66 b3"));
}
