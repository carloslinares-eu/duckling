/**
 * @file test_switch.cpp
 * @brief Tests for `duckling switch` (task 1.6), with replies shaped like the
 *        Windows recordings (C4, C5, W1, task 1.4 probe).
 */
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app/switch_host.hpp"
#include "scripted_requester.hpp"

using namespace duckling;
using duckling::test::kZeros16;
using duckling::test::ScriptedRequester;

namespace {

/** @brief LinkWatcher that returns a fixed answer and records what it was asked. */
class FakeWatcher final : public app::LinkWatcher {
public:
    /**
     * @brief Create the fake.
     * @param goesDown What waitForLinkDown() returns.
     */
    explicit FakeWatcher(bool goesDown) : goesDown_(goesDown) {}

    /**
     * @brief Record the call and return the configured answer.
     * @param slot    Device slot.
     * @param timeout Ignored.
     * @return The configured answer.
     */
    bool waitForLinkDown(std::uint8_t slot, std::chrono::milliseconds /*timeout*/) override {
        CHECK(armed == slot);  // arm() came first
        asked.push_back(slot);
        return goesDown_;
    }

    /**
     * @brief Record that the switch was armed.
     * @param slot Device slot.
     */
    void arm(std::uint8_t slot) override { armed = slot; }

    std::vector<std::uint8_t> asked;  ///< Slots waited for.
    std::uint8_t armed = 0;           ///< Last slot armed.

private:
    bool goesDown_;  ///< Answer to give.
};

/**
 * @brief Pad a long frame's hex to 20 bytes with zeros.
 * @param hex Leading bytes.
 * @return The 20-byte frame.
 */
std::string longFrame(const std::string& hex) {
    const std::size_t bytes = (hex.size() + 1) / 3;
    return hex + kZeros16.substr(0, (20 - bytes) * 3);
}

/**
 * @brief Expect a host name read of 14 bytes or fewer.
 * @param rq   Script.
 * @param host Host 0..2 as a hex digit.
 * @param hex  Name bytes (14 of them, zero-padded).
 * @param len  Name length as hex.
 */
void expectName(ScriptedRequester& rq, char host, const std::string& hex, const std::string& len) {
    const std::string h = std::string("0") + host;
    rq.expect(longFrame("11 02 0b 11 " + h), longFrame("11 02 0b 11 " + h + " 01 05 01 " + len + " 18"))
        .expect(longFrame("11 02 0b 31 " + h), "11 02 0b 31 " + h + " 00 " + hex);
}

/**
 * @brief Script slot 1 (keyboard, not selected) and the mouse in slot 2 up to
 *        its host-name table: ch1 DESKTOP-CARLOS (current), ch2 fedora,
 *        ch3 carbon.
 * @param rq Script.
 */
void expectMouseOnWindows(ScriptedRequester& rq) {
    rq.expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 01 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00")
        .expect("10 ff 83 b5 52 00 00", "11 ff 83 b5 52 02 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")
        .expect(longFrame("11 02 00 11 00 00 a5"), longFrame("11 02 00 11 04 05 a5"))
        .expect(longFrame("11 02 00 01 18 14"), longFrame("11 02 00 01 0a 00 01"))
        .expect(longFrame("11 02 00 01 18 15"), longFrame("11 02 00 01 0b 00 02"))
        .expect(longFrame("11 02 00 01 00 05"), longFrame("11 02 00 01 03 00 00"))
        .expect(longFrame("11 02 0a 01"), longFrame("11 02 0a 01 03 00"))
        .expect(longFrame("11 02 0b 01"), longFrame("11 02 0b 01 13 08 03 00"));
    expectName(rq, '0', "44 45 53 4b 54 4f 50 2d 43 41 52 4c 4f 53", "0e");  // DESKTOP-CARLOS
    expectName(rq, '1', "66 65 64 6f 72 61 00 00 00 00 00 00 00 00", "06");  // fedora
    expectName(rq, '2', "63 61 72 62 6f 6e 00 00 00 00 00 00 00 00", "06");  // carbon
}

/**
 * @brief Script the empty slots 3..6 (two reads each, issue #7).
 * @param rq Script.
 */
void expectEmptySlots(ScriptedRequester& rq) {
    for (char slot = '3'; slot <= '6'; ++slot) {
        for (int i = 0; i < 2; ++i) {
            rq.expect(std::string("10 ff 83 b5 5") + slot + " 00 00", "10 ff 8f 83 b5 08 00");
        }
    }
}

/**
 * @brief Run a switch and return the single outcome.
 * @param rq      Script.
 * @param watcher Watcher.
 * @param target  Target argument.
 * @return The outcome.
 */
app::SwitchOutcome switchMouse(ScriptedRequester& rq, FakeWatcher& watcher, std::string_view target) {
    const auto outcomes = app::switchDevices(rq, watcher, app::DeviceSelector::Mouse, *app::parseSwitchTarget(target));
    CHECK(rq.done());
    REQUIRE(outcomes.size() == 1);
    return outcomes.front();
}

}  // namespace

