/**
 * @file status.hpp
 * @brief `duckling status`: receiver, paired devices, own channel, host names.
 *
 * Collecting and formatting are separate, so the tests can drive
 * collectStatus() with recorded replies and check formatStatus()'s text.
 * Live peers are added with the network layer (M2).
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "hidpp/features.hpp"
#include "hidpp/receiver_info.hpp"
#include "hidpp/requester.hpp"

namespace duckling::app {

/** @brief What `status` found out about one paired device. */
struct DeviceStatus {
    hidpp::PairingInfo pairing;                     ///< From the receiver; always set.
    std::string codename;                           ///< Receiver's short name ("MX MCHNCL"), may be empty.
    std::optional<hidpp::ProtocolVersion> version;  ///< Set if the device answered the ping.
    std::optional<hidpp::Failure> pingFailure;      ///< Why it didn't, if the link was up.
    std::string name;                               ///< DEVICE_NAME, e.g. "MX Mechanical".
    std::optional<hidpp::DeviceFeatures> features;  ///< Feature indexes, if found.
    std::optional<hidpp::HostInfo> host;            ///< Own channel: the device's current host.
    std::vector<hidpp::HostName> hostNames;         ///< HOSTS_INFO table, if read.
    std::vector<std::string> problems;              ///< Calls that failed, for the report.

    /**
     * @brief Whether the device is connected here and answering.
     * @return True if the ping succeeded.
     */
    [[nodiscard]] bool here() const noexcept { return version.has_value(); }
};

/** @brief Everything `status` prints. */
struct StatusReport {
    std::string receiver;               ///< Receiver description, e.g. its path.
    std::string thisHost;               ///< This computer's host name.
    std::vector<DeviceStatus> devices;  ///< Paired devices, by slot.
    std::vector<std::string> problems;  ///< Slots that couldn't be read.
};

/**
 * @brief Query the receiver and every paired device.
 *
 * For each slot 1..6: pairing info and codename from the receiver. Devices
 * whose link is up are pinged; if they answer, their features, name,
 * current host (= this host's channel on that device) and host-name table
 * are read. Devices on another host are not pinged: the receiver already
 * says their link is down (r1), and each ping would cost a timeout.
 *
 * A slot is only reported empty when two pairing reads both answer
 * "unknown device", because that error can't be tied to one slot
 * (hidpp/receiver_info.hpp, issue #7). Names are shown with control
 * characters replaced (they come from other computers).
 *
 * @param rq       The receiver.
 * @param receiver Description to show for the receiver.
 * @param thisHost This computer's host name.
 * @return The collected report.
 */
[[nodiscard]] StatusReport collectStatus(hidpp::Requester& rq, std::string receiver,
                                         std::string thisHost);

/**
 * @brief Render a report as text for the terminal.
 * @param report What collectStatus() found.
 * @return Multi-line text ending in a newline.
 */
[[nodiscard]] std::string formatStatus(const StatusReport& report);

/**
 * @brief This computer's host name, without a domain.
 * @return E.g. "carbon" (Linux `gethostname`) or "DESKTOP-CARLOS" (Windows
 *         `GetComputerNameA`). Empty if unknown.
 */
[[nodiscard]] std::string localHostName();

/**
 * @brief Run `duckling status`: open the first receiver that opens, collect, print.
 * @return 0 on success, 1 if no receiver could be opened or a slot
 *         couldn't be read (StatusReport::problems).
 */
int runStatus();

}  // namespace duckling::app
