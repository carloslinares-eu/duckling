/**
 * @file replay_guard.hpp
 * @brief Freshness and replay check for authenticated messages: the
 *        timestamp window and per-host duplicate detection.
 *
 * Runs after net::parse() has verified the MAC, so every field it sees came
 * from a host that holds the shared secret. @see PLAN.md "Wire format":
 * a message is rejected if its timestamp is off by more than 5 s or its
 * (host, seq) was already seen.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "net/message.hpp"

namespace duckling::net {

/**
 * @brief How far a message timestamp may be from the local clock, either
 *        way, in ms. Hosts keep time with NTP.
 * @see PLAN.md "Wire format" (timestamp off by more than 5 s → rejected)
 */
inline constexpr std::uint64_t kReplayWindowMs = 5000;
/** @brief Most hosts remembered at once; the least recently heard is
 *         forgotten first. A real setup has 2–3. */
inline constexpr std::size_t kReplayMaxHosts = 16;
/**
 * @brief Most remembered messages per host. A HELLO every 2 s plus a few
 *        ARRIVED bursts fill about 10 slots of a 10 s span; the oldest entry
 *        is forgotten first.
 */
inline constexpr std::size_t kReplayMaxEntriesPerHost = 64;

/** @brief ReplayGuard::check() verdict. */
enum class ReplayVerdict : std::uint8_t {
    Accepted,   ///< Fresh and not seen before; now remembered.
    TooOld,     ///< Timestamp more than kReplayWindowMs behind the local clock.
    TooNew,     ///< Timestamp more than kReplayWindowMs ahead of the local clock.
    Duplicate,  ///< Same host, seq and timestamp already accepted.
};

/**
 * @brief A short English description for logs.
 * @param verdict The verdict.
 * @return For example `"duplicate (replay or retransmit)"`.
 */
[[nodiscard]] std::string_view describe(ReplayVerdict verdict) noexcept;

/**
 * @brief Remembers recently accepted messages and rejects stale, future
 *        and repeated ones.
 *
 * A message is identified by (hostname, seq, timestamp). A replay is a
 * byte-for-byte copy, because the MAC covers every field, so it matches
 * all three. ARRIVED retransmits (sent 3×, 100 ms apart) must be
 * byte-identical copies too (serialize once, send the same buffer 3 times;
 * @see PLAN.md "Wire format"), so this also de-duplicates them. A retransmit
 * re-serialized with a fresh timestamp would be accepted again. A peer that restarts
 * and starts its seq again from the beginning sends new timestamps, so its
 * messages aren't mistaken for replays.
 *
 * An entry is only kept while its timestamp is inside the window. After
 * that, the timestamp check rejects a copy by itself. Memory is bounded by
 * kReplayMaxHosts × kReplayMaxEntriesPerHost.
 *
 * @warning If the local clock jumps back by more than the window, entries
 *          that were already forgotten become acceptable again for a short
 *          time. The same applies to entries evicted by the caps (a host
 *          sending more than kReplayMaxEntriesPerHost messages within the
 *          window, or more than kReplayMaxHosts hostnames). Either way, only
 *          a host holding the secret could have produced the messages.
 * @note Not thread-safe. The net worker thread owns it (task 2.6).
 */
class ReplayGuard {
public:
    /**
     * @brief Check a message and remember it if it's accepted.
     * @param hostname The sender, as in Message::hostname.
     * @param seq The sender's sequence number.
     * @param timestampMs The sender's clock, unix ms.
     * @param nowMs The local clock, unix ms (injected for tests).
     * @return Accepted, or why the message must be dropped.
     */
    [[nodiscard]] ReplayVerdict check(std::string_view hostname, std::uint32_t seq,
                                      std::uint64_t timestampMs, std::uint64_t nowMs);

    /**
     * @brief Same as the other overload, using the fields of @p message.
     * @param message A message returned by net::parse().
     * @param nowMs The local clock, unix ms.
     * @return Accepted, or why the message must be dropped.
     */
    [[nodiscard]] ReplayVerdict check(const Message& message, std::uint64_t nowMs);

    /**
     * @brief Number of hosts currently remembered (for tests and status).
     * @return 0..kReplayMaxHosts.
     */
    [[nodiscard]] std::size_t hostCount() const noexcept { return hosts_.size(); }

    /**
     * @brief Number of messages currently remembered for one host.
     * @param hostname The sender.
     * @return 0..kReplayMaxEntriesPerHost.
     */
    [[nodiscard]] std::size_t entryCount(std::string_view hostname) const;

private:
    /** @brief One accepted message. */
    struct Entry {
        std::uint32_t seq;          ///< Sender's sequence number.
        std::uint64_t timestampMs;  ///< Sender's timestamp.
    };
    /** @brief What is remembered about one sender. */
    struct Host {
        std::vector<Entry> entries;  ///< Accepted messages still inside the window.
        std::uint64_t lastSeenMs = 0;  ///< Local time of the last accepted message.
    };

    /** @brief Hash that also accepts std::string_view, so lookups don't
     *         allocate (C++20 heterogeneous lookup). */
    struct NameHash {
        using is_transparent = void;  ///< Enables heterogeneous lookup.
        /**
         * @brief Hash a hostname.
         * @param s The hostname.
         * @return Its std::hash<std::string_view> value.
         */
        std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    /**
     * @brief Drop entries (and hosts) whose timestamp left the window.
     * @param nowMs The local clock, unix ms.
     */
    void prune(std::uint64_t nowMs);

    /** @brief Keyed by hostname. */
    std::unordered_map<std::string, Host, NameHash, std::equal_to<>> hosts_;
};

}  // namespace duckling::net
