/**
 * @file report.hpp
 * @brief HID++ report framing: the short (7-byte) and long (20-byte) reports
 *        exchanged with a Logi Bolt receiver.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/**
 * @namespace duckling::hidpp
 * @brief The Logitech HID++ protocol: report framing, features and events.
 *        This layer is pure: no I/O and no threads.
 */
namespace duckling::hidpp {

/**
 * @brief HID report IDs used by HID++ on the receiver's vendor interface
 *        (usage page 0xFF00).
 */
enum class ReportId : std::uint8_t {
    Short = 0x10,  ///< 7 bytes: id, device index, sub-id/feature, 4 bytes.
    Long = 0x11,   ///< 20 bytes: id, device index, sub-id/feature, 17 bytes.
};

/** @brief Total length of a short report (0x10), including the report ID. */
inline constexpr std::size_t kShortLength = 7;
/** @brief Total length of a long report (0x11), including the report ID. */
inline constexpr std::size_t kLongLength = 20;
/** @brief Device index that addresses the receiver itself. */
inline constexpr std::uint8_t kReceiverIndex = 0xFF;
/**
 * @brief Software id duckling puts in every request it sends.
 *
 * Replies echo the requester's swId, which tells duckling's replies apart
 * from other software on the same interface. Solaar cycles its swId through
 * 0x2..0xF (`logitech_receiver/base.py`, and all of 2..F appear in the C2/C3
 * logs); Options+ uses 0xA and 0xB (C4, W1). 0x1 is the only free id.
 * swId **0 is reserved for unsolicited
 * device events**: EventParser recognises a CHANGE_HOST switch announcement
 * by swId 0 (PLAN.md C2), so a request sent with swId 0 would have its reply
 * mistaken for a host switch.
 */
inline constexpr std::uint8_t kDucklingSwId = 0x01;
/** @brief Byte 2 value of a HID++ 1.0 "get long register" request and its reply. */
inline constexpr std::uint8_t kGetLongRegisterSubId = 0x83;
/** @brief Byte 2 value for a HID++ 1.0 error reply (from the receiver). */
inline constexpr std::uint8_t kHidpp10ErrorSubId = 0x8F;
/** @brief Byte 2 value for a HID++ 2.0 error reply (from a device). */
inline constexpr std::uint8_t kHidpp20ErrorFeature = 0xFF;

/**
 * @brief One HID++ report, either short or long.
 *
 * Byte layout. `params` starts at byte 4:
 * @code
 *   [0] report id   0x10 | 0x11
 *   [1] device idx  1..6 = paired device, 0xFF = receiver
 *   [2] feature idx (HID++ 2.0) or sub-id (HID++ 1.0, e.g. 0x41, 0x8F)
 *   [3] function << 4 | software id   (HID++ 1.0: register address)
 *   [4..] params    3 bytes (short) or 16 bytes (long)
 * @endcode
 *
 * Examples recorded in `hw-logs/`:
 * - `11 01 09 00 02 01 …`: keyboard (index 1), CHANGE_HOST (feature 9),
 *   function 0, swId 0 = event "leaving host 2 for host 1" (PLAN.md C2).
 * - `10 01 41 10 41 66 b3`: receiver notice, device 1 disconnected
 *   (PLAN.md C2).
 *
 * The class is a small value type. It always holds a valid frame of
 * kShortLength or kLongLength bytes.
 */
class Report {
public:
    /** @brief Number of parameter bytes in a short report. */
    static constexpr std::size_t kShortParams = kShortLength - 4;
    /** @brief Number of parameter bytes in a long report. */
    static constexpr std::size_t kLongParams = kLongLength - 4;

    /**
     * @brief Build a HID++ 2.0 request or event frame.
     *
     * @param id           Short or long frame.
     * @param deviceIndex  Target device (1..6) or kReceiverIndex.
     * @param featureIndex Feature index on the device (byte 2).
     * @param function     Function number, 0..15 (high nibble of byte 3).
     * @param swId         Software id, 0..15 (low nibble of byte 3). Replies
     *                     echo it, so callers can match their own requests.
     * @param params       Parameter bytes. Missing ones are zero-filled.
     * @return The encoded frame.
     * @throws std::invalid_argument if @p id is not Short/Long, if @p function
     *         or @p swId is above 15, or if @p params doesn't fit the frame.
     */
    [[nodiscard]] static Report make(ReportId id, std::uint8_t deviceIndex,
                                     std::uint8_t featureIndex, std::uint8_t function,
                                     std::uint8_t swId,
                                     std::span<const std::uint8_t> params = {});

    /**
     * @brief Parse raw bytes read from the HID interface.
     *
     * @param bytes Raw report starting with the report ID. Trailing bytes
     *              beyond the frame length are ignored (some HID APIs return
     *              fixed-size buffers).
     * @return The report, or std::nullopt if the ID is not 0x10/0x11 or the
     *         buffer is shorter than the frame.
     */
    [[nodiscard]] static std::optional<Report> decode(std::span<const std::uint8_t> bytes);

    /**
     * @brief Copy the frame out.
     * @return The frame bytes, exactly kShortLength or kLongLength long.
     */
    [[nodiscard]] std::vector<std::uint8_t> encode() const;

