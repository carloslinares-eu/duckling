/**
 * @file replay_guard.cpp
 * @brief Timestamp window and duplicate detection for received messages.
 */
#include "net/replay_guard.hpp"

#include <algorithm>

namespace duckling::net {

std::string_view describe(ReplayVerdict verdict) noexcept {
    switch (verdict) {
    case ReplayVerdict::Accepted: return "accepted";
    case ReplayVerdict::TooOld: return "timestamp too old (stale, or clock behind)";
    case ReplayVerdict::TooNew: return "timestamp in the future (clock ahead)";
    case ReplayVerdict::Duplicate: return "duplicate (replay or retransmit)";
    }
    return "unknown";
}

void ReplayGuard::prune(std::uint64_t nowMs) {
    // An entry is useful only while a copy of it could still pass the
    // timestamp check, i.e. while timestampMs >= nowMs - window.
    const std::uint64_t oldest = nowMs > kReplayWindowMs ? nowMs - kReplayWindowMs : 0;
    for (auto it = hosts_.begin(); it != hosts_.end();) {
        auto& entries = it->second.entries;
        std::erase_if(entries, [oldest](const Entry& e) { return e.timestampMs < oldest; });
        it = entries.empty() ? hosts_.erase(it) : std::next(it);
    }
}

ReplayVerdict ReplayGuard::check(std::string_view hostname, std::uint32_t seq,
                                 std::uint64_t timestampMs, std::uint64_t nowMs) {
    // Unsigned on purpose: compare without subtracting into negatives.
    if (timestampMs < nowMs && nowMs - timestampMs > kReplayWindowMs) {
        return ReplayVerdict::TooOld;
    }
    if (timestampMs > nowMs && timestampMs - nowMs > kReplayWindowMs) {
        return ReplayVerdict::TooNew;
    }

    prune(nowMs);

    auto it = hosts_.find(hostname);
    if (it != hosts_.end()) {
        const auto& entries = it->second.entries;
        const bool seen = std::any_of(entries.begin(), entries.end(), [&](const Entry& e) {
            return e.seq == seq && e.timestampMs == timestampMs;
        });
        if (seen) {
            return ReplayVerdict::Duplicate;
        }
    } else {
        if (hosts_.size() >= kReplayMaxHosts) {
            const auto quietest = std::min_element(
                hosts_.begin(), hosts_.end(),
                [](const auto& a, const auto& b) { return a.second.lastSeenMs < b.second.lastSeenMs; });
            hosts_.erase(quietest);
        }
        it = hosts_.emplace(std::string(hostname), Host{}).first;
    }

    Host& host = it->second;
    if (host.entries.size() >= kReplayMaxEntriesPerHost) {
        const auto oldest = std::min_element(
            host.entries.begin(), host.entries.end(),
            [](const Entry& a, const Entry& b) { return a.timestampMs < b.timestampMs; });
        host.entries.erase(oldest);
    }
    host.entries.push_back({seq, timestampMs});
    host.lastSeenMs = nowMs;
    return ReplayVerdict::Accepted;
}

ReplayVerdict ReplayGuard::check(const Message& message, std::uint64_t nowMs) {
    return check(message.hostname, message.seq, message.timestampMs, nowMs);
}

std::size_t ReplayGuard::entryCount(std::string_view hostname) const {
    const auto it = hosts_.find(hostname);
    return it == hosts_.end() ? 0 : it->second.entries.size();
}

}  // namespace duckling::net
