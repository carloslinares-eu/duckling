/**
 * @file receiver_info.cpp
 * @brief Implementation of the Bolt receiver register reads.
 */
#include "hidpp/receiver_info.hpp"

#include <algorithm>
#include <stdexcept>

namespace duckling::hidpp {

namespace {

/** @brief Most characters a codename reply can hold (20 - 7 header bytes). */
constexpr std::size_t kMaxCodename = 13;

/**
 * @brief Read one sub-register of register 0x2B5.
 * @param rq    The receiver.
 * @param sub   Sub-register, e.g. 0x51.
 * @param extra Second parameter byte (0x01 for codenames).
 * @return The reply, checked to answer @p sub.
 */
Result<Report> readInfo(Requester& rq, std::uint8_t sub, std::uint8_t extra) {
    const std::uint8_t params[] = {sub, extra};
    // Short request; byte 3 is the register (0xB5), split across the nibbles.
    const Report req = Report::make(ReportId::Short, kReceiverIndex, kGetLongRegisterSubId,
                                    kReceiverInfoRegister >> 4, kReceiverInfoRegister & 0x0F, params);
    auto r = send(rq, req);
    if (!r) return r;
    if (r->param(0) != sub) return Failure{FailureKind::BadReply};
    return r;
}

/**
 * @brief Reject slot numbers outside 1..6.
 * @param slot Slot to check.
 * @throws std::invalid_argument if out of range.
 */
void checkSlot(std::uint8_t slot) {
    if (slot < 1 || slot > kMaxDeviceIndex) throw std::invalid_argument("device slot must be 1..6");
}

/**
 * @brief Whether a report is a register 0x2B5 reply for sub-register base + slot 1..6.
 * @param r    Report.
 * @param base kBoltPairingInfo or kBoltCodename.
 * @return True if so.
 */
bool isInfoReply(const Report& r, std::uint8_t base) noexcept {
    const std::uint8_t sub = r.param(0);
    return r.id() == ReportId::Long && r.deviceIndex() == kReceiverIndex &&
           r.featureIndex() == kGetLongRegisterSubId && r.functionByte() == kReceiverInfoRegister &&
           (sub & 0xF0) == base && (sub & 0x0F) >= 1 && (sub & 0x0F) <= kMaxDeviceIndex;
}

}  // namespace

std::optional<PairingInfo> parsePairingInfo(const Report& r) noexcept {
    if (!isInfoReply(r, kBoltPairingInfo)) return std::nullopt;
    const LinkStatus status = decodeLinkStatus(r.param(1));
    PairingInfo info;
    info.slot = static_cast<std::uint8_t>(r.param(0) & 0x0F);
    info.linkUp = status.linkUp;
    info.rawKind = status.rawKind;
    info.wpid = static_cast<std::uint16_t>(r.param(2) | (r.param(3) << 8));
    info.serial = (std::uint32_t{r.param(4)} << 24) | (std::uint32_t{r.param(5)} << 16) |
                  (std::uint32_t{r.param(6)} << 8) | std::uint32_t{r.param(7)};
    return info;
}

std::optional<Codename> parseCodename(const Report& r) {
    if (!isInfoReply(r, kBoltCodename)) return std::nullopt;
    Codename c{static_cast<std::uint8_t>(r.param(0) & 0x0F), {}};
    const std::size_t length = std::min<std::size_t>(r.param(2), kMaxCodename);
    for (std::size_t i = 0; i < length; ++i) c.name += static_cast<char>(r.param(3 + i));
    while (!c.name.empty() && c.name.back() == '\0') c.name.pop_back();
    return c;
}

Result<PairingInfo> getPairingInfo(Requester& rq, std::uint8_t slot) {
    checkSlot(slot);
    auto r = readInfo(rq, static_cast<std::uint8_t>(kBoltPairingInfo + slot), 0x00);
    if (!r) return r.failure();
    auto info = parsePairingInfo(*r);
    if (!info) return Failure{FailureKind::BadReply};
    return *info;
}

Result<PairingInfo> getPairingInfoConfirmed(Requester& rq, std::uint8_t slot) {
    auto first = getPairingInfo(rq, slot);
    if (first || !first.failure().unknownDevice()) return first;
    return getPairingInfo(rq, slot);
}

Result<std::string> getCodename(Requester& rq, std::uint8_t slot) {
    checkSlot(slot);
    auto r = readInfo(rq, static_cast<std::uint8_t>(kBoltCodename + slot), 0x01);
    if (!r) return r.failure();
    auto c = parseCodename(*r);
    if (!c) return Failure{FailureKind::BadReply};
    return c->name;
}

}  // namespace duckling::hidpp