    /**
     * @brief View the frame without copying.
     * @return Read-only view of the frame bytes, valid while this Report lives.
     */
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

    /**
     * @brief Report ID (byte 0).
     * @return ReportId::Short or ReportId::Long.
     */
    [[nodiscard]] ReportId id() const noexcept;

    /**
     * @brief Frame length.
     * @return kShortLength (7) or kLongLength (20).
     */
    [[nodiscard]] std::size_t length() const noexcept;

    /**
     * @brief Device index (byte 1).
     * @return 1..6 for a paired device, kReceiverIndex for the receiver.
     */
    [[nodiscard]] std::uint8_t deviceIndex() const noexcept;

    /**
     * @brief Feature index or sub-id (byte 2).
     * @return HID++ 2.0 feature index, or HID++ 1.0 sub-id such as 0x41.
     */
    [[nodiscard]] std::uint8_t featureIndex() const noexcept;

    /**
     * @brief Byte 3 as-is.
     * @return function << 4 | swId (HID++ 2.0), or register address (HID++ 1.0).
     */
    [[nodiscard]] std::uint8_t functionByte() const noexcept;

    /**
     * @brief HID++ 2.0 function number.
     * @return High nibble of byte 3, 0..15.
     */
    [[nodiscard]] std::uint8_t function() const noexcept;

    /**
     * @brief HID++ 2.0 software id.
     * @return Low nibble of byte 3, 0..15. 0 means an unsolicited device event.
     */
    [[nodiscard]] std::uint8_t swId() const noexcept;

    /**
     * @brief Parameter bytes (from byte 4 on).
     * @return 3 bytes (short) or 16 bytes (long).
     */
    [[nodiscard]] std::span<const std::uint8_t> params() const noexcept;

    /**
     * @brief One parameter byte, bounds-checked.
     * @param i Parameter index, counted from byte 4.
     * @return The parameter byte, or 0 if @p i is past the end of the frame.
     */
    [[nodiscard]] std::uint8_t param(std::size_t i) const noexcept;

    /**
     * @brief Whether this is an error reply.
     * @return True for a HID++ 1.0 error (sub-id 0x8F) or a HID++ 2.0 error
     *         (feature index 0xFF).
     */
    [[nodiscard]] bool isError() const noexcept;

    /**
     * @brief Byte-wise equality of two frames.
     * @param a First report.
     * @param b Second report.
     * @return True if both frames have the same length and bytes.
     */
    friend bool operator==(const Report& a, const Report& b) noexcept;

private:
    Report() = default;

    std::array<std::uint8_t, kLongLength> bytes_{};  ///< Frame, zero-padded.
    std::size_t length_ = kShortLength;              ///< Valid bytes in bytes_.
};

/**
 * @brief Whether @p candidate answers @p request.
 *
 * A reply matches when:
 * - **Normal reply:** device index, feature index and byte 3
 *   (function | swId) are equal. The length may differ.
 * - **HID++ 2.0 error** from the device: `11 i FF <feat> <fn|sw> <err> …`,
 *   with feat and fn|sw equal to the request's.
 * - **HID++ 1.0 error** from the receiver, e.g. when the device is offline:
 *   `10 i 8F <feat> <fn|sw> <err> …`.
 *
 * - **HID++ 1.0 long register read** (`10 FF 83 <reg> <sub> …`): the
 *   sub-register in param 0 must match as well. Options+ reads
 *   `11 ff 83 b5 61 …` on the same interface (W1), which must not answer a
 *   read of `b5 62`.
 *
 * Header-only matching can't tell a late reply to an earlier, timed-out
 * request from the reply to a new one with the same header;
 * io::Receiver handles that.
 *
 * A HID++ 1.0 error to a long register read (`10 ff 8f 83 b5 08 00`)
 * doesn't carry the sub-register, so it matches any read of that register,
 * including another program's (issue #7).
 *
 * Example from PLAN.md C4: request `11 01 00 1a 00 00 aa`, reply
 * `11 01 00 1a 04 05 aa` (ping answered, protocol 4.5).
 *
 * @param request   The request that was sent.
 * @param candidate A report read afterwards.
 * @return True if @p candidate is the reply or an error for @p request.
 */
[[nodiscard]] bool isReplyTo(const Report& request, const Report& candidate) noexcept;

/**
 * @brief Whether a byte-2 value is a HID++ 1.0 register access sub-id.
 *
 * For sub-ids 0x80..0x83 (set/get short/long register), byte 3 is a register
 * address, not function | swId, so swId rules don't apply.
 *
 * @param subId Byte 2 of a report.
 * @return True for 0x80..0x83.
 */
[[nodiscard]] constexpr bool isRegisterAccess(std::uint8_t subId) noexcept {
    return subId >= 0x80 && subId <= kGetLongRegisterSubId;
}

/**
 * @brief Expected frame length for a report ID byte.
 * @param id First byte of a raw report.
 * @return kShortLength or kLongLength, or std::nullopt for any other ID.
 */
[[nodiscard]] std::optional<std::size_t> frameLength(std::uint8_t id) noexcept;

}  // namespace duckling::hidpp
