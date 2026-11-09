/**
 * @file hex.cpp
 * @brief Implementation of the hex helpers.
 */
#include "hidpp/hex.hpp"

namespace duckling::hidpp {

namespace {

/// Hex digits for formatting.
constexpr char kDigits[] = "0123456789abcdef";

/**
 * @brief Value of one hex digit.
 * @param c Character to convert.
 * @return 0..15, or -1 if @p c is not a hex digit.
 */
int digitValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief Whether @p c separates tokens.
 * @param c Character to test.
 * @return True for space, tab, CR, LF and comma.
 */
bool isSeparator(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',';
}

}  // namespace

std::string toHex(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve(bytes.size() * 3);
    for (std::uint8_t b : bytes) {
        if (!out.empty()) out += ' ';
        out += kDigits[b >> 4];
        out += kDigits[b & 0x0F];
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> parseHex(std::string_view text) {
    std::vector<std::uint8_t> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (isSeparator(text[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && !isSeparator(text[end])) ++end;
        std::string_view token = text.substr(i, end - i);
        i = end;

        if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
            token.remove_prefix(2);
        }
        if (token.empty() || token.size() > 2) return std::nullopt;
        int value = 0;
        for (char c : token) {
            const int d = digitValue(c);
            if (d < 0) return std::nullopt;
            value = value * 16 + d;
        }
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

}  // namespace duckling::hidpp
