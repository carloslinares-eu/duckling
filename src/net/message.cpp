/**
 * @file message.cpp
 * @brief HELLO/ARRIVED encoding, signing and checked decoding.
 */
#include "net/message.hpp"

#include <algorithm>
#include <stdexcept>

#include "net/hmac_sha256.hpp"

namespace duckling::net {

namespace {

constexpr std::uint8_t kMagic[4] = {'D', 'U', 'C', 'K'};

/**
 * @brief Whether @p s is well-formed UTF-8 with no control characters.
 *
 * Rejects overlong forms, surrogates (U+D800..DFFF), code points above
 * U+10FFFF, C0 controls, DEL and C1 controls (U+0080..009F), so a hostname
 * can be printed in a log without escaping.
 */
bool isCleanUtf8(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<std::uint8_t>(s[i]);
        if (b0 < 0x80) {
            if (b0 < 0x20 || b0 == 0x7F) {
                return false;
            }
            ++i;
            continue;
        }
        std::size_t len = 0;
        std::uint32_t cp = 0;
        std::uint32_t min = 0;
        if ((b0 & 0xE0) == 0xC0) {
            len = 2;
            cp = b0 & 0x1F;
            min = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            len = 3;
            cp = b0 & 0x0F;
            min = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            len = 4;
            cp = b0 & 0x07;
            min = 0x10000;
        } else {
            return false;  // stray continuation byte or 0xF8..0xFF
        }
        if (s.size() - i < len) {
            return false;
        }
        for (std::size_t j = 1; j < len; ++j) {
            const auto b = static_cast<std::uint8_t>(s[i + j]);
            if ((b & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (b & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
            (cp >= 0x80 && cp <= 0x9F)) {
            return false;
        }
        i += len;
    }
    return true;
}

/** @brief Append @p v big-endian in @p n bytes. */
void putBe(std::vector<std::uint8_t>& out, std::uint64_t v, int n) {
    for (int i = n - 1; i >= 0; --i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

/** @brief Read @p n bytes big-endian at @p p. The caller checks bounds. */
std::uint64_t getBe(const std::uint8_t* p, int n) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) {
        v = (v << 8) | p[i];
    }
    return v;
}

}  // namespace

std::string_view describe(ParseError error) noexcept {
    switch (error) {
        case ParseError::TooShort: return "too short";
        case ParseError::TooLong: return "too long";
        case ParseError::BadMagic: return "not a duckling message (bad magic)";
        case ParseError::BadVersion: return "unsupported wire version";
        case ParseError::BadLength: return "length field doesn't match the datagram size";
        case ParseError::BadMac: return "bad MAC (wrong secret or tampered)";
        case ParseError::BadType: return "unknown message type";
        case ParseError::BadHostname: return "invalid hostname";
        case ParseError::BadDeviceCount: return "invalid device count";
        case ParseError::BadDevice: return "invalid device entry";
        case ParseError::LengthMismatch: return "fields don't fill the payload exactly";
    }
    return "unknown error";
}

std::optional<ParseError> validate(const Message& m) {
    if (m.type != MessageType::Hello && m.type != MessageType::Arrived) {
        return ParseError::BadType;
    }
    if (m.hostname.empty() || m.hostname.size() > kMaxHostnameSize ||
        !isCleanUtf8(m.hostname)) {
        return ParseError::BadHostname;
    }
    if (m.devices.size() > kMaxDevices ||
        (m.type == MessageType::Arrived && m.devices.empty())) {
        return ParseError::BadDeviceCount;
    }
    for (std::size_t i = 0; i < m.devices.size(); ++i) {
        const auto& d = m.devices[i];
        if (d.wpid == 0 || d.channel < 1 || d.channel > kMaxChannel) {
            return ParseError::BadDevice;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (m.devices[j].wpid == d.wpid) {
                return ParseError::BadDevice;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> serialize(const Message& m, std::span<const std::uint8_t> key) {
    if (key.empty()) {
        throw std::invalid_argument("duckling message: empty secret");
    }
    if (const auto err = validate(m)) {
        throw std::invalid_argument(std::string("duckling message: ") +
                                    std::string(describe(*err)));
    }
    const std::size_t payload = 8 + 4 + 1 + m.hostname.size() + 1 + 3 * m.devices.size();

    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + payload + kMacSize);
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    out.push_back(kWireVersion);
    out.push_back(static_cast<std::uint8_t>(m.type));
    putBe(out, payload, 2);
    putBe(out, m.timestampMs, 8);
    putBe(out, m.seq, 4);
    out.push_back(static_cast<std::uint8_t>(m.hostname.size()));
    out.insert(out.end(), m.hostname.begin(), m.hostname.end());
    out.push_back(static_cast<std::uint8_t>(m.devices.size()));
    for (const auto& d : m.devices) {
        putBe(out, d.wpid, 2);
        out.push_back(d.channel);
    }
    const Sha256Digest mac = hmacSha256(key, out);
    out.insert(out.end(), mac.begin(), mac.begin() + kMacSize);
    return out;
}

ParseResult parse(std::span<const std::uint8_t> d, std::span<const std::uint8_t> key) noexcept {
    if (d.size() < kMinFrameSize) {
        return ParseError::TooShort;
    }
    if (d.size() > kMaxFrameSize) {
        return ParseError::TooLong;
    }
    if (!std::equal(std::begin(kMagic), std::end(kMagic), d.begin())) {
        return ParseError::BadMagic;
    }
    if (d[4] != kWireVersion) {
        return ParseError::BadVersion;
    }
    const std::size_t payload = getBe(&d[6], 2);
    if (kHeaderSize + payload + kMacSize != d.size()) {
        return ParseError::BadLength;
    }
    const auto signedPart = d.first(d.size() - kMacSize);
    const Sha256Digest mac = hmacSha256(key, signedPart);
    if (key.empty() ||
        !constantTimeEqual(std::span(mac).first(kMacSize), d.last(kMacSize))) {
        return ParseError::BadMac;
    }

    // Authenticated from here on, but still checked: a peer with the secret
    // can have a bug. `end` is the first MAC byte; `p` never passes it.
    Message m;
    if (d[5] != static_cast<std::uint8_t>(MessageType::Hello) &&
        d[5] != static_cast<std::uint8_t>(MessageType::Arrived)) {
        return ParseError::BadType;
    }
    m.type = static_cast<MessageType>(d[5]);
    const std::size_t end = d.size() - kMacSize;
    std::size_t p = kHeaderSize;
    m.timestampMs = getBe(&d[p], 8);
    p += 8;
    m.seq = static_cast<std::uint32_t>(getBe(&d[p], 4));
    p += 4;

    const std::size_t n = d[p++];  // in bounds: kMinFrameSize covers it
    if (n == 0 || n > kMaxHostnameSize || end - p < n + 1) {
        return n == 0 || n > kMaxHostnameSize ? ParseError::BadHostname
                                              : ParseError::LengthMismatch;
    }
    try {
        m.hostname.assign(reinterpret_cast<const char*>(&d[p]), n);
    } catch (...) {
        return ParseError::BadHostname;  // allocation failure; keeps noexcept honest
    }
    p += n;

    const std::size_t k = d[p++];
    if (k > kMaxDevices) {
        return ParseError::BadDeviceCount;
    }
    if (end - p != 3 * k) {
        return ParseError::LengthMismatch;
    }
    try {
        m.devices.reserve(k);
        for (std::size_t i = 0; i < k; ++i, p += 3) {
            m.devices.push_back({static_cast<std::uint16_t>(getBe(&d[p], 2)), d[p + 2]});
        }
    } catch (...) {
        return ParseError::BadDevice;
    }
    if (const auto err = validate(m)) {
        return *err;
    }
    return m;
}

}  // namespace duckling::net
