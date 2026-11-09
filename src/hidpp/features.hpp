/**
 * @file features.hpp
 * @brief HID++ 2.0 feature calls: ROOT, DEVICE_NAME, CHANGE_HOST, HOSTS_INFO.
 *
 * Every call goes through a hidpp::Requester (io::Receiver in the program,
 * a scripted fake in the tests) and returns a Result: the value or a Failure
 * that says why (timeout, device unreachable, feature missing, …).
 *
 * Byte layouts are taken from the hardware logs and from Solaar's
 * `logitech_receiver/hidpp20.py`. `S` is duckling's software id (0x1):
 * @code
 *   ROOT getFeature     11 i 00 0S <id hi> <id lo>   -> <index> <type> <version>
 *   ROOT ping           11 i 00 1S 00 00 <echo>      -> <major> <minor> <echo>
 *   DEVICE_NAME count   11 i N 0S                    -> <length>
 *   DEVICE_NAME name    11 i N 1S <offset>           -> up to 16 characters
 *   CHANGE_HOST info    11 i F 0S                    -> <nbHost> <currHost>
 *   CHANGE_HOST set     11 i F 1S <host>             -> ack (C5: 11 01 09 1a 00)
 *   HOSTS_INFO info     11 i H 0S                    -> <caps> <?> <nbHost> <currHost>
 *   HOSTS_INFO host     11 i H 1S <host>             -> <host> <paired> <bus> <pages> <nameLen> <maxLen>
 *   HOSTS_INFO name     11 i H 3S <host> <offset>    -> <host> <offset> <14 name bytes>
 * @endcode
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "hidpp/report.hpp"
#include "hidpp/requester.hpp"

namespace duckling::hidpp {

/** @brief Feature id of ROOT, always at feature index 0. */
inline constexpr std::uint16_t kFeatureRoot = 0x0000;
/** @brief Feature id of DEVICE_NAME (index 3 on both devices; C3, C4). */
inline constexpr std::uint16_t kFeatureDeviceName = 0x0005;
/** @brief Feature id of CHANGE_HOST (keyboard index 9, mouse 10; C1, C4). */
inline constexpr std::uint16_t kFeatureChangeHost = 0x1814;
/** @brief Feature id of HOSTS_INFO (keyboard index 0x0A, mouse 0x0B; W1, C3). */
inline constexpr std::uint16_t kFeatureHostsInfo = 0x1815;
/** @brief Most host channels getHostNames() accepts (both devices have 3). */
inline constexpr std::uint8_t kMaxHosts = 8;

/** @brief Why a feature call failed. */
enum class FailureKind : std::uint8_t {
    Timeout,      ///< No reply in time, or the receiver closed.
    Hidpp10,      ///< The receiver answered with a HID++ 1.0 error (0x8F).
    Hidpp20,      ///< The device answered with a HID++ 2.0 error (0xFF).
    Unsupported,  ///< The device doesn't have the feature or capability.
    BadReply,     ///< The reply doesn't make sense (wrong echo, out-of-range values).
};

/** @brief A failed feature call. */
struct Failure {
    FailureKind kind = FailureKind::Timeout;  ///< What went wrong.
    std::uint8_t code = 0;                     ///< Error code for Hidpp10 / Hidpp20.

    /**
     * @brief Whether the receiver couldn't reach the device.
     *
     * - **0x04** (connection request failed): the device is paired but on
     *   another host or off. Seen on carbon while both devices were on
     *   Windows (task 1.3 probe: `10 01 8f 00 11 04 00`).
     * - **0x09** (resource error): the Bolt receiver returns it for empty
     *   slots 3–6 (task 1.3 probe: `10 03 8f 00 11 09 00`), but it can also
     *   mean a paired device isn't answering. Solaar treats 0x04 and 0x09
     *   the same way (`base.py`), so this does too.
     *
     * Whether a slot is occupied comes from the receiver's pairing info, not
     * from this (issue #6).
     * @return True for HID++ 1.0 errors 0x04 and 0x09.
     */
    [[nodiscard]] bool deviceUnreachable() const noexcept {
        return kind == FailureKind::Hidpp10 && (code == 0x04 || code == 0x09);
    }

    /**
     * @brief Whether the receiver says it has no such device.
     * @return True for HID++ 1.0 error 0x08 (unknown device).
     */
    [[nodiscard]] bool unknownDevice() const noexcept {
        return kind == FailureKind::Hidpp10 && code == 0x08;
    }

    /** @brief Equality, for tests. */
    friend bool operator==(const Failure&, const Failure&) = default;
};

