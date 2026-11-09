/**
 * @file receiver.cpp
 * @brief hidapi-based implementation of duckling::io::Receiver.
 */
#include "io/receiver.hpp"

#include <hidapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwchar>
#include <stdexcept>
#include <utility>

namespace duckling::io {

namespace {

using Clock = std::chrono::steady_clock;

/**
 * @brief Poll slice used when two Windows handles have to be read in turn.
 *
 * hidapi can't wait on two handles at once, so each gets a short timeout in
 * turn. 5 ms per handle keeps latency far below anything a user notices.
 */
constexpr int kSliceMs = 5;

/**
 * @brief Convert hidapi's wide-char strings to UTF-8 (ASCII is enough for serials).
 * @param w Wide string or nullptr.
 * @return Narrow copy. Characters outside ASCII become '?'.
 */
std::string narrow(const wchar_t* w) {
    std::string out;
    if (w == nullptr) return out;
    for (; *w != L'\0'; ++w) out += (*w > 0 && *w < 0x80) ? static_cast<char>(*w) : '?';
    return out;
}

/**
 * @brief Clamp a duration to a non-negative int number of milliseconds.
 * @param d Duration.
 * @return Milliseconds in [0, INT_MAX].
 */
int toMs(std::chrono::milliseconds d) {
    const auto c = std::clamp<std::chrono::milliseconds::rep>(d.count(), 0, 0x7FFFFFFF);
    return static_cast<int>(c);
}

}  // namespace

std::string collectionKey(std::string_view path) {
    std::string key(path);
    if (key.find('#') == std::string::npos) return key;  // Linux hidraw node
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (key.starts_with("\\\\?\\")) key.erase(0, 4);

    // Segments: enumerator # hardware id # instance id # interface class GUID.
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const auto hash = key.find('#', start);
        parts.push_back(key.substr(start, hash - start));
        if (hash == std::string::npos) break;
        start = hash + 1;
    }
    for (auto& part : parts) {
        if (const auto col = part.find("&col"); col != std::string::npos) {
            part.erase(col, part.find('&', col + 1) - col);
        }
    }
    if (parts.size() >= 3) {
        std::string& instance = parts[parts.size() - 2];
        if (const auto amp = instance.rfind('&'); amp != std::string::npos) instance.erase(amp);
    }
    std::string out = parts.front();
    for (std::size_t i = 1; i < parts.size(); ++i) out += "#" + parts[i];
    return out;
}

std::vector<ReceiverPaths> findReceivers() {
    std::vector<ReceiverPaths> out;
    if (hid_init() != 0) return out;

    hid_device_info* list = hid_enumerate(kLogitechVid, kBoltPid);
    for (hid_device_info* d = list; d != nullptr; d = d->next) {
        if (d->usage_page != kHidppUsagePage) continue;
        if (d->usage != kHidppShortUsage && d->usage != kHidppLongUsage) continue;

        // Linux: both collections share one path. Windows: the paths differ
        // only in &ColNN and the collection number (see collectionKey()).
        const std::string key = collectionKey(d->path);
        auto it = std::find_if(out.begin(), out.end(), [&](const ReceiverPaths& r) {
            const std::string& known = r.shortPath.empty() ? r.longPath : r.shortPath;
            return collectionKey(known) == key;
        });
        if (it == out.end()) {
            out.push_back(ReceiverPaths{{}, {}, narrow(d->serial_number), d->product_id});
            it = std::prev(out.end());
        }
        (d->usage == kHidppShortUsage ? it->shortPath : it->longPath) = d->path;
    }
    hid_free_enumeration(list);

    std::erase_if(out, [](const ReceiverPaths& r) { return r.shortPath.empty() || r.longPath.empty(); });
    return out;
}

Receiver::~Receiver() { close(); }

Receiver::Receiver(Receiver&& other) noexcept
    : short_(std::exchange(other.short_, nullptr)),
      long_(std::exchange(other.long_, nullptr)),
      paths_(std::move(other.paths_)),
      pending_(std::move(other.pending_)),
      timedOut_(std::move(other.timedOut_)),
      readErrors_(std::exchange(other.readErrors_, 0)) {}

Receiver& Receiver::operator=(Receiver&& other) noexcept {
    if (this != &other) {
        close();
        short_ = std::exchange(other.short_, nullptr);
        long_ = std::exchange(other.long_, nullptr);
        paths_ = std::move(other.paths_);
        pending_ = std::move(other.pending_);
        timedOut_ = std::move(other.timedOut_);
        readErrors_ = std::exchange(other.readErrors_, 0);
    }
    return *this;
}

