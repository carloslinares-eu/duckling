/**
 * @file features.cpp
 * @brief Implementation of the HID++ 2.0 feature calls.
 */
#include "hidpp/features.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <utility>

namespace duckling::hidpp {

namespace {

/** @brief Feature index of ROOT on every HID++ 2.0 device. */
constexpr std::uint8_t kRootIndex = 0x00;
/** @brief Byte the ping asks the device to echo back. */
constexpr std::uint8_t kPingEcho = 0xA5;
/** @brief HOSTS_INFO capability bit: the device can report host names. */
constexpr std::uint8_t kCanGetHostNames = 0x01;
/** @brief Name bytes per HOSTS_INFO getHostFriendlyName reply. */
constexpr std::size_t kHostNameChunk = 14;
/** @brief Name bytes per DEVICE_NAME getDeviceName reply. */
constexpr std::size_t kDeviceNameChunk = 16;
/** @brief Upper bound on name lengths, so a broken reply can't loop for long. */
constexpr std::size_t kMaxNameLength = 64;

/**
 * @brief Send a long HID++ 2.0 request and turn the outcome into a Result.
 * @param rq           Where to send it.
 * @param deviceIndex  Device slot.
 * @param featureIndex Feature index.
 * @param function     Function number.
 * @param params       Parameters.
 * @return The (non-error) reply, or the failure.
 */
Result<Report> call(Requester& rq, std::uint8_t deviceIndex, std::uint8_t featureIndex,
                    std::uint8_t function, std::span<const std::uint8_t> params = {}) {
    return send(rq, Report::make(ReportId::Long, deviceIndex, featureIndex, function,
                                 kDucklingSwId, params));
}

/**
 * @brief Name of a HID++ 2.0 error code.
 * @param code Error code.
 * @return Short description.
 */
const char* hidpp20Error(std::uint8_t code) {
    static constexpr std::array<const char*, 10> kNames = {
        "no error",       "unknown",          "invalid argument", "out of range",
        "hardware error", "internal error",   "invalid feature index",
        "invalid function", "busy",           "unsupported"};
    return code < kNames.size() ? kNames[code] : "unknown error";
}

/**
 * @brief Name of a HID++ 1.0 (receiver) error code.
 * @param code Error code.
 * @return Short description.
 */
const char* hidpp10Error(std::uint8_t code) {
    switch (code) {
        case 0x04:
            return "device not connected";
        case 0x08:
            return "unknown device";
        case 0x09:
            return "device not responding";
        default:
            return "receiver error";
    }
}

/**
 * @brief Drop trailing NUL bytes.
 * @param s String to trim in place.
 */
void trimNuls(std::string& s) {
    while (!s.empty() && s.back() == '\0') s.pop_back();
}

}  // namespace

Result<Report> send(Requester& rq, const Report& request) {
    const auto reply = rq.request(request);
    if (!reply) return Failure{FailureKind::Timeout};
    if (reply->featureIndex() == kHidpp10ErrorSubId) {
        // 10 i 8F <feat> <fn|sw> <code> 00
        return Failure{FailureKind::Hidpp10, reply->param(1)};
    }
    if (reply->featureIndex() == kHidpp20ErrorFeature) {
        // 11 i FF <feat> <fn|sw> <code> …
        return Failure{FailureKind::Hidpp20, reply->param(1)};
    }
    return *reply;
}

std::string describe(const Failure& failure) {
    char buf[96];
    switch (failure.kind) {
        case FailureKind::Timeout:
            return "timeout";
        case FailureKind::Unsupported:
            return "not supported by the device";
        case FailureKind::BadReply:
            return "unexpected reply";
        case FailureKind::Hidpp10:
            std::snprintf(buf, sizeof buf, "%s (HID++ 1.0 error 0x%02x)", hidpp10Error(failure.code),
                          failure.code);
            return buf;
        case FailureKind::Hidpp20:
            std::snprintf(buf, sizeof buf, "%s (HID++ 2.0 error 0x%02x)", hidpp20Error(failure.code),
                          failure.code);
            return buf;
    }
    return "unknown failure";
}

Result<ProtocolVersion> ping(Requester& rq, std::uint8_t deviceIndex) {
    const std::uint8_t params[] = {0x00, 0x00, kPingEcho};
    auto r = call(rq, deviceIndex, kRootIndex, 1, params);
    if (!r) return r.failure();
    if (r->param(2) != kPingEcho) return Failure{FailureKind::BadReply};
    return ProtocolVersion{r->param(0), r->param(1)};
}

Result<FeatureInfo> getFeature(Requester& rq, std::uint8_t deviceIndex, std::uint16_t featureId) {
    const std::uint8_t params[] = {static_cast<std::uint8_t>(featureId >> 8),
                                   static_cast<std::uint8_t>(featureId & 0xFF)};
    auto r = call(rq, deviceIndex, kRootIndex, 0, params);
    if (!r) return r.failure();
    // Index 0 means "not present", except for ROOT itself.
    if (r->param(0) == 0 && featureId != kFeatureRoot) return Failure{FailureKind::Unsupported};
    return FeatureInfo{r->param(0), r->param(1), r->param(2)};
}

Result<DeviceFeatures> discover(Requester& rq, std::uint8_t deviceIndex) {
    DeviceFeatures f;
    auto changeHost = getFeature(rq, deviceIndex, kFeatureChangeHost);
    if (!changeHost) return changeHost.failure();
    f.changeHost = changeHost->index;
    f.changeHostVersion = changeHost->version;

    // Optional features: only a missing feature is fine; other failures
    // (timeout, device gone) mean the answer is unknown.
    for (auto [id, slot] : {std::pair{kFeatureHostsInfo, &f.hostsInfo},
                            std::pair{kFeatureDeviceName, &f.deviceName}}) {
        auto info = getFeature(rq, deviceIndex, id);
        if (info) {
            *slot = info->index;
        } else if (info.failure().kind != FailureKind::Unsupported) {
            return info.failure();
        }
    }
    return f;
}

Result<HostInfo> getHostInfo(Requester& rq, std::uint8_t deviceIndex, std::uint8_t changeHostIndex) {
    auto r = call(rq, deviceIndex, changeHostIndex, 0);
    if (!r) return r.failure();
    const HostInfo info{r->param(0), r->param(1)};
    if (info.count == 0 || info.current >= info.count) return Failure{FailureKind::BadReply};
    return info;
}

Result<Ack> setCurrentHost(Requester& rq, std::uint8_t deviceIndex, std::uint8_t changeHostIndex,
                           std::uint8_t host) {
    const std::uint8_t params[] = {host};
    auto r = call(rq, deviceIndex, changeHostIndex, 1, params);
    if (!r) return r.failure();
    return Ack{};
}

Result<std::vector<HostName>> getHostNames(Requester& rq, std::uint8_t deviceIndex,
                                           std::uint8_t hostsInfoIndex) {
    auto info = call(rq, deviceIndex, hostsInfoIndex, 0);
    if (!info) return info.failure();
    // <caps> <?> <nbHost> <currHost>; W1 keyboard: 13 08 03 00.
    if ((info->param(0) & kCanGetHostNames) == 0) return Failure{FailureKind::Unsupported};
    const std::uint8_t hostCount = info->param(2);
    if (hostCount > kMaxHosts) return Failure{FailureKind::BadReply};

    std::vector<HostName> out;
    for (std::uint8_t host = 0; host < hostCount; ++host) {
        // <host> <status> <bus> <pages> <nameLen> <maxLen>; W1: 00 01 05 01 0e 18.
        const std::uint8_t hostParam[] = {host};
        auto desc = call(rq, deviceIndex, hostsInfoIndex, 1, hostParam);
        if (!desc) return desc.failure();
        if (desc->param(0) != host) return Failure{FailureKind::BadReply};

        HostName entry{host, desc->param(1) != 0, {}, false};
        if (!entry.paired) {
            out.push_back(std::move(entry));
            continue;
        }
        const std::size_t length = std::min<std::size_t>(desc->param(4), kMaxNameLength);
        entry.nameRead = true;
        while (entry.name.size() < length) {
            const auto offset = static_cast<std::uint8_t>(entry.name.size());
            const std::uint8_t nameParams[] = {host, offset};
            auto piece = call(rq, deviceIndex, hostsInfoIndex, 3, nameParams);
            if (!piece && piece.failure().kind == FailureKind::Hidpp20) {
                // The device refuses this channel's name: keep the others.
                entry.name.clear();
                entry.nameRead = false;
                break;
            }
            if (!piece) return piece.failure();
            if (piece->param(0) != host || piece->param(1) != offset) {
                return Failure{FailureKind::BadReply};
            }
            const std::size_t take = std::min(kHostNameChunk, length - entry.name.size());
            for (std::size_t i = 0; i < take; ++i) {
                entry.name += static_cast<char>(piece->param(2 + i));
            }
        }
        trimNuls(entry.name);
        out.push_back(std::move(entry));
    }
    return out;
}

Result<std::string> getDeviceName(Requester& rq, std::uint8_t deviceIndex,
                                  std::uint8_t deviceNameIndex) {
    auto count = call(rq, deviceIndex, deviceNameIndex, 0);
    if (!count) return count.failure();
    const std::size_t length = std::min<std::size_t>(count->param(0), kMaxNameLength);

    std::string name;
    while (name.size() < length) {
        const std::uint8_t params[] = {static_cast<std::uint8_t>(name.size())};
        auto piece = call(rq, deviceIndex, deviceNameIndex, 1, params);
        if (!piece) return piece.failure();
        const std::size_t take = std::min(kDeviceNameChunk, length - name.size());
        for (std::size_t i = 0; i < take; ++i) name += static_cast<char>(piece->param(i));
    }
    trimNuls(name);
    return name;
}

}  // namespace duckling::hidpp
