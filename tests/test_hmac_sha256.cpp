/**
 * @file test_hmac_sha256.cpp
 * @brief Tests for the vendored SHA-256 / HMAC-SHA256: FIPS 180 examples,
 *        all RFC 4231 test cases, incremental hashing and constantTimeEqual.
 *
 * The expected values were also checked against Python's hashlib/hmac
 * (task 2.1).
 */
#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "hidpp/hex.hpp"
#include "net/hmac_sha256.hpp"

using duckling::net::constantTimeEqual;
using duckling::net::hmacSha256;
using duckling::net::Sha256;

namespace {

/** @brief The bytes of a string literal, without a terminator. */
std::vector<std::uint8_t> bytesOf(std::string_view s) {
    return {s.begin(), s.end()};
}

/** @brief Hex digits with no separators, e.g. "e3b0c442...". */
std::string hexNoSpaces(std::span<const std::uint8_t> bytes) {
    std::string out = duckling::hidpp::toHex(bytes);
    std::erase(out, ' ');
    return out;
}

}  // namespace

TEST_CASE("SHA-256 matches the FIPS 180 examples") {
    CHECK(hexNoSpaces(Sha256::hash({})) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hexNoSpaces(Sha256::hash(bytesOf("abc"))) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 56 bytes: the padding no longer fits, so it takes a second block.
    CHECK(hexNoSpaces(Sha256::hash(
              bytesOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    Sha256 h;  // one million 'a', fed in uneven pieces
    const std::vector<std::uint8_t> chunk(999, 'a');
    std::size_t left = 1'000'000;
    while (left > 0) {
        const std::size_t n = std::min(left, chunk.size());
        h.update(std::span(chunk).first(n));
        left -= n;
    }
    CHECK(hexNoSpaces(h.finish()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 gives the same digest however the input is split") {
    // Lengths around the padding boundaries (55/56/64) and a few blocks.
    for (std::size_t len : {0u, 1u, 55u, 56u, 57u, 63u, 64u, 65u, 119u, 120u, 128u, 200u}) {
        std::vector<std::uint8_t> msg(len);
        for (std::size_t i = 0; i < len; ++i) {
            msg[i] = static_cast<std::uint8_t>(i * 7 + 3);
        }
        const auto whole = Sha256::hash(msg);
        for (std::size_t cut = 0; cut <= len; ++cut) {
            Sha256 h;
            h.update(std::span(msg).first(cut));
            h.update(std::span(msg).subspan(cut));
            REQUIRE_MESSAGE(h.finish() == whole, "len " << len << " cut " << cut);
        }
    }
}

TEST_CASE("HMAC-SHA256 matches RFC 4231") {
    struct Case {
        const char* name;
        std::vector<std::uint8_t> key;
        std::vector<std::uint8_t> data;
        const char* mac;  // full, or truncated for case 5
    };
    std::vector<std::uint8_t> key4;
    for (std::uint8_t b = 0x01; b <= 0x19; ++b) {
        key4.push_back(b);
    }
    const std::vector<Case> cases = {
        {"1", std::vector<std::uint8_t>(20, 0x0b), bytesOf("Hi There"),
         "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"},
        {"2 (key shorter than the output)", bytesOf("Jefe"),
         bytesOf("what do ya want for nothing?"),
         "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"},
        {"3", std::vector<std::uint8_t>(20, 0xaa), std::vector<std::uint8_t>(50, 0xdd),
         "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"},
        {"4", key4, std::vector<std::uint8_t>(50, 0xcd),
         "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"},
        {"5 (truncated to 128 bits, as on the wire)", std::vector<std::uint8_t>(20, 0x0c),
         bytesOf("Test With Truncation"), "a3b6167473100ee06e0c796c2955552b"},
        {"6 (key longer than a block)", std::vector<std::uint8_t>(131, 0xaa),
         bytesOf("Test Using Larger Than Block-Size Key - Hash Key First"),
         "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"},
        {"7 (key and data longer than a block)", std::vector<std::uint8_t>(131, 0xaa),
         bytesOf("This is a test using a larger than block-size key and a larger than "
                 "block-size data. The key needs to be hashed before being used by the "
                 "HMAC algorithm."),
         "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.name);
        const auto mac = hmacSha256(c.key, c.data);
        const std::string expected = c.mac;
        CHECK(hexNoSpaces(std::span(mac).first(expected.size() / 2)) == expected);
    }
}

TEST_CASE("HMAC-SHA256 key edge cases") {
    const auto msg = bytesOf("duckling");
    // A 64-byte key is used as is; 65 bytes gets hashed. Both differ from
    // each other and from the empty key.
    const std::vector<std::uint8_t> k64(64, 0x42), k65(65, 0x42);
    CHECK(hmacSha256(k64, msg) != hmacSha256(k65, msg));
    CHECK(hmacSha256({}, msg) != hmacSha256(k64, msg));
    // RFC 2104: a long key behaves exactly like its SHA-256 digest.
    const auto k65hash = Sha256::hash(k65);
    CHECK(hmacSha256(k65, msg) == hmacSha256(k65hash, msg));
    // Zero padding: a short key equals itself padded with zeros to 64 bytes.
    std::vector<std::uint8_t> padded = bytesOf("Jefe");
    padded.resize(64, 0);
    CHECK(hmacSha256(bytesOf("Jefe"), msg) == hmacSha256(padded, msg));
}

TEST_CASE("constantTimeEqual") {
    const std::vector<std::uint8_t> a = {1, 2, 3, 4}, b = {1, 2, 3, 4}, c = {1, 2, 3, 5},
                                    d = {1, 2, 3};
    CHECK(constantTimeEqual(a, b));
    CHECK_FALSE(constantTimeEqual(a, c));
    CHECK_FALSE(constantTimeEqual(a, d));
    CHECK(constantTimeEqual({}, {}));
}
