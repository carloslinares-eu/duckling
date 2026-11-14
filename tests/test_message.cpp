/**
 * @file test_message.cpp
 * @brief Tests for duckling::net message encoding and checked decoding:
 *        golden frames, every rejection reason, truncation, bit flips and
 *        fuzz-style random input.
 *
 * The golden frames were produced by an independent Python encoder
 * (struct + hmac, task 2.2), not by this code.
 */
#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "hidpp/hex.hpp"
#include "net/hmac_sha256.hpp"
#include "net/message.hpp"

using namespace duckling::net;
using Bytes = std::vector<std::uint8_t>;

namespace {

const std::string kSecret = "duckling-test-secret";

/** @brief The test secret as bytes. */
std::span<const std::uint8_t> key() {
    return {reinterpret_cast<const std::uint8_t*>(kSecret.data()), kSecret.size()};
}

/** @brief Parse hex from a test literal; fails the test on bad hex. */
Bytes hex(std::string_view s) {
    auto b = duckling::hidpp::parseHex(s);
    REQUIRE(b.has_value());
    return *b;
}

/** @brief The error parse() gave, or nullopt if it accepted. */
std::optional<ParseError> errorOf(const ParseResult& r) {
    if (const auto* e = std::get_if<ParseError>(&r)) {
        return *e;
    }
    return std::nullopt;
}

/** @brief Replace the last 16 bytes with a correct MAC over the rest. */
void resign(Bytes& frame) {
    const auto mac = hmacSha256(key(), std::span(frame).first(frame.size() - kMacSize));
    std::copy(mac.begin(), mac.begin() + kMacSize, frame.end() - kMacSize);
}

/** @brief Signed frame from raw fields, without any validation, so tests
 *         can build frames serialize() refuses to produce. */
Bytes rawFrame(std::uint8_t type, std::string_view host, std::uint8_t hostLen,
               const Bytes& devices, std::uint8_t count) {
    Bytes f = {'D', 'U', 'C', 'K', kWireVersion, type, 0, 0};
    for (int i = 0; i < 12; ++i) {
        f.push_back(static_cast<std::uint8_t>(i));  // timestamp + seq
    }
    f.push_back(hostLen);
    f.insert(f.end(), host.begin(), host.end());
    f.push_back(count);
    f.insert(f.end(), devices.begin(), devices.end());
    const std::size_t payload = f.size() - kHeaderSize;
    f[6] = static_cast<std::uint8_t>(payload >> 8);
    f[7] = static_cast<std::uint8_t>(payload);
    f.resize(f.size() + kMacSize);
    resign(f);
    return f;
}

const Message kHello{MessageType::Hello, 1762714800123, 7, "carbon", {{0xB366, 3}, {0xB034, 3}}};
const Message kArrived{MessageType::Arrived, 1762714800456, 8, "DESKTOP-CARLOS", {{0xB034, 1}}};

const char* const kHelloHex =
    "44 55 43 4b 01 01 00 1a 00 00 01 9a 69 fd 4f fb 00 00 00 07 06 63 61 72 62 6f 6e 02 b3 66 "
    "03 b0 34 03 d3 86 4a 2c a6 fc cc 18 7e ff b6 9f dc 41 7d 2c";
const char* const kArrivedHex =
    "44 55 43 4b 01 02 00 1f 00 00 01 9a 69 fd 51 48 00 00 00 08 0e 44 45 53 4b 54 4f 50 2d 43 "
    "41 52 4c 4f 53 01 b0 34 01 3e e5 52 ad e1 c2 22 89 c6 dd 4a a9 ea 49 5f 32";

}  // namespace

TEST_CASE("serialize matches the independently computed golden frames") {
    CHECK(serialize(kHello, key()) == hex(kHelloHex));
    CHECK(serialize(kArrived, key()) == hex(kArrivedHex));
}