std::optional<Receiver> Receiver::open(const ReceiverPaths& paths) {
    if (hid_init() != 0) return std::nullopt;
    Receiver r;
    r.paths_ = paths;
    r.long_ = hid_open_path(paths.longPath.c_str());
    if (r.long_ == nullptr) return std::nullopt;
    r.short_ = paths.sharedHandle() ? r.long_ : hid_open_path(paths.shortPath.c_str());
    if (r.short_ == nullptr) return std::nullopt;  // destructor closes long_
    return r;
}

std::optional<Receiver> Receiver::openFirst() {
    for (const auto& paths : findReceivers()) {
        if (auto r = open(paths)) return r;
    }
    return std::nullopt;
}

bool Receiver::isOpen() const noexcept { return long_ != nullptr; }

void Receiver::close() noexcept {
    if (short_ != nullptr && short_ != long_) hid_close(short_);
    if (long_ != nullptr) hid_close(long_);
    short_ = nullptr;
    long_ = nullptr;
}

std::optional<hidpp::Report> Receiver::readHandle(hid_device_* handle, int timeout) {
    std::array<std::uint8_t, 64> buf{};
    const int n = hid_read_timeout(handle, buf.data(), buf.size(), timeout);
    if (n < 0) {
        // One error can be transient (Linux: poll() interrupted by a signal,
        // EINTR). An unplugged receiver fails every call, so a run of errors
        // closes it quickly.
        if (++readErrors_ >= kMaxReadErrors) close();
        return std::nullopt;
    }
    readErrors_ = 0;
    if (n == 0) return std::nullopt;
    return hidpp::Report::decode(std::span(buf.data(), static_cast<std::size_t>(n)));
}

bool Receiver::consumeLateReply(const hidpp::Report& report) {
    const auto now = Clock::now();
    std::erase_if(timedOut_, [&](const TimedOut& t) { return t.expires <= now; });
    const auto it = std::find_if(timedOut_.begin(), timedOut_.end(),
                                 [&](const TimedOut& t) { return hidpp::isReplyTo(t.request, report); });
    if (it == timedOut_.end()) return false;
    timedOut_.erase(it);
    return true;
}

std::optional<hidpp::Report> Receiver::readDevice(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (true) {
        auto r = readRaw(timeout);
        if (!r || !consumeLateReply(*r)) return r;
        timeout = std::max(std::chrono::milliseconds{0},
                           std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()));
    }
}

std::optional<hidpp::Report> Receiver::readRaw(std::chrono::milliseconds timeout) {
    if (!isOpen()) return std::nullopt;
    if (short_ == long_) return readHandle(long_, toMs(timeout));

    // Two handles (Windows): take turns until something arrives or time is up.
    // Long first: the CHANGE_HOST announcement (long) precedes the 0x41
    // OFFLINE notice (short) by 37 ms (C2), and should be read first.
    const auto deadline = Clock::now() + timeout;
    do {
        for (hid_device_* h : {long_, short_}) {
            if (auto r = readHandle(h, timeout.count() == 0 ? 0 : kSliceMs)) return r;
            if (!isOpen()) return std::nullopt;
        }
    } while (Clock::now() < deadline);
    return std::nullopt;
}

std::optional<hidpp::Report> Receiver::read(std::chrono::milliseconds timeout) {
    if (!pending_.empty()) {
        hidpp::Report r = pending_.front();
        pending_.pop_front();
        return r;
    }
    return readDevice(timeout);
}

bool Receiver::write(const hidpp::Report& report) {
    if (!isOpen()) return false;
    hid_device_* h = report.id() == hidpp::ReportId::Short ? short_ : long_;
    const auto bytes = report.bytes();
    const int n = hid_write(h, bytes.data(), bytes.size());
    return n >= 0 && static_cast<std::size_t>(n) >= bytes.size();
}

std::optional<hidpp::Report> Receiver::request(const hidpp::Report& req,
                                               std::chrono::milliseconds timeout) {
    if (!hidpp::isRegisterAccess(req.featureIndex()) && req.swId() == 0) {
        throw std::invalid_argument("HID++ 2.0 requests must not use software id 0");
    }
    if (!write(req)) return std::nullopt;

    const auto deadline = Clock::now() + timeout;
    while (isOpen()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) break;
        auto r = readDevice(left);
        if (!r) continue;
        if (hidpp::isReplyTo(req, *r)) return r;
        if (pending_.size() >= kMaxPending) pending_.pop_front();
        pending_.push_back(*r);
    }
    timedOut_.push_back(TimedOut{req, Clock::now() + kLateReplyWindow});
    return std::nullopt;
}

}  // namespace duckling::io
