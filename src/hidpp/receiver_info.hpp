/**
 * @file receiver_info.hpp
 * @brief What the Bolt receiver itself knows about its paired devices.
 *
 * The receiver keeps this in long register 0x2B5 (HID++ 1.0 sub-id 0x83,
 * register 0xB5), so it's available while the devices are on another host.
 * Layout from Solaar (`receiver.py`, BoltReceiver) and checked on carbon
 * (`hw-logs/r1-…`):
 * @code
 *   pairing   10 ff 83 b5 5N 00 00 -> 11 ff 83 b5 5N <status> <wpid lo> <wpid hi> <serial x4> …
 *   codename  10 ff 83 b5 6N 01 00 -> 11 ff 83 b5 6N 01 <len> <name…>
 *   empty     -> 10 ff 8f 83 b5 08 00   (HID++ 1.0 error 0x08, unknown device)
 * @endcode
 * `status` has the 0x41 notice's layout: bit 6 = link down, low nibble = kind.
 *
 * @warning The error reply doesn't echo the sub-register, so an error can't
 *          be tied to one slot. Another program reading 0x2B5 at the same
 *          time (Options+ does, W1) can produce an error that looks like
 *          the answer to duckling's read. Callers that act on "unknown
 *          device" should read again to confirm (issue #7).
 */
#pragma once

#include <cstdint>
#include <string>

#include "hidpp/events.hpp"
#include "hidpp/features.hpp"
#include "hidpp/requester.hpp"

namespace duckling::hidpp {

/** @brief Register holding the Bolt pairing info (low byte of long register 0x2B5). */
inline constexpr std::uint8_t kReceiverInfoRegister = 0xB5;
/** @brief Sub-register base for a slot's pairing info (0x50 + slot). */
inline constexpr std::uint8_t kBoltPairingInfo = 0x50;
/** @brief Sub-register base for a slot's codename (0x60 + slot). */
inline constexpr std::uint8_t kBoltCodename = 0x60;

/** @brief A paired device, as the receiver remembers it. */
struct PairingInfo {
    std::uint8_t slot = 0;     ///< Device index, 1..6.
    bool linkUp = false;       ///< Whether the device is connected to this receiver now.
    std::uint8_t rawKind = 0;  ///< Low nibble of the status byte.
    std::uint16_t wpid = 0;    ///< Wireless product id, e.g. 0xB366 (MX Mechanical).
    std::uint32_t serial = 0;  ///< Device serial, e.g. 0x284C459C.

    /**
     * @brief The kind decoded from rawKind.
     * @return Keyboard, Mouse or Unknown.
     */
    [[nodiscard]] DeviceKind kind() const noexcept { return toDeviceKind(rawKind); }

    /** @brief Equality, for tests. */
    friend bool operator==(const PairingInfo&, const PairingInfo&) = default;
};

/**
 * @brief Read a slot's pairing info.
 * @param rq   The receiver.
 * @param slot Device slot, 1..6.
 * @return The info. An empty slot fails with Failure::unknownDevice() (r1).
 * @throws std::invalid_argument if @p slot is outside 1..6.
 */
[[nodiscard]] Result<PairingInfo> getPairingInfo(Requester& rq, std::uint8_t slot);

/**
 * @brief getPairingInfo(), but an "unknown device" answer is read twice.
 *
 * The receiver's error reply doesn't say which slot it answers, so another
 * program's empty-slot error can look like ours (issue #7). An empty slot
 * is only believed when two reads agree.
 *
 * @param rq   The receiver.
 * @param slot Device slot, 1..6.
 * @return The pairing info or the failure.
 * @throws std::invalid_argument if @p slot is outside 1..6.
 */
[[nodiscard]] Result<PairingInfo> getPairingInfoConfirmed(Requester& rq, std::uint8_t slot);

/**
 * @brief Read a slot's codename, a short name the device gave the receiver.
 * @param rq   The receiver.
 * @param slot Device slot, 1..6.
 * @return E.g. "MX MCHNCL" or "MX Master 3S" (r1).
 * @throws std::invalid_argument if @p slot is outside 1..6.
 */
[[nodiscard]] Result<std::string> getCodename(Requester& rq, std::uint8_t slot);

}  // namespace duckling::hidpp
