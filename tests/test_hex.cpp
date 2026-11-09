/**
 * @file test_hex.cpp
 * @brief Tests for duckling::hidpp::toHex / parseHex.
 */
#include <doctest/doctest.h>

#include "hidpp/hex.hpp"

using duckling::hidpp::parseHex;
using duckling::hidpp::toHex;

TEST_CASE("toHex formats lowercase pairs like the recorders") {
    const std::uint8_t bytes[] = {0x10, 0x01, 0x41, 0x10, 0x41, 0x66, 0xB3};
    CHECK(toHex(bytes) == "10 01 41 10 41 66 b3");
    CHECK(toHex({}) == "");
}

TEST_CASE("parseHex accepts the formats used in logs and on the command line") {
    using V = std::vector<std::uint8_t>;
    CHECK(parseHex("11 01 09 1A 02") == V{0x11, 0x01, 0x09, 0x1A, 0x02});
    CHECK(parseHex("0x11,0x01,0X09") == V{0x11, 0x01, 0x09});
    CHECK(parseHex("  f\t0a\n") == V{0x0F, 0x0A});
    CHECK(parseHex("") == V{});
}

TEST_CASE("parseHex rejects invalid tokens") {
    CHECK_FALSE(parseHex("11 0g").has_value());
    CHECK_FALSE(parseHex("123").has_value());
    CHECK_FALSE(parseHex("0x").has_value());
    CHECK_FALSE(parseHex("0x123").has_value());
}

TEST_CASE("toHex and parseHex round-trip") {
    std::vector<std::uint8_t> all(256);
    for (int i = 0; i < 256; ++i) all[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    CHECK(parseHex(toHex(all)) == all);
}
