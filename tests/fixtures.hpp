/**
 * @file fixtures.hpp
 * @brief Real HID++ frames from the hardware-check recordings in `hw-logs/`,
 *        embedded into the test binary at configure time.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * @namespace duckling::test
 * @brief Test-only helpers (fixtures). Not linked into duckling itself.
 */
namespace duckling::test {

/**
 * @brief One recording file, embedded by cmake/EmbedFixtures.cmake.
 */
struct EmbeddedLog {
    std::string_view name;  ///< File name, e.g. `c3-2026-11-09-1542.txt`.
    std::string_view text;  ///< Full file contents.
};

/**
 * @brief All embedded `.txt` recordings from `hw-logs/`, sorted by file name.
 * @return The logs. Defined in the generated `hw_logs_data.cpp`.
 */
[[nodiscard]] const std::vector<EmbeddedLog>& embeddedLogs();

/**
 * @brief One HID++ frame found in a recording.
 */
struct LoggedFrame {
    std::vector<std::uint8_t> bytes;  ///< Frame bytes. Exactly 7 or 20 for valid frames.
    std::string file;                 ///< Log file name, for failure messages.
    int line = 0;                     ///< 1-based line number in the file.
};

/**
 * @brief Result of scanning recordings for frames.
 */
struct FrameScan {
    std::vector<LoggedFrame> frames;     ///< Complete frames.
    std::vector<LoggedFrame> malformed;  ///< Started with 0x10/0x11 but too short.
};

/**
 * @brief Extract every HID++ frame from one recording.
 *
 * Works with both recorder formats:
 * - `hidpp-sniff.py` / `duckling sniff`: `7956.509 11 01 09 00 …  <- note`
 * - `hidpp-win.py`: `16:11:40.478 >> 11 01 00 1a …`
 *
 * Every token on every line is examined. A two-digit hex token `10` or `11`
 * starts a frame of 7 or 20 bytes. If enough hex tokens follow, the frame is
 * taken and scanning goes on after it. If too few follow, it's recorded as
 * malformed, so truncated recordings get noticed. Other tokens
 * (timestamps, `>>`, labels, solaar output) are skipped.
 *
 * @param name File name stored in each LoggedFrame.
 * @param text Recording contents.
 * @return Frames and malformed candidates, in file order.
 */
[[nodiscard]] FrameScan scanLog(std::string_view name, std::string_view text);

/**
 * @brief scanLog() over every embedded recording.
 * @return Frames and malformed candidates from all logs, in file-name order.
 */
[[nodiscard]] FrameScan scanAllLogs();

}  // namespace duckling::test
