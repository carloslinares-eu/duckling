/**
 * @file cli_common.hpp
 * @brief Helpers shared by the CLI commands (`status`, `switch`).
 */
#pragma once

#include <optional>
#include <string>

#include "hidpp/events.hpp"
#include "io/receiver.hpp"

namespace duckling::app {

/**
 * @brief Name of a device kind for output.
 * @param kind Kind.
 * @return "keyboard", "mouse" or "device".
 */
[[nodiscard]] const char* kindName(hidpp::DeviceKind kind) noexcept;

/**
 * @brief Make a device-supplied name safe to print.
 *
 * Host names come from other computers (HOSTS_INFO), so control characters
 * and ESC sequences are replaced instead of being sent to the terminal
 * (issue #7).
 *
 * @param s Name as read.
 * @return The name with bytes outside 0x20..0x7E replaced by '?'.
 */
[[nodiscard]] std::string printable(const std::string& s);

/**
 * @brief printf into a std::string, with compiler format checking.
 * @param fmt printf format.
 * @param ... Arguments.
 * @return The formatted text (truncated at 511 characters).
 */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
[[nodiscard]] std::string formatText(const char* fmt, ...);

/**
 * @brief Open the first Bolt receiver that opens, or explain on stderr why none did.
 *
 * Tells "no receiver" apart from "found but can't open" and, for the
 * latter, lists the paths and the udev hint (Linux needs hidraw access).
 *
 * @return The open receiver (its paths() say which one), or std::nullopt.
 */
[[nodiscard]] std::optional<io::Receiver> openReceiverOrExplain();

}  // namespace duckling::app