TEST_CASE("argument parsing") {
    CHECK(app::parseDeviceSelector("mouse") == app::DeviceSelector::Mouse);
    CHECK(app::parseDeviceSelector("keyboard") == app::DeviceSelector::Keyboard);
    CHECK(app::parseDeviceSelector("kbd") == app::DeviceSelector::Keyboard);
    CHECK(app::parseDeviceSelector("all") == app::DeviceSelector::All);
    CHECK_FALSE(app::parseDeviceSelector("trackball").has_value());

    CHECK(app::parseSwitchTarget("3")->channel == 3);
    CHECK(app::parseSwitchTarget("carbon")->hostName == "carbon");
    CHECK(app::parseSwitchTarget("carbon")->channel == 0);
    CHECK(app::parseSwitchTarget("12")->hostName == "12");
    CHECK_FALSE(app::parseSwitchTarget("0").has_value());
    CHECK_FALSE(app::parseSwitchTarget("").has_value());
}

TEST_CASE("switch the mouse by channel number: ack, then OFFLINE") {
    ScriptedRequester rq;
    expectMouseOnWindows(rq);
    // C5 shape: setCurrentHost(ch3) acknowledged.
    rq.expect(longFrame("11 02 0a 11 02"), longFrame("11 02 0a 11 00"));
    expectEmptySlots(rq);
    FakeWatcher watcher(true);
    const auto o = switchMouse(rq, watcher, "3");
    CHECK(o.ok);
    CHECK(o.message == "mouse: ch1 (DESKTOP-CARLOS) -> ch3 (carbon)");
    CHECK(watcher.asked == std::vector<std::uint8_t>{2});
    CHECK(watcher.armed == 2);
}

TEST_CASE("switch by host name, ignoring case") {
    ScriptedRequester rq;
    expectMouseOnWindows(rq);
    rq.expect(longFrame("11 02 0a 11 01"), longFrame("11 02 0a 11 00"));
    expectEmptySlots(rq);
    FakeWatcher watcher(true);
    const auto o = switchMouse(rq, watcher, "FEDORA");
    CHECK(o.ok);
    CHECK(o.message == "mouse: ch1 (DESKTOP-CARLOS) -> ch2 (fedora)");
}

TEST_CASE("already on the target: nothing is sent") {
    ScriptedRequester rq;
    expectMouseOnWindows(rq);
    expectEmptySlots(rq);
    FakeWatcher watcher(true);
    const auto o = switchMouse(rq, watcher, "desktop-carlos");
    CHECK(o.ok);
    CHECK(o.message == "mouse: already on ch1 (DESKTOP-CARLOS)");
    CHECK(watcher.asked.empty());
}