/**
 * @brief Describe a failure for logs and the CLI.
 * @param failure The failure.
 * @return E.g. "timeout", "device not connected (HID++ 1.0 error 0x04)".
 */
[[nodiscard]] std::string describe(const Failure& failure);

/**
 * @brief The value of a feature call, or why it failed.
 * @tparam T Value type.
 */
template <class T>
class Result {
public:
    /**
     * @brief A successful result.
     * @param value The value.
     */
    Result(T value) : v_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
    /**
     * @brief A failed result.
     * @param failure Why it failed.
     */
    Result(Failure failure) : v_(failure) {}  // NOLINT(google-explicit-constructor)

    /**
     * @brief Whether the call succeeded.
     * @return True if a value is held.
     */
    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(v_); }
    /**
     * @brief Same as ok().
     * @return True if a value is held.
     */
    explicit operator bool() const noexcept { return ok(); }

    /**
     * @brief The value.
     * @return The value.
     * @throws std::bad_variant_access if the call failed.
     */
    [[nodiscard]] const T& value() const { return std::get<T>(v_); }
    /**
     * @brief The value.
     * @return The value.
     * @throws std::bad_variant_access if the call failed.
     */
    const T& operator*() const { return value(); }
    /**
     * @brief Member access to the value.
     * @return Pointer to the value.
     * @throws std::bad_variant_access if the call failed.
     */
    const T* operator->() const { return &value(); }

    /**
     * @brief Why the call failed.
     * @return The failure.
     * @throws std::bad_variant_access if the call succeeded.
     */
    [[nodiscard]] const Failure& failure() const { return std::get<Failure>(v_); }

private:
    std::variant<T, Failure> v_;  ///< Value or failure.
};

/** @brief Value of calls that return nothing but an acknowledgement. */
struct Ack {
    /** @brief Equality, for tests. */
    friend bool operator==(const Ack&, const Ack&) = default;
};

/** @brief HID++ protocol version, from ROOT ping. */
struct ProtocolVersion {
    std::uint8_t major = 0;  ///< E.g. 4 for both devices (C4).
    std::uint8_t minor = 0;  ///< E.g. 5 for both devices (C4).
    /** @brief Equality, for tests. */
    friend bool operator==(const ProtocolVersion&, const ProtocolVersion&) = default;
};

/** @brief Where a feature lives on a device, from ROOT getFeature. */
struct FeatureInfo {
    std::uint8_t index = 0;    ///< Feature index to use in requests.
    std::uint8_t type = 0;     ///< Feature type flags (obsolete, hidden, …).
    std::uint8_t version = 0;  ///< Feature version (CHANGE_HOST: keyboard 2, mouse 1).
    /** @brief Equality, for tests. */
    friend bool operator==(const FeatureInfo&, const FeatureInfo&) = default;
};

/** @brief CHANGE_HOST getHostInfo: how many hosts, and which one is current. */
struct HostInfo {
    std::uint8_t count = 0;    ///< Number of host channels (3 on both devices).
    std::uint8_t current = 0;  ///< Current host, 0-based (Windows read 0 in C5).
    /** @brief Equality, for tests. */
    friend bool operator==(const HostInfo&, const HostInfo&) = default;
};

/** @brief One entry of a device's HOSTS_INFO table. */
struct HostName {
    std::uint8_t host = 0;  ///< Host channel, 0-based.
    bool paired = false;    ///< Whether a host is paired on this channel.
    std::string name;       ///< Host name, e.g. "DESKTOP-CARLOS". Empty if none or unread.
    bool nameRead = false;  ///< False if the device refused the name read, or the channel is unpaired.
    /** @brief Equality, for tests. */
    friend bool operator==(const HostName&, const HostName&) = default;
};

/** @brief Feature indexes duckling uses on one device. 0 = not present. */
struct DeviceFeatures {
    std::uint8_t changeHost = 0;         ///< CHANGE_HOST (0x1814) index. Always set on success.
    std::uint8_t changeHostVersion = 0;  ///< CHANGE_HOST version (V2 sends the switch event; C2).
    std::uint8_t hostsInfo = 0;          ///< HOSTS_INFO (0x1815) index, 0 if missing.
    std::uint8_t deviceName = 0;         ///< DEVICE_NAME (0x0005) index, 0 if missing.
    /** @brief Equality, for tests. */
    friend bool operator==(const DeviceFeatures&, const DeviceFeatures&) = default;
};

/**
 * @brief Ping a device slot (ROOT function 1).
 *
 * The receiver answers for a device that isn't connected: HID++ 1.0 error
 * 0x04 when it is paired but elsewhere, 0x09 when the slot is empty.
 *
 * @param rq          Where to send it.
 * @param deviceIndex Device slot, 1..6.
 * @return The protocol version, or a Failure (BadReply if the echo byte is wrong).
 */