TEST_CASE("parse decodes the golden frames") {
    const auto h = parse(hex(kHelloHex), key());
    REQUIRE(std::holds_alternative<Message>(h));
    CHECK(std::get<Message>(h) == kHello);
    const auto a = parse(hex(kArrivedHex), key());
    REQUIRE(std::holds_alternative<Message>(a));
    CHECK(std::get<Message>(a) == kArrived);
}

TEST_CASE("frame size limits are reached exactly") {
    Message smallest{MessageType::Hello, 0, 0, "a", {}};
    CHECK(serialize(smallest, key()).size() == kMinFrameSize);
    Message largest{MessageType::Hello, UINT64_MAX, UINT32_MAX, std::string(kMaxHostnameSize, 'h'), {}};
    for (std::uint16_t w = 1; w <= kMaxDevices; ++w) {
        largest.devices.push_back({w, kMaxChannel});
    }
    const auto f = serialize(largest, key());
    CHECK(f.size() == kMaxFrameSize);
    CHECK(std::get<Message>(parse(f, key())) == largest);
}

TEST_CASE("the MAC depends on the secret") {
    const auto f = hex(kHelloHex);
    const std::string other = "duckling-test-secreT";
    CHECK(errorOf(parse(f, {reinterpret_cast<const std::uint8_t*>(other.data()), other.size()})) ==
          ParseError::BadMac);
    CHECK(errorOf(parse(f, {})) == ParseError::BadMac);
    CHECK_THROWS_AS((void)serialize(kHello, {}), std::invalid_argument);
}

TEST_CASE("header checks come before the MAC") {
    auto f = hex(kHelloHex);
    CHECK(errorOf(parse(std::span(f).first(kMinFrameSize - 1), key())) == ParseError::TooShort);
    Bytes big(kMaxFrameSize + 1, 0);
    CHECK(errorOf(parse(big, key())) == ParseError::TooLong);
    auto g = f;
    g[0] = 'd';
    CHECK(errorOf(parse(g, key())) == ParseError::BadMagic);
    g = f;
    g[4] = 2;
    CHECK(errorOf(parse(g, key())) == ParseError::BadVersion);
    g = f;
    g[7] ^= 1;
    CHECK(errorOf(parse(g, key())) == ParseError::BadLength);
    g = f;
    g.push_back(0);  // one extra byte: length field no longer matches
    CHECK(errorOf(parse(g, key())) == ParseError::BadLength);
}

TEST_CASE("every truncation of a valid frame is rejected") {
    const auto f = hex(kArrivedHex);
    for (std::size_t n = 0; n < f.size(); ++n) {
        CAPTURE(n);
        CHECK(std::holds_alternative<ParseError>(parse(std::span(f).first(n), key())));
    }
}

TEST_CASE("every single-bit flip of a valid frame is rejected") {
    const auto f = hex(kHelloHex);
    for (std::size_t i = 0; i < f.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            auto g = f;
            g[i] ^= static_cast<std::uint8_t>(1 << bit);
            const auto r = parse(g, key());
            REQUIRE_MESSAGE(std::holds_alternative<ParseError>(r), "byte " << i << " bit " << bit);
        }
    }
}

