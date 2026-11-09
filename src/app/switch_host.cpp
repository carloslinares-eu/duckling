/**
 * @file switch_host.cpp
 * @brief Implementation of `duckling switch`.
 */
#include "app/switch_host.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app/cli_common.hpp"
#include "hidpp/features.hpp"
#include "hidpp/receiver_info.hpp"

namespace duckling::app {

namespace {

using Clock = std::chrono::steady_clock;

/**
 * @brief Whether the selector includes a kind.
 * @param selector Selector.
 * @param kind     Device kind.
 * @return True if a device of that kind should be switched.
 */
bool selects(DeviceSelector selector, hidpp::DeviceKind kind) {
    switch (selector) {
        case DeviceSelector::Keyboard:
            return kind == hidpp::DeviceKind::Keyboard;
        case DeviceSelector::Mouse:
            return kind == hidpp::DeviceKind::Mouse;
        case DeviceSelector::All:
            return kind == hidpp::DeviceKind::Keyboard || kind == hidpp::DeviceKind::Mouse;
    }
    return false;
}

/**
 * @brief ASCII case-insensitive equality (host names are ASCII in practice).
 * @param a First string.
 * @param b Second string.
 * @return True if equal ignoring case.
 */
bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

/**
 * @brief "ch2 (fedora)" for messages.
 * @param host  0-based host.
 * @param names HOSTS_INFO table, possibly empty.
 * @return The label.
 */
std::string channelLabel(std::uint8_t host, const std::vector<hidpp::HostName>& names) {
    std::string label = "ch" + std::to_string(host + 1);
    for (const auto& n : names) {
        if (n.host == host && n.paired && !n.name.empty()) label += " (" + printable(n.name) + ")";
    }
    return label;
}

/** @brief LinkWatcher on top of an open io::Receiver. */
class ReceiverLinkWatcher final : public LinkWatcher {
public:
    /**
     * @brief Watch an open receiver.
     * @param rx Receiver; must outlive the watcher.
     */
    explicit ReceiverLinkWatcher(io::Receiver& rx) : rx_(rx) {}

    /**
     * @brief Discard every report already read or queued.
     * @param slot Unused; all queued reports are dropped.
     */
    void arm(std::uint8_t /*slot*/) override {
        while (rx_.isOpen() && rx_.read(std::chrono::milliseconds{0})) {
        }
    }

    /**
     * @brief Read reports until @p slot's link-down notice or the timeout.
     * @param slot    Device slot.
     * @param timeout How long to wait.
     * @return True if the notice arrived.
     */
    bool waitForLinkDown(std::uint8_t slot, std::chrono::milliseconds timeout) override {
        const auto deadline = Clock::now() + timeout;
        while (rx_.isOpen()) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            if (left.count() <= 0) return false;
            const auto r = rx_.read(left);
            if (!r) continue;
            const auto c = hidpp::parseDeviceConnection(*r);
            if (c && c->deviceIndex == slot && !c->online) return true;
        }
        return false;
    }

private:
    io::Receiver& rx_;  ///< The receiver being read.
};

/**
 * @brief Switch one paired device.
 * @param rq      Receiver.
 * @param watcher Link-down watcher.
 * @param pairing The device, from the pairing table.
 * @param target  Where to.
 * @return The outcome.
 */
SwitchOutcome switchOne(hidpp::Requester& rq, LinkWatcher& watcher, const hidpp::PairingInfo& pairing,
                        const SwitchTarget& target) {
    SwitchOutcome out{pairing.slot, pairing.kind(), false, {}};
    const std::string who = kindName(pairing.kind());
    const std::uint8_t slot = pairing.slot;
    const auto fail = [&](const std::string& why) {
        out.message = who + ": " + why;
        return out;
    };

    if (!pairing.linkUp) {
        return fail("not connected to this computer (on another host, or off). "
                    "Run `duckling switch` on the computer it is connected to.");
    }
    if (auto version = hidpp::ping(rq, slot); !version) {
        return fail("not answering: " + hidpp::describe(version.failure()));
    }
    const auto features = hidpp::discover(rq, slot);
    if (!features) return fail("can't find CHANGE_HOST: " + hidpp::describe(features.failure()));
    const auto host = hidpp::getHostInfo(rq, slot, features->changeHost);
    if (!host) return fail("can't read the current host: " + hidpp::describe(host.failure()));

    std::vector<hidpp::HostName> names;
    if (features->hostsInfo != 0) {
        if (auto table = hidpp::getHostNames(rq, slot, features->hostsInfo)) names = *table;
    }

    std::uint8_t to = 0;
    if (target.channel != 0) {
        if (target.channel > host->count) {
            return fail("has only " + std::to_string(host->count) + " channels, no ch" +
                        std::to_string(target.channel));
        }
        to = static_cast<std::uint8_t>(target.channel - 1);
        // A device sent to a channel with no paired host is stranded until
        // its Easy-Switch button is pressed (issue #8).
        for (const auto& n : names) {
            if (n.host == to && !n.paired) {
                return fail("ch" + std::to_string(target.channel) + " has no host paired");
            }
        }
    } else {
        std::vector<std::uint8_t> matches;
        for (const auto& n : names) {
            if (n.paired && equalsIgnoreCase(n.name, target.hostName)) matches.push_back(n.host);
        }
        if (matches.size() != 1) {
            std::string known;
            for (const auto& n : names) {
                if (!known.empty()) known += ", ";
                known += channelLabel(n.host, names);
            }
            return fail((matches.empty() ? "no channel named '" : "more than one channel named '") +
                        printable(target.hostName) + "'" +
                        (known.empty() ? std::string(" (host names not available)") : " (" + known + ")") +
                        ". Use the channel number instead.");
        }
        to = matches.front();
    }

    const std::string from = channelLabel(host->current, names);
    const std::string dest = channelLabel(to, names);
    if (to == host->current) {
        out.ok = true;
        out.message = who + ": already on " + dest;
        return out;
    }

    // Only notices that arrive after the command count (issue #8).
    watcher.arm(slot);
    const auto ack = hidpp::setCurrentHost(rq, slot, features->changeHost, to);
    // No ack, or the receiver saying the device is gone: the device may have
    // left before answering. The OFFLINE notice decides (issues #6, #8).
    const bool unknown = !ack && (ack.failure().kind == hidpp::FailureKind::Timeout ||
                                  ack.failure().deviceUnreachable());
    if (!ack && !unknown) {
        return fail("switch refused: " + hidpp::describe(ack.failure()));
    }
    // C5: the ack comes before the drop, so the OFFLINE notice follows it.
    if (!watcher.waitForLinkDown(slot, kLinkDownTimeout)) {
        return fail(ack ? "acknowledged the switch to " + dest + " but is still connected here"
                        : "no answer to the switch, and still connected here");
    }
    out.ok = true;
    out.message = who + ": " + from + " -> " + dest + (ack ? "" : " (no ack, but it left)");
    return out;
}

}  // namespace

