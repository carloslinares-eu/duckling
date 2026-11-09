/**
 * @file hmac_sha256.hpp
 * @brief Vendored SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104), used to
 *        sign and check the UDP messages between duckling hosts.
 *
 * Self-contained on purpose: no OpenSSL / CNG dependency, so the binary stays
 * a single static file on Linux and Windows (PLAN.md "Build and packaging").
 * Checked against the FIPS 180 examples and all RFC 4231 HMAC test cases.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

/**
 * @namespace duckling::net
 * @brief Networking between duckling hosts: the UDP wire format, its HMAC
 *        signature, replay protection and the socket.
 */
namespace duckling::net {

/** @brief Size of a SHA-256 digest, in bytes. */
inline constexpr std::size_t kSha256Size = 32;

/** @brief SHA-256 input block size, in bytes. HMAC pads keys to this size. */
inline constexpr std::size_t kSha256BlockSize = 64;

/** @brief A full SHA-256 / HMAC-SHA256 output. */
using Sha256Digest = std::array<std::uint8_t, kSha256Size>;

/**
 * @brief Incremental SHA-256 (FIPS 180-4).
 *
 * Feed data with update() in any number of pieces, then call finish() once.
 * The result is the same as hashing all the pieces joined together.
 *
 * @note Not thread-safe; use one object per thread. It holds no secrets
 *       beyond its buffer, which HMAC wipes through its own copies.
 */
class Sha256 {
public:
    /** @brief Start a new hash (the FIPS 180-4 initial state). */
    Sha256() noexcept;

    /**
     * @brief Append bytes to the message.
     * @param data Bytes to add. May be empty.
     * @warning Calling it after finish() is a logic error; the result is
     *          meaningless.
     */
    void update(std::span<const std::uint8_t> data) noexcept;

    /**
     * @brief Pad the message and return its digest.
     * @return The 32-byte digest. Call it at most once per object.
     */
    [[nodiscard]] Sha256Digest finish() noexcept;

    /**
     * @brief Hash a whole message in one call.
     * @param data The message.
     * @return Its SHA-256 digest.
     */
    [[nodiscard]] static Sha256Digest hash(std::span<const std::uint8_t> data) noexcept;

private:
    /**
     * @brief Run the compression function on one block.
     * @param block Pointer to exactly 64 bytes.
     */
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_;               ///< H0..H7.
    std::array<std::uint8_t, kSha256BlockSize> buf_{};  ///< Partial block.
    std::size_t bufLen_ = 0;                            ///< Bytes used in buf_.
    std::uint64_t totalLen_ = 0;                        ///< Message length in bytes.
};

/**
 * @brief HMAC-SHA256 (RFC 2104) of a message.
 *
 * Keys longer than 64 bytes are hashed first, as the RFC requires (RFC 4231
 * test cases 6 and 7). Any key length is accepted, including empty.
 *
 * @param key The shared secret.
 * @param message The data to authenticate.
 * @return The full 32-byte MAC. The wire format sends its first 16 bytes
 *         (PLAN.md "Wire format"; RFC 4231 test case 5 covers truncation).
 */
[[nodiscard]] Sha256Digest hmacSha256(std::span<const std::uint8_t> key,
                                      std::span<const std::uint8_t> message) noexcept;

/**
 * @brief Compare two byte strings in time that doesn't depend on where they
 *        differ, so a forger can't learn a MAC byte by byte from timing.
 * @param a First string.
 * @param b Second string.
 * @return True if both have the same length and the same bytes. A length
 *         mismatch returns false right away; lengths aren't secret here.
 */
[[nodiscard]] bool constantTimeEqual(std::span<const std::uint8_t> a,
                                     std::span<const std::uint8_t> b) noexcept;

}  // namespace duckling::net
