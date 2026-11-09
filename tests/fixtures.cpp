/**
 * @file fixtures.cpp
 * @brief Frame extraction from embedded hw-logs recordings.
 */
#include "fixtures.hpp"

#include <iterator>
#include <optional>

#include "hidpp/hex.hpp"
#include "hidpp/report.hpp"

namespace duckling::test {

namespace {

/**
 * @brief Parse a token that is exactly two hex digits.
 * @param token Token to parse.
 * @return The byte, or std::nullopt for anything else (e.g. `7956.509`, `>>`).
 */
std::optional<std::uint8_t> hexPair(std::string_view token) {
    if (token.size() != 2) return std::nullopt;
    const auto bytes = hidpp::parseHex(token);
    if (!bytes || bytes->size() != 1) return std::nullopt;
    return (*bytes)[0];
}

/**
 * @brief Split a line on spaces and tabs.
 * @param line Line to split.
 * @return Non-empty tokens, as views into @p line.
 */
std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        std::size_t end = i;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r') ++end;
        if (end > i) tokens.push_back(line.substr(i, end - i));
        i = end;
    }
    return tokens;
}

}  // namespace

FrameScan scanLog(std::string_view name, std::string_view text) {
    FrameScan scan;
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view line =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++lineNo;

        const auto tokens = tokenize(line);
        std::size_t t = 0;
        while (t < tokens.size()) {
            const auto first = hexPair(tokens[t]);
            const auto length = first ? hidpp::frameLength(*first) : std::nullopt;
            if (!length) {
                ++t;
                continue;
            }
            LoggedFrame frame{{}, std::string(name), lineNo};
            std::size_t k = t;
            while (k < tokens.size() && frame.bytes.size() < *length) {
                const auto b = hexPair(tokens[k]);
                if (!b) break;
                frame.bytes.push_back(*b);
                ++k;
            }
            if (frame.bytes.size() == *length) {
                scan.frames.push_back(std::move(frame));
            } else {
                scan.malformed.push_back(std::move(frame));
            }
            t = k;
        }
    }
    return scan;
}

FrameScan scanAllLogs() {
    FrameScan all;
    for (const auto& log : embeddedLogs()) {
        auto scan = scanLog(log.name, log.text);
        all.frames.insert(all.frames.end(), std::make_move_iterator(scan.frames.begin()),
                          std::make_move_iterator(scan.frames.end()));
        all.malformed.insert(all.malformed.end(), std::make_move_iterator(scan.malformed.begin()),
                             std::make_move_iterator(scan.malformed.end()));
    }
    return all;
}

}  // namespace duckling::test
