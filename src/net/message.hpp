/**
 * @file message.hpp
 * @brief The UDP wire format between duckling hosts: HELLO and ARRIVED
 *        messages, signed with a truncated HMAC-SHA256.
 *
 * Layout (all integers big-endian; @see PLAN.md "Wire format"):
 * @code
 *  off size field
 *   0   4   magic "DUCK"
 *   4   1   version = 1
 *   5   1   type: 1 = HELLO, 2 = ARRIVED
 *   6   2   payload length = bytes from offset 8 up to the MAC
 *   8   8   timestamp, unix ms
 *  16   4   sequence number (per sender)
 *  20   1   hostname length n (1..63), then n bytes of UTF-8
 *   ..  1   device count k (0..6; ARRIVED needs >= 1),
 *           then k x {wpid u16, channel u8 (1..3)}
 *   .. 16   HMAC-SHA256 over every byte above, first 16 bytes
 * @endcode
 *
 * Every valid message has exactly one encoding: parse() rejects anything
 * serialize() wouldn't produce, so `serialize(parse(b)) == b` for any
 * accepted `b`. The timestamp window and replay check are not done here;
 * see net::ReplayGuard (task 2.3).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace duckling::net {

/** @brief UDP port duckling listens and broadcasts on. */
inline constexpr std::uint16_t kDefaultPort = 47474;
/** @brief Wire format version written and accepted. */
inline constexpr std::uint8_t kWireVersion = 1;
/** @brief Bytes of the HMAC-SHA256 sent on the wire (truncated). */
inline constexpr std::size_t kMacSize = 16;
/** @brief Fixed header: magic, version, type, payload length. */
inline constexpr std::size_t kHeaderSize = 8;
/** @brief Longest hostname, in bytes. */
inline constexpr std::size_t kMaxHostnameSize = 63;
/** @brief Most devices per message: a Bolt receiver pairs up to 6. */
inline constexpr std::size_t kMaxDevices = 6;
/**
 * @brief Highest Easy-Switch channel. Channels are 1-based on the wire,
 *        as in the CLI (`ch1`..`ch3`).
 * @see PLAN.md C6 (both devices report 3 hosts)
 */
inline constexpr std::uint8_t kMaxChannel = 3;
/** @brief Smallest valid datagram: 1-byte hostname, no devices. */
inline constexpr std::size_t kMinFrameSize = kHeaderSize + 8 + 4 + 1 + 1 + 1 + kMacSize;
/** @brief Largest valid datagram: 63-byte hostname, 6 devices. */
inline constexpr std::size_t kMaxFrameSize =
    kHeaderSize + 8 + 4 + 1 + kMaxHostnameSize + 1 + kMaxDevices * 3 + kMacSize;

/** @brief What a message announces. */
enum class MessageType : std::uint8_t {
    Hello = 1,    ///< Sent every 2 s: "I'm alive, these are my channels".
    Arrived = 2,  ///< A device just came online here; the first entry is it.
};

/** @brief One device and the channel the sender sits on for it. */
struct DeviceChannel {
    std::uint16_t wpid = 0;   ///< Wireless product id, e.g. 0xB366 (MX Mechanical).
    std::uint8_t channel = 0; ///< The sender's Easy-Switch channel, 1..3.

    /**
     * @brief Field-wise equality.
     * @return True if every field is equal.
     */
    bool operator==(const DeviceChannel&) const = default;
};

/** @brief A decoded, authenticated message. */
struct Message {
    MessageType type = MessageType::Hello;  ///< HELLO or ARRIVED.
    std::uint64_t timestampMs = 0;          ///< Sender's clock, unix ms.
    std::uint32_t seq = 0;                  ///< Per-sender sequence number.
    std::string hostname;                   ///< Sender, 1..63 bytes of UTF-8.
    /** @brief Per device: the sender's channel. ARRIVED lists the arriving
     *         device first. No wpid appears twice. */
    std::vector<DeviceChannel> devices;

    /**
     * @brief Field-wise equality.
     * @return True if every field is equal.
     */
    bool operator==(const Message&) const = default;
};

/** @brief Why a message is invalid or a datagram was rejected. */
enum class ParseError : std::uint8_t {
    TooShort,        ///< Fewer than kMinFrameSize bytes.
    TooLong,         ///< More than kMaxFrameSize bytes.
    BadMagic,        ///< Doesn't start with "DUCK".
    BadVersion,      ///< Version isn't kWireVersion.
    BadLength,       ///< The payload length field doesn't match the size.
    BadMac,          ///< Wrong secret, tampered, or no secret configured.
    BadType,         ///< Type isn't HELLO or ARRIVED.
    BadHostname,     ///< Empty, too long, invalid UTF-8 or control characters.
    BadDeviceCount,  ///< More than kMaxDevices, or ARRIVED with none.
    BadDevice,       ///< wpid 0, channel outside 1..kMaxChannel, or a repeated wpid.
    LengthMismatch,  ///< The fields don't add up to the payload length.
};

/**
 * @brief A short English description for logs.
 * @param error The error.
 * @return For example `"bad MAC (wrong secret or tampered)"`.
 */
[[nodiscard]] std::string_view describe(ParseError error) noexcept;

/** @brief parse() result: the message, or why it was rejected. */
using ParseResult = std::variant<Message, ParseError>;

/**
 * @brief Check a message against the format's limits.
 * @param message The message to check.
 * @return std::nullopt if it can be sent, otherwise the first problem
 *         (one of BadType, BadHostname, BadDeviceCount, BadDevice).
 */
[[nodiscard]] std::optional<ParseError> validate(const Message& message);

/**
 * @brief Encode and sign a message.
 * @param message The message; must pass validate().
 * @param key The shared secret.
 * @return The datagram, kMinFrameSize..kMaxFrameSize bytes.
 * @throws std::invalid_argument if the message is invalid or the key empty.
 */
[[nodiscard]] std::vector<std::uint8_t> serialize(const Message& message,
                                                  std::span<const std::uint8_t> key);

/**
 * @brief Check and decode a received datagram.
 *
 * Checks run cheapest first: size, magic, version, length field, then the
 * MAC (constant time) before any field is trusted, then the fields
 * themselves. Never throws and never reads outside @p datagram.
 *
 * @param datagram The bytes as received.
 * @param key The shared secret. An empty key rejects everything (BadMac).
 * @return The message, or the first reason it was rejected.
 */
[[nodiscard]] ParseResult parse(std::span<const std::uint8_t> datagram,
                                std::span<const std::uint8_t> key) noexcept;

}  // namespace duckling::net
