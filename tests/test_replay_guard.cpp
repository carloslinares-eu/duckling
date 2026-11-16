/**
 * @file test_replay_guard.cpp
 * @brief Tests for duckling::net::ReplayGuard: the timestamp window edges,
 *        replay and retransmit de-duplication, peer restarts, expiry and the
 *        memory caps.
 */
#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "net/hmac_sha256.hpp"
#include "net/message.hpp"
#include "net/replay_guard.hpp"

using namespace duckling::net;

namespace {

/** @brief A fixed "now": 2026-11-16 12:00:00 UTC, in unix ms. */
constexpr std::uint64_t kNow = 1'794'830'400'000;

}  // namespace

TEST_CASE("ReplayGuard: timestamp window edges") {
    ReplayGuard g;
    CHECK(g.check("carbon", 1, kNow, kNow) == ReplayVerdict::Accepted);
    CHECK(g.check("carbon", 2, kNow - kReplayWindowMs, kNow) == ReplayVerdict::Accepted);
    CHECK(g.check("carbon", 3, kNow + kReplayWindowMs, kNow) == ReplayVerdict::Accepted);
    CHECK(g.check("carbon", 4, kNow - kReplayWindowMs - 1, kNow) == ReplayVerdict::TooOld);
    CHECK(g.check("carbon", 5, kNow + kReplayWindowMs + 1, kNow) == ReplayVerdict::TooNew);
    // Extremes must not overflow or wrap.
    CHECK(g.check("carbon", 6, 0, kNow) == ReplayVerdict::TooOld);
    CHECK(g.check("carbon", 7, UINT64_MAX, kNow) == ReplayVerdict::TooNew);
    CHECK(g.check("carbon", 8, 0, 0) == ReplayVerdict::Accepted);
    CHECK(g.check("carbon", 9, UINT64_MAX, UINT64_MAX) == ReplayVerdict::Accepted);
}

