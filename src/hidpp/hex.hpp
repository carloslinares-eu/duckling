/**
 * @file hex.hpp
 * @brief Hex formatting and parsing for HID++ bytes. Used for logs,
 *        `duckling sniff`, and reading `hw-logs/` fixtures.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace duckling::hidpp {

/**
 * @brief Format bytes as lowercase hex pairs separated by spaces.
 * @param bytes Bytes to format.
 * @return For example `"10 01 41 10 41 66 b3"`, or `""` for no bytes. This
 *         is the same format the hw-logs recorders use.
 */
[[nodiscard]] std::string toHex(std::span<const std::uint8_t> bytes);

/**
 * @brief Parse a whitespace- or comma-separated list of hex bytes.
 *
 * Each token is one or two hex digits, with an optional `0x`/`0X` prefix.
 * Upper and lower case are both accepted. Examples: `"11 01 09 1A 02"` and
 * `"0x11,0x01,0x09"`.
 *
 * @param text Text to parse.
 * @return The bytes, or std::nullopt if any token isn't a valid byte.
 *         Empty input gives an empty vector.
 */
[[nodiscard]] std::optional<std::vector<std::uint8_t>> parseHex(std::string_view text);

}  // namespace duckling::hidpp
