/**
 * @file events.hpp
 * @brief Turn raw HID++ reports into the few events duckling acts on:
 *        device link up/down, and a device announcing a host switch.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include "hidpp/report.hpp"

namespace duckling::hidpp {

/** @brief HID++ 1.0 sub-id of the receiver's "device connection" notice (PLAN.md C2). */
inline constexpr std::uint8_t kDeviceConnectionSubId = 0x41;
/** @brief HID++ 1.0 sub-id of the receiver's "device unpaired" notice. */
inline constexpr std::uint8_t kDeviceUnpairedSubId = 0x40;
/** @brief Highest device index a Bolt receiver uses for paired devices. */
inline constexpr std::uint8_t kMaxDeviceIndex = 6;
/** @brief Number of Easy-Switch hosts (channels 1..3 = hosts 0..2). */
inline constexpr std::uint8_t kHostCount = 3;

/**
 * @brief Device kind, from the low nibble of a connection notice's status
 *        byte. Only the values duckling cares about are named.
 */
enum class DeviceKind : std::uint8_t {
    Unknown = 0x00,   ///< Kind not reported or not recognised.
    Keyboard = 0x01,  ///< e.g. MX Mechanical.
    Mouse = 0x02,     ///< e.g. MX Master 3S.
};

/**
 * @brief Map a raw kind nibble to DeviceKind.
 * @param raw Low nibble of the connection notice's status byte.
 * @return Keyboard, Mouse or Unknown.
 */
[[nodiscard]] DeviceKind toDeviceKind(std::uint8_t raw) noexcept;

/**
 * @brief Status bit set when the device's link is down.
 * @see PLAN.md C2: `10 01 41 10 41 66 b3` (0x41: bit 6 set) = keyboard
 *      OFFLINE, `10 01 41 10 01 66 b3` (0x01) = keyboard online.
 */
inline constexpr std::uint8_t kLinkDownBit = 0x40;
/**
 * @brief Status bits holding the device kind.
 * @see PLAN.md C2: low nibble 1 for the MX Mechanical, 2 for the MX Master 3S.
 */
inline constexpr std::uint8_t kKindMask = 0x0F;

/** @brief A decoded link status byte. */
struct LinkStatus {
    bool linkUp = false;       ///< Bit 6 clear.
    std::uint8_t rawKind = 0;  ///< Low nibble, see toDeviceKind().
};

/**
 * @brief Decode the status byte shared by the 0x41 connection notice and the
 *        Bolt pairing register (hidpp::getPairingInfo; `hw-logs/r1-…`).
 * @param status The status byte.
 * @return Link state and raw kind.
 */
[[nodiscard]] constexpr LinkStatus decodeLinkStatus(std::uint8_t status) noexcept {
    return LinkStatus{(status & kLinkDownBit) == 0, static_cast<std::uint8_t>(status & kKindMask)};
}

/**
 * @brief The receiver reports that a paired device's link went up or down.
 *
 * Short frame (PLAN.md C2), sent by the receiver for device `i`:
 * @code
 *   10 i 41 <proto> <status> <wpid lo> <wpid hi>
 *   status bit 6 = link down, low nibble = device kind
 *   10 01 41 10 41 66 b3   keyboard (B366) OFFLINE
 *   10 02 41 10 02 34 b0   mouse    (B034) online
 * @endcode
 *
 * @note It looks the same whether the device switched host, was powered
 *       off, or went out of range (C2). Only a HostChange event, when the
 *       device sends one, tells these apart.
 */
struct DeviceConnection {
    std::uint8_t deviceIndex = 0;  ///< Receiver slot, 1..6.
    bool online = false;           ///< True = link established.
    std::uint8_t rawKind = 0;      ///< Low nibble of status, as sent (see kind()).
    std::uint16_t wpid = 0;        ///< Wireless product id, e.g. 0xB366.

    /**
     * @brief Device kind derived from rawKind.
     * @return Keyboard, Mouse or Unknown.
     */
    [[nodiscard]] DeviceKind kind() const noexcept { return toDeviceKind(rawKind); }

    /** @brief Field-wise equality. @param o Other event. @return True if equal. */
    bool operator==(const DeviceConnection& o) const = default;
};

