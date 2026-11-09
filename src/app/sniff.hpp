/**
 * @file sniff.hpp
 * @brief `duckling sniff`: print every HID++ report from the receiver, labelled.
 *
 * Replaces `tools/hidpp-sniff.py`. The output format matches the existing
 * recordings in `hw-logs/`, so a capture can be saved and used as a test
 * fixture straight away:
 * @code
 *   7439.301 10 02 41 10 42 34 b0  <- mouse OFFLINE (slot 2, wpid B034)
 *   7432.025 ===== Mouse_switch_1 =====
 * @endcode
 * Labels and markers go through fixtureSafe(), so the fixture scanner only
 * sees the frame itself.
 */
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "hidpp/events.hpp"
#include "hidpp/report.hpp"

namespace duckling::app {

/**
 * @brief Turns reports into short human-readable labels.
 *
 * Stateful: it remembers each slot's device kind (from connection notices,
 * pairing info and pairing-info replies on the wire) and uses a
 * hidpp::EventParser to recognise CHANGE_HOST switch announcements, which
 * needs each device's CHANGE_HOST index.
 */
class SniffLabeler {
public:
    /**
     * @brief Register a device so its switch announcements are recognised.
     * @param slot        Device slot, 1..6.
     * @param kind        Device kind, for labels.
     * @param wpid        Wireless product id (see hidpp::EventParser).
     * @param changeHost  CHANGE_HOST feature index on that device.
     */
    void addDevice(std::uint8_t slot, hidpp::DeviceKind kind, std::uint16_t wpid, std::uint8_t changeHost);

    /**
     * @brief Remember a slot's kind without its CHANGE_HOST index (device not connected).
     * @param slot Device slot, 1..6.
     * @param kind Device kind.
     * @param wpid Wireless product id.
     */
    void setKind(std::uint8_t slot, hidpp::DeviceKind kind, std::uint16_t wpid) noexcept;

    /**
     * @brief Whether a slot's CHANGE_HOST index is known.
     * @param slot Device slot.
     * @return True after addDevice() for that slot (and no re-pairing since).
     */
    [[nodiscard]] bool knows(std::uint8_t slot) const noexcept;

    /**
     * @brief Label one report.
     * @param report Report as read.
     * @return Label text without the leading "<- ". Not yet passed through
     *         fixtureSafe(); formatSniffLine() does that.
     */
    [[nodiscard]] std::string label(const hidpp::Report& report);

private:
    /**
     * @brief Kind name of a slot, as last seen.
     * @param slot Device slot.
     * @return "keyboard", "mouse" or "device".
     */
    [[nodiscard]] const char* kindOf(std::uint8_t slot) const noexcept;

    hidpp::EventParser parser_;                                          ///< Recognises events.
    std::array<hidpp::DeviceKind, hidpp::kMaxDeviceIndex + 1> kinds_{};  ///< Kind per slot.
    std::array<std::uint16_t, hidpp::kMaxDeviceIndex + 1> wpids_{};     ///< wpid per slot.
    std::array<bool, hidpp::kMaxDeviceIndex + 1> known_{};               ///< CHANGE_HOST index known.
};

/**
 * @brief Make free text safe to put in a capture that is used as a fixture.
 *
 * The fixture scanner treats a token `10` or `11` as the start of a frame.
 * Every whitespace-separated token of exactly two hex digits gets a `#`
 * prefix, so "press 10 times" becomes "press #10 times". Non-printable
 * bytes become '?'.
 *
 * @param text Label or marker.
 * @return The safe text.
 */
[[nodiscard]] std::string fixtureSafe(std::string_view text);

/**
 * @brief Format one output line.
 * @param seconds Time since the sniffer started.
 * @param report  The report.
 * @param label   Label from SniffLabeler::label(), may be empty; it is
 *                passed through fixtureSafe().
 * @return E.g. `12.345 10 02 41 10 42 34 b0  <- mouse OFFLINE (slot 2, wpid B034)`.
 */
[[nodiscard]] std::string formatSniffLine(double seconds, const hidpp::Report& report, const std::string& label);

/** @brief How long after a device's online notice sniff asks for its CHANGE_HOST index. */
inline constexpr std::chrono::milliseconds kLearnDelay{3000};

/**
 * @brief Run `duckling sniff`.
 *
 * Prints every report until Ctrl+C, or for @p seconds. Lines typed on stdin
 * become `===== label =====` markers, timed when Enter was pressed.
 *
 * To label switch announcements, sniff asks each connected device for its
 * CHANGE_HOST index: before recording starts, and kLearnDelay after a
 * device connects (after the connect burst of Solaar or Options+, which is
 * what recordings usually want to see). Those requests are `#` lines, their
 * replies aren't printed, and reports that arrived meanwhile are printed
 * under a `#` line with the time window.
 *
 * @warning sniff's requests use duckling's software id. A `duckling switch`
 * or daemon asking the same device at the same moment could take sniff's
 * reply (issue #9); the delay makes that unlikely, not impossible.
 *
 * @param seconds How long to run; 0 = until interrupted.
 * @return 0, or 1 if no receiver could be opened or it was unplugged.
 */
int runSniff(int seconds);

}  // namespace duckling::app
