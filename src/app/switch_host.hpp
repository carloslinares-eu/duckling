/**
 * @file switch_host.hpp
 * @brief `duckling switch <mouse|keyboard|all> <channel|hostname>`.
 *
 * Sends CHANGE_HOST setCurrentHost to devices connected to this computer and
 * checks that they really left: the ack (C5) and then the receiver's 0x41
 * OFFLINE notice (C2).
 *
 * @warning In M1 there is no network, so the target can't be checked for
 *          being alive. A device sent to a host that is off stays offline
 *          until its Easy-Switch button is pressed (PLAN.md C3).
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hidpp/events.hpp"
#include "hidpp/requester.hpp"

namespace duckling::app {

/** @brief Which devices `switch` acts on. */
enum class DeviceSelector : std::uint8_t {
    Keyboard,  ///< Every paired keyboard.
    Mouse,     ///< Every paired mouse.
    All,       ///< Keyboards and mice.
};

/**
 * @brief Parse the device argument.
 * @param arg "keyboard", "kbd", "mouse" or "all".
 * @return The selector, or std::nullopt if unknown.
 */
[[nodiscard]] std::optional<DeviceSelector> parseDeviceSelector(std::string_view arg);

/** @brief Where to send the devices: a channel number or a host name. */
struct SwitchTarget {
    std::uint8_t channel = 0;  ///< 1-based channel, or 0 when hostName is used.
    std::string hostName;      ///< Name from the device's HOSTS_INFO table.
};

/**
 * @brief Parse the target argument.
 * @param arg A single digit 1..9 is a channel; anything else is a host name.
 * @return The target, or std::nullopt for an empty argument or channel 0.
 */
[[nodiscard]] std::optional<SwitchTarget> parseSwitchTarget(std::string_view arg);

/** @brief Waits for the receiver to report that a device's link went down. */
class LinkWatcher {
public:
    /** @brief Virtual destructor for a polymorphic base. */
    virtual ~LinkWatcher() = default;

    /**
     * @brief Forget every notice seen so far; call right before sending the switch.
     *
     * Without this, an older link-down notice for the slot (the device
     * napping after the receiver was opened) would count as proof of a
     * switch that didn't happen (issue #8).
     *
     * @param slot Device slot about to be switched.
     */
    virtual void arm(std::uint8_t slot) = 0;

    /**
     * @brief Wait for a link-down notice for @p slot that arrived after arm().
     *
     * The receiver's connection notice with bit 6 of the status set:
     * `10 <slot> 41 <?> <status> <wpid lo> <wpid hi>`, e.g. keyboard OFFLINE
     * `10 01 41 10 41 66 b3`.
     *
     * @param slot    Device slot.
     * @param timeout How long to wait.
     * @return True if the device went offline in time.
     * @see PLAN.md C2
     */
    [[nodiscard]] virtual bool waitForLinkDown(std::uint8_t slot, std::chrono::milliseconds timeout) = 0;

protected:
    /** @brief Only derived classes construct it. */
    LinkWatcher() = default;
    /** @brief Copyable by derived classes only. */
    LinkWatcher(const LinkWatcher&) = default;
    /**
     * @brief Copy assignment for derived classes.
     * @return *this.
     */
    LinkWatcher& operator=(const LinkWatcher&) = default;
};

/** @brief Result of switching one device. */
struct SwitchOutcome {
    std::uint8_t slot = 0;                                  ///< Device slot (0 if none was found).
    hidpp::DeviceKind kind = hidpp::DeviceKind::Unknown;    ///< Keyboard or mouse.
    bool ok = false;                                        ///< Switched, or already there.
    std::string message;                                    ///< One line for the user.
};

/** @brief Usage text of `duckling switch`, printed on a usage error. */
inline constexpr const char* kSwitchUsage =
    "usage: duckling switch <mouse|keyboard|all> <channel 1-3|hostname>\n";

/** @brief How long to wait for the OFFLINE notice after the ack (C5: ack after 0.66 s). */
inline constexpr std::chrono::milliseconds kLinkDownTimeout{3000};

/**
 * @brief Switch the selected devices that are connected here.
 *
 * Per device: find it in the receiver's pairing table, ping it, look up
 * CHANGE_HOST, read the current host, resolve the target (channel number,
 * or a unique case-insensitive match in the HOSTS_INFO table), send
 * setCurrentHost and wait for the OFFLINE notice.
 *
 * - Already on the target: ok, nothing sent.
 * - Ack but no OFFLINE within kLinkDownTimeout: failure.
 * - No ack (timeout, or the receiver answering "device unreachable") but
 *   OFFLINE: ok. The device can leave before answering.
 * - A numeric target whose HOSTS_INFO entry says "not paired": refused,
 *   because the device would be stranded there.
 * - Slots whose pairing info can't be read are reported as warnings
 *   (ok = true): they may not be selected devices at all (issue #8).
 *
 * @param rq       The receiver.
 * @param watcher  Reports link-down notices.
 * @param selector Devices to switch.
 * @param target   Where to.
 * @return One outcome per selected paired device; a single failed outcome
 *         (slot 0) if none is paired.
 */
[[nodiscard]] std::vector<SwitchOutcome> switchDevices(hidpp::Requester& rq, LinkWatcher& watcher,
                                                       DeviceSelector selector, const SwitchTarget& target);

/**
 * @brief Run `duckling switch`.
 * @param device Device argument (see parseDeviceSelector()). Empty for a
 *               missing argument (usage error).
 * @param target Target argument (see parseSwitchTarget()). Empty for a
 *               missing argument (usage error).
 * @return 0 if every selected device switched (or was already there),
 *         1 if any failed, 2 on a usage error.
 */
int runSwitch(std::string_view device, std::string_view target);

}  // namespace duckling::app