/**
 * @brief A device announces it's about to switch to another host
 *        (CHANGE_HOST 0x1814 event, sent before the link drops).
 *
 * Frame (PLAN.md C2), keyboard at index 1 with CHANGE_HOST at feature 9:
 * @code
 *   11 01 09 00 <from> <to> …    function 0, swId 0 (= event, not a reply)
 *   11 01 09 00 02 00 …          leaving host 2 (ch3) for host 0 (ch1)
 *   11 01 09 00 02 01 …          leaving host 2 (ch3) for host 1 (ch2)
 * @endcode
 *
 * Host numbers are 0-based (host 0 = Easy-Switch channel 1) and always
 * < kHostCount. This event is only seen from CHANGE_HOST V2 (MX Mechanical)
 * and only for button presses, not for software-triggered switches (C5).
 * The MX Master 3S (V1) never sends it.
 */
struct HostChange {
    std::uint8_t deviceIndex = 0;  ///< Receiver slot, 1..6.
    std::uint8_t fromHost = 0;     ///< Host being left, 0-based, < kHostCount.
    std::uint8_t toHost = 0;       ///< Target host, 0-based, < kHostCount.

    /** @brief Field-wise equality. @param o Other event. @return True if equal. */
    bool operator==(const HostChange& o) const = default;
};

/** @brief Any event duckling reacts to. */
using Event = std::variant<DeviceConnection, HostChange>;

/**
 * @brief Classifies incoming reports into Events.
 *
 * Feature indexes differ per device (CHANGE_HOST is 9 on the MX Mechanical
 * and 10 on the MX Master 3S), so the parser has to be told where CHANGE_HOST
 * is for each device before it can recognise HostChange events. That is
 * discovered at startup by hidpp::Features (task 1.4).
 *
 * The registration is tied to the device's wpid. If the receiver reports a
 * different device in that slot (a connection notice with another wpid, or
 * an unpair notice), the registration is dropped, so a newly paired device's
 * unrelated feature events can't be mistaken for host switches.
 *
 * Everything else (replies to anyone's requests, Solaar/Options+ traffic,
 * battery and button events, errors) yields no event.
 *
 * @warning HostChange is told apart from a getHostInfo reply only by swId 0.
 *          Requests must never be sent with swId 0; duckling uses
 *          kDucklingSwId.
 */
class EventParser {
public:
    /**
     * @brief Register where CHANGE_HOST lives on a device.
     * @param deviceIndex  Receiver slot, 1..6. Other values are ignored.
     * @param wpid         Wireless product id of the device in that slot.
     * @param featureIndex CHANGE_HOST feature index on that device. 0 (ROOT)
     *                     means unknown and disables HostChange for it.
     */
    void setChangeHostFeature(std::uint8_t deviceIndex, std::uint16_t wpid,
                              std::uint8_t featureIndex) noexcept;

    /**
     * @brief Forget a device's registration.
     * @param deviceIndex Receiver slot, 1..6. Other values are ignored.
     */
    void forgetDevice(std::uint8_t deviceIndex) noexcept;

    /**
     * @brief Classify one report.
     *
     * Non-const: connection and unpair notices update the slot registrations
     * (see the class description).
     *
     * @param report A report read from the receiver.
     * @return The event, or std::nullopt if the report isn't one duckling
     *         reacts to.
     */
    [[nodiscard]] std::optional<Event> parse(const Report& report) noexcept;

private:
    /** @brief What is known about one receiver slot. */
    struct Slot {
        std::uint16_t wpid = 0;        ///< Device the registration belongs to.
        std::uint8_t changeHost = 0;   ///< CHANGE_HOST feature index, 0 = unknown.
    };
    /** @brief Registrations, indexed by device slot (index 0 unused). */
    std::array<Slot, kMaxDeviceIndex + 1> slots_{};
};

/**
 * @brief Decode a receiver connection notice on its own (no parser state needed).
 * @param report A report read from the receiver.
 * @return The notice, or std::nullopt if @p report is anything else (only
 *         short 0x10 frames with sub-id 0x41 from a device slot qualify).
 */
[[nodiscard]] std::optional<DeviceConnection> parseDeviceConnection(const Report& report) noexcept;

}  // namespace duckling::hidpp