TEST_CASE("ReplayGuard: rejected messages are not remembered") {
    ReplayGuard g;
    CHECK(g.check("carbon", 1, kNow - 60'000, kNow) == ReplayVerdict::TooOld);
    CHECK(g.hostCount() == 0);
    CHECK(g.check("carbon", 1, kNow + 60'000, kNow) == ReplayVerdict::TooNew);
    CHECK(g.hostCount() == 0);
}

TEST_CASE("ReplayGuard: a replay inside the window is a duplicate") {
    ReplayGuard g;
    REQUIRE(g.check("carbon", 7, kNow, kNow) == ReplayVerdict::Accepted);
    CHECK(g.check("carbon", 7, kNow, kNow + 1) == ReplayVerdict::Duplicate);
    CHECK(g.check("carbon", 7, kNow, kNow + kReplayWindowMs) == ReplayVerdict::Duplicate);
    // Once the copy's timestamp has left the window, the time check rejects it.
    CHECK(g.check("carbon", 7, kNow, kNow + kReplayWindowMs + 1) == ReplayVerdict::TooOld);
}

TEST_CASE("ReplayGuard: ARRIVED sent 3x, 100 ms apart, is accepted once") {
    ReplayGuard g;
    CHECK(g.check("fedora", 42, kNow, kNow + 3) == ReplayVerdict::Accepted);
    CHECK(g.check("fedora", 42, kNow, kNow + 103) == ReplayVerdict::Duplicate);
    CHECK(g.check("fedora", 42, kNow, kNow + 203) == ReplayVerdict::Duplicate);
    CHECK(g.check("fedora", 43, kNow + 300, kNow + 303) == ReplayVerdict::Accepted);
}

TEST_CASE("ReplayGuard: hosts are tracked separately") {
    ReplayGuard g;
    CHECK(g.check("carbon", 1, kNow, kNow) == ReplayVerdict::Accepted);
    CHECK(g.check("fedora", 1, kNow, kNow) == ReplayVerdict::Accepted);
    CHECK(g.check("DESKTOP-CARLOS", 1, kNow, kNow) == ReplayVerdict::Accepted);
    CHECK(g.hostCount() == 3);
    // Hostnames are compared byte for byte (net::parse() already limits them).
    CHECK(g.check("Carbon", 1, kNow, kNow) == ReplayVerdict::Accepted);
}

TEST_CASE("ReplayGuard: a restarted peer reusing seq numbers is accepted") {
    ReplayGuard g;
    REQUIRE(g.check("carbon", 0, kNow, kNow) == ReplayVerdict::Accepted);
    REQUIRE(g.check("carbon", 1, kNow + 2000, kNow + 2000) == ReplayVerdict::Accepted);
    // Restart 1 s later: seq starts at 0 again, with a new timestamp.
    CHECK(g.check("carbon", 0, kNow + 3000, kNow + 3000) == ReplayVerdict::Accepted);
    CHECK(g.check("carbon", 1, kNow + 5000, kNow + 5000) == ReplayVerdict::Accepted);
    // Out-of-order delivery inside the window is fine too.
    CHECK(g.check("carbon", 9, kNow + 4000, kNow + 5001) == ReplayVerdict::Accepted);
}

TEST_CASE("ReplayGuard: entries expire with the window") {
    ReplayGuard g;
    for (std::uint32_t i = 0; i < 5; ++i) {
        REQUIRE(g.check("carbon", i, kNow + i * 2000, kNow + i * 2000) == ReplayVerdict::Accepted);
    }
    // At kNow + 8000 the window starts at kNow + 3000: seq 0 and 1 are gone.
    REQUIRE(g.check("fedora", 0, kNow + 8000, kNow + 8000) == ReplayVerdict::Accepted);
    CHECK(g.entryCount("carbon") == 3);
    // A host that falls silent is forgotten completely.
    REQUIRE(g.check("fedora", 1, kNow + 60'000, kNow + 60'000) == ReplayVerdict::Accepted);
    CHECK(g.entryCount("carbon") == 0);
    CHECK(g.hostCount() == 1);
}

TEST_CASE("ReplayGuard: memory stays bounded") {
    SUBCASE("entries per host") {
        ReplayGuard g;
        for (std::uint32_t i = 0; i < 1000; ++i) {
            REQUIRE(g.check("carbon", i, kNow + i, kNow) == ReplayVerdict::Accepted);
        }
        CHECK(g.entryCount("carbon") == kReplayMaxEntriesPerHost);
        // The newest entries are the ones kept.
        CHECK(g.check("carbon", 999, kNow + 999, kNow) == ReplayVerdict::Duplicate);
    }
    SUBCASE("hosts") {
        ReplayGuard g;
        for (std::uint32_t i = 0; i < 100; ++i) {
            REQUIRE(g.check("host" + std::to_string(i), 1, kNow, kNow + i) == ReplayVerdict::Accepted);
        }
        CHECK(g.hostCount() == kReplayMaxHosts);
        // The most recently heard host is kept; the earliest ones were evicted.
        CHECK(g.entryCount("host99") == 1);
        CHECK(g.entryCount("host0") == 0);
    }
}

TEST_CASE("ReplayGuard: works on parsed messages end to end") {
    const std::string secret = "duckling-test-secret";
    const std::span<const std::uint8_t> key{reinterpret_cast<const std::uint8_t*>(secret.data()),
                                            secret.size()};
    Message m;
    m.type = MessageType::Arrived;
    m.timestampMs = kNow;
    m.seq = 17;
    m.hostname = "carbon";
    m.devices = {{0xB366, 3}};
    const auto frame = serialize(m, key);

    ReplayGuard g;
    for (std::uint64_t copy = 0; copy < 3; ++copy) {
        const auto parsed = parse(frame, key);
        REQUIRE(std::holds_alternative<Message>(parsed));
        const auto verdict = g.check(std::get<Message>(parsed), kNow + 100 * copy);
        CHECK(verdict == (copy == 0 ? ReplayVerdict::Accepted : ReplayVerdict::Duplicate));
    }
}

TEST_CASE("ReplayGuard: describe() covers every verdict") {
    for (auto v : {ReplayVerdict::Accepted, ReplayVerdict::TooOld, ReplayVerdict::TooNew,
                   ReplayVerdict::Duplicate}) {
        CHECK(describe(v) != "unknown");
        CHECK_FALSE(describe(v).empty());
    }
}

TEST_CASE("ReplayGuard: a retransmit with a fresh timestamp is NOT a duplicate") {
    // Pins the sender contract (PLAN.md "Wire format"): ARRIVED retransmits
    // must reuse the same bytes. Re-serializing with a new timestamp would
    // make the receiver act on every copy.
    ReplayGuard g;
    CHECK(g.check("fedora", 42, kNow, kNow) == ReplayVerdict::Accepted);
    CHECK(g.check("fedora", 42, kNow + 100, kNow + 100) == ReplayVerdict::Accepted);
}
