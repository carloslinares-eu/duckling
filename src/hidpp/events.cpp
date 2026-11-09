/**
 * @file events.cpp
 * @brief Implementation of the HID++ event parser.
 */
#include "hidpp/events.hpp"

namespace duckling::hidpp {

namespace {

/**
 * @brief Whether a device index refers to a paired device slot.
 * @param index Device index from a report.
 * @return True for 1..kMaxDeviceIndex.
 */
constexpr bool isDeviceSlot(std::uint8_t index) noexcept {
    return index >= 1 && index <= kMaxDeviceIndex;
}

}  // namespace

DeviceKind toDeviceKind(std::uint8_t raw) noexcept {
    switch (raw) {
        case 0x01:
            return DeviceKind::Keyboard;
        case 0x02:
            return DeviceKind::Mouse;
        default:
            return DeviceKind::Unknown;
    }
}

std::optional<DeviceConnection> parseDeviceConnection(const Report& report) noexcept {
    if (report.id() != ReportId::Short || report.featureIndex() != kDeviceConnectionSubId ||
        !isDeviceSlot(report.deviceIndex())) {
        return std::nullopt;
    }
    const LinkStatus status = decodeLinkStatus(report.param(0));
    DeviceConnection c;
    c.deviceIndex = report.deviceIndex();
    c.online = status.linkUp;
    c.rawKind = status.rawKind;
    c.wpid = static_cast<std::uint16_t>(report.param(1) | (report.param(2) << 8));
    return c;
}

void EventParser::setChangeHostFeature(std::uint8_t deviceIndex, std::uint16_t wpid,
                                       std::uint8_t featureIndex) noexcept {
    if (isDeviceSlot(deviceIndex)) slots_[deviceIndex] = Slot{wpid, featureIndex};
}

void EventParser::forgetDevice(std::uint8_t deviceIndex) noexcept {
    if (isDeviceSlot(deviceIndex)) slots_[deviceIndex] = Slot{};
}

std::optional<Event> EventParser::parse(const Report& report) noexcept {
    const std::uint8_t index = report.deviceIndex();

    if (auto c = parseDeviceConnection(report)) {
        // A different device now occupies this slot: its feature map is unknown.
        if (slots_[index].wpid != c->wpid) forgetDevice(index);
        return Event{*c};
    }
    if (report.id() == ReportId::Short && report.featureIndex() == kDeviceUnpairedSubId &&
        isDeviceSlot(index)) {
        forgetDevice(index);
        return std::nullopt;
    }

    if (!isDeviceSlot(index) || report.isError()) return std::nullopt;

    const std::uint8_t changeHost = slots_[index].changeHost;
    // Event = CHANGE_HOST, function 0, software id 0. Replies to getHostInfo
    // (also function 0) carry the requester's non-zero swId (C2: Solaar's
    // `11 01 09 02 03 02`, C5: `11 01 09 0a 03 00`), so swId 0 tells them apart.
    if (changeHost == 0 || report.id() != ReportId::Long || report.featureIndex() != changeHost ||
        report.function() != 0 || report.swId() != 0) {
        return std::nullopt;
    }
    const std::uint8_t from = report.param(0);
    const std::uint8_t to = report.param(1);
    if (from >= kHostCount || to >= kHostCount) return std::nullopt;
    return Event{HostChange{index, from, to}};
}

}  // namespace duckling::hidpp