[[nodiscard]] Result<ProtocolVersion> ping(Requester& rq, std::uint8_t deviceIndex);

/**
 * @brief Find a feature's index (ROOT function 0).
 * @param rq          Where to send it.
 * @param deviceIndex Device slot, 1..6.
 * @param featureId   Feature id, e.g. kFeatureChangeHost.
 * @return Index, type and version, or Unsupported if the device doesn't
 *         have the feature (index 0).
 */
[[nodiscard]] Result<FeatureInfo> getFeature(Requester& rq, std::uint8_t deviceIndex,
                                             std::uint16_t featureId);

/**
 * @brief Look up all features duckling uses on a device.
 * @param rq          Where to send it.
 * @param deviceIndex Device slot, 1..6.
 * @return The indexes. Fails with Unsupported if CHANGE_HOST is missing,
 *         because duckling can't do anything with that device. HOSTS_INFO
 *         and DEVICE_NAME are optional (index 0 when missing).
 */
[[nodiscard]] Result<DeviceFeatures> discover(Requester& rq, std::uint8_t deviceIndex);

/**
 * @brief CHANGE_HOST getHostInfo (function 0): host count and current host.
 * @param rq              Where to send it.
 * @param deviceIndex     Device slot, 1..6.
 * @param changeHostIndex CHANGE_HOST feature index on that device.
 * @return The host info. BadReply if the count is 0 or current >= count.
 * @see PLAN.md C5: Windows read `03 00` (3 hosts, current = ch1).
 */
[[nodiscard]] Result<HostInfo> getHostInfo(Requester& rq, std::uint8_t deviceIndex,
                                           std::uint8_t changeHostIndex);

/**
 * @brief CHANGE_HOST setCurrentHost (function 1): send the device to another host.
 *
 * The device acknowledges before it drops off this receiver (C5:
 * `11 01 09 1a 00` after 0.66 s). An ack means the switch is under way. A
 * Timeout means **unknown**: the ack can be lost while the device has
 * already left, so confirm with the receiver's 0x41 OFFLINE notice before
 * retrying.
 *
 * @param rq              Where to send it.
 * @param deviceIndex     Device slot, 1..6.
 * @param changeHostIndex CHANGE_HOST feature index on that device.
 * @param host            Target host, 0-based (0 = ch1).
 * @return Ack, or the failure.
 */
[[nodiscard]] Result<Ack> setCurrentHost(Requester& rq, std::uint8_t deviceIndex,
                                         std::uint8_t changeHostIndex, std::uint8_t host);

/**
 * @brief Read the host name table (HOSTS_INFO functions 0, 1 and 3).
 *
 * Names come in 14-byte pieces. Each reply starts with the host and offset
 * it answers, which are checked. Example (C3, mouse, HOSTS_INFO at 0x0B):
 * @code
 *   11 02 0b 3S 02 00 -> 11 02 0b 3S 02 00 "Logitech Bolt "
 *   11 02 0b 3S 02 0e -> 11 02 0b 3S 02 0e "receiver"
 * @endcode
 * A channel nobody has named shows the receiver's default name ("Logitech
 * Bolt receiver"). Names of unpaired channels aren't read. If the device
 * refuses one channel's name (a HID++ 2.0 error), that entry keeps an empty
 * name with `nameRead == false` and the others are still returned. A
 * timeout or receiver error fails the whole call: the device is gone. Solaar writes the current host's name when it reads the
 * table, and Options+ does too (W1), so names fill in over time.
 *
 * @param rq             Where to send it.
 * @param deviceIndex    Device slot, 1..6.
 * @param hostsInfoIndex HOSTS_INFO feature index on that device.
 * @return One entry per host channel, or Unsupported if the device can't
 *         report names (capability bit 0 clear), or BadReply for more than
 *         kMaxHosts channels.
 */
[[nodiscard]] Result<std::vector<HostName>> getHostNames(Requester& rq, std::uint8_t deviceIndex,
                                                         std::uint8_t hostsInfoIndex);

/**
 * @brief Read the device's marketing name (DEVICE_NAME functions 0 and 1).
 * @param rq              Where to send it.
 * @param deviceIndex     Device slot, 1..6.
 * @param deviceNameIndex DEVICE_NAME feature index on that device.
 * @return E.g. "MX Mechanical" (C4) or "MX Master 3S" (C3).
 */
[[nodiscard]] Result<std::string> getDeviceName(Requester& rq, std::uint8_t deviceIndex,
                                                std::uint8_t deviceNameIndex);

}  // namespace duckling::hidpp
