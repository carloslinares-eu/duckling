/**
 * @file report.cpp
 * @brief Implementation of duckling::hidpp::Report.
 */
#include "hidpp/report.hpp"

#include <algorithm>
#include <stdexcept>

namespace duckling::hidpp {

std::optional<std::size_t> frameLength(std::uint8_t id) noexcept {
    switch (id) {
        case static_cast<std::uint8_t>(ReportId::Short):
            return kShortLength;
        case static_cast<std::uint8_t>(ReportId::Long):
            return kLongLength;
        default:
            return std::nullopt;
    }
}

Report Report::make(ReportId id, std::uint8_t deviceIndex, std::uint8_t featureIndex,
                    std::uint8_t function, std::uint8_t swId,
                    std::span<const std::uint8_t> params) {
    if (function > 0x0F) {
        throw std::invalid_argument("HID++ function must be 0..15");
    }
    if (swId > 0x0F) {
        throw std::invalid_argument("HID++ software id must be 0..15");
    }
    const auto frame = frameLength(static_cast<std::uint8_t>(id));
    if (!frame) {
        throw std::invalid_argument("unknown HID++ report id");
    }
    const std::size_t length = *frame;
    if (params.size() > length - 4) {
        throw std::invalid_argument("too many parameters for HID++ report");
    }

    Report r;
    r.length_ = length;
    r.bytes_[0] = static_cast<std::uint8_t>(id);
    r.bytes_[1] = deviceIndex;
    r.bytes_[2] = featureIndex;
    r.bytes_[3] = static_cast<std::uint8_t>((function << 4) | swId);
    std::copy(params.begin(), params.end(), r.bytes_.begin() + 4);
    return r;
}

std::optional<Report> Report::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return std::nullopt;
    }
    const auto length = frameLength(bytes[0]);
    if (!length || bytes.size() < *length) {
        return std::nullopt;
    }
    Report r;
    r.length_ = *length;
    std::copy_n(bytes.begin(), *length, r.bytes_.begin());
    return r;
}

std::vector<std::uint8_t> Report::encode() const {
    return {bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(length_)};
}

std::span<const std::uint8_t> Report::bytes() const noexcept {
    return {bytes_.data(), length_};
}

ReportId Report::id() const noexcept { return static_cast<ReportId>(bytes_[0]); }

std::size_t Report::length() const noexcept { return length_; }

std::uint8_t Report::deviceIndex() const noexcept { return bytes_[1]; }

std::uint8_t Report::featureIndex() const noexcept { return bytes_[2]; }

std::uint8_t Report::functionByte() const noexcept { return bytes_[3]; }

std::uint8_t Report::function() const noexcept {
    return static_cast<std::uint8_t>(bytes_[3] >> 4);
}

std::uint8_t Report::swId() const noexcept {
    return static_cast<std::uint8_t>(bytes_[3] & 0x0F);
}

std::span<const std::uint8_t> Report::params() const noexcept {
    return {bytes_.data() + 4, length_ - 4};
}

std::uint8_t Report::param(std::size_t i) const noexcept {
    return i < length_ - 4 ? bytes_[4 + i] : std::uint8_t{0};
}

bool Report::isError() const noexcept {
    return bytes_[2] == kHidpp10ErrorSubId || bytes_[2] == kHidpp20ErrorFeature;
}

/**
 * @brief Byte-wise equality of two frames.
 * @param a First report.
 * @param b Second report.
 * @return True if both frames have the same length and bytes.
 */
bool operator==(const Report& a, const Report& b) noexcept {
    return std::ranges::equal(a.bytes(), b.bytes());
}

}  // namespace duckling::hidpp