TEST_CASE("authenticated but malformed fields are rejected") {
    const Bytes dev = {0xB0, 0x34, 0x01};
    CHECK(std::holds_alternative<Message>(parse(rawFrame(1, "carbon", 6, dev, 1), key())));
    CHECK(std::holds_alternative<Message>(parse(rawFrame(2, "carbon", 6, dev, 1), key())));

    CHECK(errorOf(parse(rawFrame(0, "carbon", 6, dev, 1), key())) == ParseError::BadType);
    CHECK(errorOf(parse(rawFrame(3, "carbon", 6, dev, 1), key())) == ParseError::BadType);
    // Hostname length 0, too long, or pointing past the payload.
    CHECK(errorOf(parse(rawFrame(1, "", 0, dev, 1), key())) == ParseError::BadHostname);
    CHECK(errorOf(parse(rawFrame(1, std::string(64, 'h'), 64, {}, 0), key())) ==
          ParseError::BadHostname);
    CHECK(errorOf(parse(rawFrame(1, "carbon", 9, {}, 0), key())) == ParseError::LengthMismatch);
    // Device count vs. the bytes that follow.
    CHECK(errorOf(parse(rawFrame(1, "carbon", 6, dev, 2), key())) == ParseError::LengthMismatch);
    CHECK(errorOf(parse(rawFrame(1, "carbon", 6, dev, 0), key())) == ParseError::LengthMismatch);
    Bytes seven;
    for (std::uint8_t i = 1; i <= 7; ++i) {
        seven.insert(seven.end(), {0x00, i, 0x01});
    }
    CHECK(errorOf(parse(rawFrame(1, "c", 1, seven, 7), key())) == ParseError::BadDeviceCount);
    CHECK(errorOf(parse(rawFrame(2, "carbon", 6, {}, 0), key())) == ParseError::BadDeviceCount);
    CHECK(std::holds_alternative<Message>(parse(rawFrame(1, "carbon", 6, {}, 0), key())));
    // Device entries.
    CHECK(errorOf(parse(rawFrame(1, "carbon", 6, {0x00, 0x00, 0x01}, 1), key())) ==
          ParseError::BadDevice);
    CHECK(errorOf(parse(rawFrame(1, "carbon", 6, {0xB0, 0x34, 0x00}, 1), key())) ==
          ParseError::BadDevice);
    CHECK(errorOf(parse(rawFrame(1, "carbon", 6, {0xB0, 0x34, 0x04}, 1), key())) ==
          ParseError::BadDevice);
    CHECK(errorOf(parse(rawFrame(1, "carbon", 6, {0xB0, 0x34, 0x01, 0xB0, 0x34, 0x02}, 2),
                        key())) == ParseError::BadDevice);
}

TEST_CASE("hostnames must be clean UTF-8") {
    auto host = [](std::string_view h) {
        return parse(rawFrame(1, h, static_cast<std::uint8_t>(h.size()), {}, 0), key());
    };
    CHECK(std::holds_alternative<Message>(host("DESKTOP-CARLOS")));
    CHECK(std::holds_alternative<Message>(host("caf\xC3\xA9")));          // é
    CHECK(std::holds_alternative<Message>(host("\xF0\x9F\xA6\x86")));      // U+1F986 duck
    CHECK(errorOf(host("a\nb")) == ParseError::BadHostname);              // control
    CHECK(errorOf(host("a\x7F")) == ParseError::BadHostname);             // DEL
    CHECK(errorOf(host(std::string("a\0b", 3))) == ParseError::BadHostname);
    CHECK(errorOf(host("\xC2\x85")) == ParseError::BadHostname);          // C1 control
    CHECK(errorOf(host("\xC0\xAF")) == ParseError::BadHostname);          // overlong '/'
    CHECK(errorOf(host("\xED\xA0\x80")) == ParseError::BadHostname);      // surrogate
    CHECK(errorOf(host("\xF4\x90\x80\x80")) == ParseError::BadHostname);  // > U+10FFFF
    CHECK(errorOf(host("\xE2\x82")) == ParseError::BadHostname);          // cut short
    CHECK(errorOf(host("\x80")) == ParseError::BadHostname);              // stray continuation
    CHECK(errorOf(host("\xFF")) == ParseError::BadHostname);
}