std::optional<DeviceSelector> parseDeviceSelector(std::string_view arg) {
    if (arg == "keyboard" || arg == "kbd") return DeviceSelector::Keyboard;
    if (arg == "mouse") return DeviceSelector::Mouse;
    if (arg == "all") return DeviceSelector::All;
    return std::nullopt;
}

std::optional<SwitchTarget> parseSwitchTarget(std::string_view arg) {
    if (arg.empty()) return std::nullopt;
    if (arg.size() == 1 && std::isdigit(static_cast<unsigned char>(arg[0])) != 0) {
        if (arg[0] == '0') return std::nullopt;
        return SwitchTarget{static_cast<std::uint8_t>(arg[0] - '0'), {}};
    }
    return SwitchTarget{0, std::string(arg)};
}

std::vector<SwitchOutcome> switchDevices(hidpp::Requester& rq, LinkWatcher& watcher, DeviceSelector selector,
                                         const SwitchTarget& target) {
    std::vector<SwitchOutcome> outcomes;
    bool anyDevice = false;
    bool unreadable = false;
    for (std::uint8_t slot = 1; slot <= hidpp::kMaxDeviceIndex; ++slot) {
        const auto pairing = hidpp::getPairingInfoConfirmed(rq, slot);
        if (!pairing) {
            if (!pairing.failure().unknownDevice()) {
                // Might not even be a selected device: warn, don't fail (issue #8).
                unreadable = true;
                outcomes.push_back({slot, hidpp::DeviceKind::Unknown, true,
                                    "warning: slot " + std::to_string(slot) + " couldn't be read: " +
                                        hidpp::describe(pairing.failure())});
            }
            continue;
        }
        if (selects(selector, pairing->kind())) {
            anyDevice = true;
            outcomes.push_back(switchOne(rq, watcher, *pairing, target));
        }
    }
    if (!anyDevice) {
        const char* what = selector == DeviceSelector::Keyboard ? "keyboard"
                           : selector == DeviceSelector::Mouse  ? "mouse"
                                                                : "keyboard or mouse";
        outcomes.push_back({0, hidpp::DeviceKind::Unknown, false,
                            std::string("no paired ") + what + " found on this receiver" +
                                (unreadable ? " (some slots couldn't be read)" : "")});
    }
    return outcomes;
}

int runSwitch(std::string_view device, std::string_view targetArg) {
    const auto selector = parseDeviceSelector(device);
    const auto target = parseSwitchTarget(targetArg);
    if (!selector || !target) {
        std::fprintf(stderr, "%s", kSwitchUsage);
        return 2;
    }
    auto rx = openReceiverOrExplain();
    if (!rx) return 1;
    ReceiverLinkWatcher watcher(*rx);
    bool ok = true;
    for (const auto& o : switchDevices(*rx, watcher, *selector, *target)) {
        std::printf("%s\n", o.message.c_str());
        ok = ok && o.ok;
    }
    return ok ? 0 : 1;
}

}  // namespace duckling::app
