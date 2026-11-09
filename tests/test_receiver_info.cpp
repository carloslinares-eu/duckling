/**
 * @file test_receiver_info.cpp
 * @brief Tests for the Bolt pairing register reads, from hw-logs/r1-… (task 1.5).
 */
#include <doctest/doctest.h>

#include <stdexcept>
#include <string_view>

#include "hidpp/hex.hpp"
#include "hidpp/receiver_info.hpp"
#include "scripted_requester.hpp"

using namespace duckling::hidpp;
using duckling::test::ScriptedRequester;

TEST_CASE("getPairingInfo decodes r1's keyboard and mouse") {
    ScriptedRequester rq;
    rq.expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 41 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00")
        .expect("10 ff 83 b5 52 00 00", "11 ff 83 b5 52 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00");
    const auto kbd = getPairingInfo(rq, 1);
    REQUIRE(kbd.ok());
    CHECK(*kbd == PairingInfo{1, false, 0x01, 0xB366, 0x284C459C});
    CHECK(kbd->kind() == DeviceKind::Keyboard);
    const auto mouse = getPairingInfo(rq, 2);
    REQUIRE(mouse.ok());
    CHECK(*mouse == PairingInfo{2, false, 0x02, 0xB034, 0xE576CD4A});
    CHECK(rq.done());
}

TEST_CASE("getPairingInfo: link up, empty slot, wrong sub-register") {
    ScriptedRequester rq;
    // Same keyboard with the link-down bit clear.
    rq.expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 01 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00");
    CHECK(getPairingInfo(rq, 1)->linkUp);
    // r1: empty slots.
    rq.expect("10 ff 83 b5 53 00 00", "10 ff 8f 83 b5 08 00");
    CHECK(getPairingInfo(rq, 3).failure().unknownDevice());
    rq.expect("10 ff 83 b5 54 00 00", "11 ff 83 b5 51 01 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00");
    CHECK(getPairingInfo(rq, 4).failure().kind == FailureKind::BadReply);
    CHECK_THROWS_AS((void)getPairingInfo(rq, 0), std::invalid_argument);
    CHECK_THROWS_AS((void)getPairingInfo(rq, 7), std::invalid_argument);
    CHECK(rq.done());
}

TEST_CASE("parsePairingInfo and parseCodename decode anyone's replies") {
    const auto bytes = [](std::string_view hex) {
        return *Report::decode(*parseHex(hex));
    };
    const auto p = parsePairingInfo(bytes("11 ff 83 b5 52 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00"));
    REQUIRE(p.has_value());
    CHECK(*p == PairingInfo{2, false, 0x02, 0xB034, 0xE576CD4A});
    const auto c = parseCodename(bytes("11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00"));
    REQUIRE(c.has_value());
    CHECK(*c == Codename{1, "MX MCHNCL"});
    // Not these: other sub-registers, slot 0 or 7, short errors, device reports.
    CHECK_FALSE(parsePairingInfo(bytes("11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00")));
    CHECK_FALSE(parsePairingInfo(bytes("11 ff 83 b5 50 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")));
    CHECK_FALSE(parsePairingInfo(bytes("11 ff 83 b5 57 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")));
    CHECK_FALSE(parseCodename(bytes("10 ff 8f 83 b5 08 00")));
    CHECK_FALSE(parseCodename(bytes("11 02 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00")));
}

TEST_CASE("getCodename (r1, W1)") {
    ScriptedRequester rq;
    rq.expect("10 ff 83 b5 61 01 00", "11 ff 83 b5 61 01 09 4d 58 20 4d 43 48 4e 43 4c 00 00 00 00")
        .expect("10 ff 83 b5 62 01 00", "11 ff 83 b5 62 01 0c 4d 58 20 4d 61 73 74 65 72 20 33 53 00")
        .expect("10 ff 83 b5 63 01 00", "10 ff 8f 83 b5 08 00");
    CHECK(*getCodename(rq, 1) == "MX MCHNCL");
    CHECK(*getCodename(rq, 2) == "MX Master 3S");
    CHECK(getCodename(rq, 3).failure().unknownDevice());
    CHECK(rq.done());
}