TEST_CASE("unknown host name and out-of-range channel are refused") {
    ScriptedRequester rq;
    expectMouseOnWindows(rq);
    expectEmptySlots(rq);
    FakeWatcher watcher(true);
    const auto o = switchMouse(rq, watcher, "laptop");
    CHECK_FALSE(o.ok);
    CHECK(o.message == "mouse: no channel named 'laptop' (ch1 (DESKTOP-CARLOS), ch2 (fedora), ch3 (carbon)). "
                       "Use the channel number instead.");

    ScriptedRequester rq4;
    expectMouseOnWindows(rq4);
    expectEmptySlots(rq4);
    const auto o4 = switchMouse(rq4, watcher, "4");
    CHECK_FALSE(o4.ok);
    CHECK(o4.message == "mouse: has only 3 channels, no ch4");
    CHECK(watcher.asked.empty());
}

TEST_CASE("a lost ack still counts if the device leaves; an ack without leaving doesn't") {
    ScriptedRequester rq;
    expectMouseOnWindows(rq);
    rq.expect(longFrame("11 02 0a 11 02"), "");  // no ack
    expectEmptySlots(rq);
    FakeWatcher gone(true);
    const auto o = switchMouse(rq, gone, "3");
    CHECK(o.ok);
    CHECK(o.message == "mouse: ch1 (DESKTOP-CARLOS) -> ch3 (carbon) (no ack, but it left)");

    ScriptedRequester rq2;
    expectMouseOnWindows(rq2);
    rq2.expect(longFrame("11 02 0a 11 02"), longFrame("11 02 0a 11 00"));
    expectEmptySlots(rq2);
    FakeWatcher stays(false);
    const auto o2 = switchMouse(rq2, stays, "3");
    CHECK_FALSE(o2.ok);
    CHECK(o2.message == "mouse: acknowledged the switch to ch3 (carbon) but is still connected here");

    // The receiver answers the switch with "device not connected": it left first.
    ScriptedRequester rq3;
    expectMouseOnWindows(rq3);
    rq3.expect(longFrame("11 02 0a 11 02"), "10 02 8f 0a 11 04 00");
    expectEmptySlots(rq3);
    FakeWatcher gone3(true);
    CHECK(switchMouse(rq3, gone3, "3").ok);

    // A real refusal from the device (HID++ 2.0 invalid argument).
    ScriptedRequester rq4;
    expectMouseOnWindows(rq4);
    rq4.expect(longFrame("11 02 0a 11 02"), longFrame("11 02 ff 0a 11 02"));
    expectEmptySlots(rq4);
    FakeWatcher gone4(true);
    const auto o4 = switchMouse(rq4, gone4, "3");
    CHECK_FALSE(o4.ok);
    CHECK(o4.message == "mouse: switch refused: invalid argument (HID++ 2.0 error 0x02)");
    CHECK(gone4.asked.empty());
}

TEST_CASE("a numeric target with no paired host is refused") {
    ScriptedRequester rq;
    rq.expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 01 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00")
        .expect("10 ff 83 b5 52 00 00", "11 ff 83 b5 52 02 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")
        .expect(longFrame("11 02 00 11 00 00 a5"), longFrame("11 02 00 11 04 05 a5"))
        .expect(longFrame("11 02 00 01 18 14"), longFrame("11 02 00 01 0a 00 01"))
        .expect(longFrame("11 02 00 01 18 15"), longFrame("11 02 00 01 0b 00 02"))
        .expect(longFrame("11 02 00 01 00 05"), longFrame("11 02 00 01 03 00 00"))
        .expect(longFrame("11 02 0a 01"), longFrame("11 02 0a 01 03 00"))
        .expect(longFrame("11 02 0b 01"), longFrame("11 02 0b 01 13 08 03 00"));
    expectName(rq, '0', "44 45 53 4b 54 4f 50 2d 43 41 52 4c 4f 53", "0e");
    rq.expect(longFrame("11 02 0b 11 01"), longFrame("11 02 0b 11 01 00 00 00 00 18"));  // ch2 unpaired
    expectName(rq, '2', "63 61 72 62 6f 6e 00 00 00 00 00 00 00 00", "06");
    expectEmptySlots(rq);
    FakeWatcher watcher(true);
    const auto o = switchMouse(rq, watcher, "2");
    CHECK_FALSE(o.ok);
    CHECK(o.message == "mouse: ch2 has no host paired");
    CHECK(watcher.asked.empty());
}

