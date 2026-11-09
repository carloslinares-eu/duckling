/**
 * @file hmac_sha256.cpp
 * @brief SHA-256 and HMAC-SHA256 implementation (FIPS 180-4, RFC 2104).
 */
#include "net/hmac_sha256.hpp"

#include <algorithm>

namespace duckling::net {

namespace {

/** @brief FIPS 180-4 section 4.2.2: the first 32 bits of the fractional
 *         parts of the cube roots of the first 64 primes. */
constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/** @brief FIPS 180-4 section 5.3.3: initial hash value H(0). */
constexpr std::array<std::uint32_t, 8> kInit = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

/** @brief Rotate right. */
constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

/** @brief Overwrite a buffer with zeros in a way the optimizer keeps. */
void wipe(std::span<std::uint8_t> bytes) noexcept {
    volatile std::uint8_t* p = bytes.data();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        p[i] = 0;
    }
}

}  // namespace

Sha256::Sha256() noexcept : state_(kInit) {}

void Sha256::compress(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t{block[4 * i]} << 24) | (std::uint32_t{block[4 * i + 1]} << 16) |
               (std::uint32_t{block[4 * i + 2]} << 8) | std::uint32_t{block[4 * i + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + S1 + ch + kRound[i] + w[i];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> data) noexcept {
    totalLen_ += data.size();
    std::size_t pos = 0;
    if (bufLen_ > 0) {
        const std::size_t take = std::min(kSha256BlockSize - bufLen_, data.size());
        std::copy_n(data.data(), take, buf_.data() + bufLen_);
        bufLen_ += take;
        pos = take;
        if (bufLen_ < kSha256BlockSize) {
            return;
        }
        compress(buf_.data());
        bufLen_ = 0;
    }
    for (; data.size() - pos >= kSha256BlockSize; pos += kSha256BlockSize) {
        compress(data.data() + pos);
    }
    std::copy(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end(), buf_.begin());
    bufLen_ = data.size() - pos;
}

Sha256Digest Sha256::finish() noexcept {
    // FIPS 180-4 section 5.1.1: append 0x80, zeros up to 56 mod 64, then the
    // message length in bits as a 64-bit big-endian number.
    const std::uint64_t bits = totalLen_ * 8;
    buf_[bufLen_++] = 0x80;
    if (bufLen_ > 56) {
        std::fill(buf_.begin() + static_cast<std::ptrdiff_t>(bufLen_), buf_.end(), 0);
        compress(buf_.data());
        bufLen_ = 0;
    }
    std::fill(buf_.begin() + static_cast<std::ptrdiff_t>(bufLen_), buf_.begin() + 56, 0);
    for (int i = 0; i < 8; ++i) {
        buf_[56 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    }
    compress(buf_.data());
    wipe(buf_);
    bufLen_ = 0;

    Sha256Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<std::uint8_t>(state_[i] >> 24);
        out[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        out[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        out[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return out;
}

Sha256Digest Sha256::hash(std::span<const std::uint8_t> data) noexcept {
    Sha256 h;
    h.update(data);
    return h.finish();
}

Sha256Digest hmacSha256(std::span<const std::uint8_t> key,
                        std::span<const std::uint8_t> message) noexcept {
    // RFC 2104: K0 = key padded to the block size (hashed first if longer).
    std::array<std::uint8_t, kSha256BlockSize> k0{};
    if (key.size() > kSha256BlockSize) {
        Sha256Digest kh = Sha256::hash(key);
        std::copy(kh.begin(), kh.end(), k0.begin());
        wipe(kh);
    } else {
        std::copy(key.begin(), key.end(), k0.begin());
    }

    std::array<std::uint8_t, kSha256BlockSize> pad{};
    for (std::size_t i = 0; i < kSha256BlockSize; ++i) {
        pad[i] = static_cast<std::uint8_t>(k0[i] ^ 0x36);
    }
    Sha256 inner;
    inner.update(pad);
    inner.update(message);
    Sha256Digest innerHash = inner.finish();

    for (std::size_t i = 0; i < kSha256BlockSize; ++i) {
        pad[i] = static_cast<std::uint8_t>(k0[i] ^ 0x5c);
    }
    Sha256 outer;
    outer.update(pad);
    outer.update(innerHash);
    const Sha256Digest mac = outer.finish();

    wipe(k0);
    wipe(pad);
    wipe(innerHash);
    return mac;
}

bool constantTimeEqual(std::span<const std::uint8_t> a,
                       std::span<const std::uint8_t> b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    }
    // Read through volatile so the compiler can't turn the loop into an
    // early-exit comparison.
    volatile std::uint8_t result = diff;
    return result == 0;
}

}  // namespace duckling::net