TEST_CASE("serialize refuses what parse would reject") {
    auto bad = [](Message m) { CHECK_THROWS_AS((void)serialize(m, key()), std::invalid_argument); };
    bad({MessageType::Hello, 0, 0, "", {}});
    bad({MessageType::Hello, 0, 0, std::string(64, 'h'), {}});
    bad({MessageType::Hello, 0, 0, "a\tb", {}});
    bad({static_cast<MessageType>(9), 0, 0, "carbon", {}});
    bad({MessageType::Arrived, 0, 0, "carbon", {}});
    bad({MessageType::Hello, 0, 0, "carbon", {{0, 1}}});
    bad({MessageType::Hello, 0, 0, "carbon", {{0xB034, 0}}});
    bad({MessageType::Hello, 0, 0, "carbon", {{0xB034, 4}}});
    bad({MessageType::Hello, 0, 0, "carbon", {{0xB034, 1}, {0xB034, 2}}});
    Message seven{MessageType::Hello, 0, 0, "carbon", {}};
    for (std::uint16_t w = 1; w <= 7; ++w) {
        seven.devices.push_back({w, 1});
    }
    bad(seven);
    CHECK_FALSE(validate(kHello).has_value());
    CHECK_FALSE(validate(kArrived).has_value());
}

TEST_CASE("fuzz: random signed frames are either rejected or canonical") {
    // Frames get a valid header and MAC, so the field parser behind the MAC
    // is what gets exercised. Anything accepted must re-encode to the same
    // bytes (one encoding per message), and nothing may crash.
    std::mt19937 rng(20261109);
    std::uniform_int_distribution<int> byte(0, 255);
    std::size_t accepted = 0;
    for (int iter = 0; iter < 200000; ++iter) {
        std::uniform_int_distribution<std::size_t> size(kMinFrameSize, kMaxFrameSize);
        Bytes f(size(rng));
        for (auto& b : f) {
            b = static_cast<std::uint8_t>(byte(rng));
        }
        // Bias toward plausible frames: sometimes small hostname/count fields
        // and printable hostnames, so the deeper checks are reached too.
        std::copy_n("DUCK", 4, f.begin());
        f[4] = kWireVersion;
        f[5] = static_cast<std::uint8_t>(1 + iter % 2);
        const std::size_t payload = f.size() - kHeaderSize - kMacSize;
        f[6] = static_cast<std::uint8_t>(payload >> 8);
        f[7] = static_cast<std::uint8_t>(payload);
        if (iter % 4 != 0) {
            const std::size_t room = payload - 8 - 4 - 1 - 1;
            const std::size_t k = static_cast<std::size_t>(byte(rng)) % (kMaxDevices + 1);
            if (room >= 3 * k + 1) {
                const std::size_t n = room - 3 * k;
                f[20] = static_cast<std::uint8_t>(n);
                for (std::size_t i = 0; i < n; ++i) {
                    f[21 + i] = static_cast<std::uint8_t>('a' + byte(rng) % 26);
                }
                f[21 + n] = static_cast<std::uint8_t>(k);
                for (std::size_t i = 0; i < k; ++i) {
                    f[22 + n + 3 * i + 2] = static_cast<std::uint8_t>(1 + byte(rng) % 4);
                }
            }
        }
        resign(f);
        const auto r = parse(f, key());
        if (const auto* m = std::get_if<Message>(&r)) {
            ++accepted;
            REQUIRE(serialize(*m, key()) == f);
        }
    }
    MESSAGE("accepted " << accepted << " of 200000 random frames");
    CHECK(accepted > 1000);  // the bias works: the accept path is covered
}

TEST_CASE("fuzz: arbitrary bytes never crash parse") {
    std::mt19937 rng(47474);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<std::size_t> size(0, kMaxFrameSize + 8);
    for (int iter = 0; iter < 100000; ++iter) {
        Bytes f(size(rng));
        for (auto& b : f) {
            b = static_cast<std::uint8_t>(byte(rng));
        }
        if (iter % 2 == 0 && f.size() >= 8) {
            std::copy_n("DUCK", 4, f.begin());
            f[4] = kWireVersion;
        }
        CHECK_NOTHROW((void)parse(f, key()));
    }
}

TEST_CASE("describe has a text for every error") {
    for (int e = 0; e <= static_cast<int>(ParseError::LengthMismatch); ++e) {
        CHECK(describe(static_cast<ParseError>(e)) != "unknown error");
    }
}