TEST_CASE("an unreadable unrelated slot is only a warning") {
    ScriptedRequester rq;
    rq.expect("10 ff 83 b5 51 00 00", "");  // keyboard slot times out (Solaar busy)
    // rest of expectMouseOnWindows without slot 1
    rq.expect("10 ff 83 b5 52 00 00", "11 ff 83 b5 52 02 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00")
        .expect(longFrame("11 02 00 11 00 00 a5"), longFrame("11 02 00 11 04 05 a5"))
        .expect(longFrame("11 02 00 01 18 14"), longFrame("11 02 00 01 0a 00 01"))
        .expect(longFrame("11 02 00 01 18 15"), longFrame("11 02 00 01 0b 00 02"))
        .expect(longFrame("11 02 00 01 00 05"), longFrame("11 02 00 01 03 00 00"))
        .expect(longFrame("11 02 0a 01"), longFrame("11 02 0a 01 03 00"))
        .expect(longFrame("11 02 0b 01"), longFrame("11 02 0b 01 13 08 03 00"));
    expectName(rq, '0', "44 45 53 4b 54 4f 50 2d 43 41 52 4c 4f 53", "0e");
    expectName(rq, '1', "66 65 64 6f 72 61 00 00 00 00 00 00 00 00", "06");
    expectName(rq, '2', "63 61 72 62 6f 6e 00 00 00 00 00 00 00 00", "06");
    rq.expect(longFrame("11 02 0a 11 02"), longFrame("11 02 0a 11 00"));
    expectEmptySlots(rq);
    FakeWatcher watcher(true);
    const auto outcomes = app::switchDevices(rq, watcher, app::DeviceSelector::Mouse, *app::parseSwitchTarget("3"));
    CHECK(rq.done());
    REQUIRE(outcomes.size() == 2);
    CHECK(outcomes[0].ok);
    CHECK(outcomes[0].message == "warning: slot 1 couldn't be read: timeout");
    CHECK(outcomes[1].ok);
    CHECK(outcomes[1].message == "mouse: ch1 (DESKTOP-CARLOS) -> ch3 (carbon)");
}

TEST_CASE("devices that aren't here, and missing devices") {
    ScriptedRequester rq;
    // r1: both devices are on Windows; run on carbon.
    rq.expect("10 ff 83 b5 51 00 00", "11 ff 83 b5 51 41 66 b3 28 4c 45 9c 01 00 01 14 01 00 00 00")
        .expect("10 ff 83 b5 52 00 00", "11 ff 83 b5 52 42 34 b0 e5 76 cd 4a 01 00 02 0a 01 00 00 00");
    expectEmptySlots(rq);
    FakeWatcher watcher(true);
    const auto all = app::switchDevices(rq, watcher, app::DeviceSelector::All, *app::parseSwitchTarget("3"));
    CHECK(rq.done());
    REQUIRE(all.size() == 2);
    CHECK_FALSE(all[0].ok);
    CHECK(all[0].message.starts_with("keyboard: not connected to this computer"));
    CHECK(all[1].message.starts_with("mouse: not connected to this computer"));

    ScriptedRequester empty;
    for (char slot = '1'; slot <= '6'; ++slot) {
        for (int i = 0; i < 2; ++i) {
            empty.expect(std::string("10 ff 83 b5 5") + slot + " 00 00", "10 ff 8f 83 b5 08 00");
        }
    }
    const auto none = app::switchDevices(empty, watcher, app::DeviceSelector::Mouse, *app::parseSwitchTarget("1"));
    REQUIRE(none.size() == 1);
    CHECK_FALSE(none[0].ok);
    CHECK(none[0].message == "no paired mouse found on this receiver");
}
