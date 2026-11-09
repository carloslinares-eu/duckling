/**
 * @file receiver.hpp
 * @brief Access to a Logi Bolt receiver's HID++ interface through hidapi.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hidpp/report.hpp"
#include "hidpp/requester.hpp"

struct hid_device_;  // hidapi's opaque handle (hidapi.h: typedef struct hid_device_ hid_device)

/**
 * @namespace duckling::io
 * @brief Device I/O: talking to the Bolt receiver through hidapi.
 */
namespace duckling::io {

/** @brief Logitech USB vendor id. */
inline constexpr std::uint16_t kLogitechVid = 0x046D;
/** @brief Logi Bolt receiver product id (PLAN.md C1, C4). */
inline constexpr std::uint16_t kBoltPid = 0xC548;
/** @brief HID usage page of the HID++ vendor collections. */
inline constexpr std::uint16_t kHidppUsagePage = 0xFF00;
/** @brief Usage of the short-report (0x10, 7-byte) collection. */
inline constexpr std::uint16_t kHidppShortUsage = 0x0001;
/** @brief Usage of the long-report (0x11, 20-byte) collection. */
inline constexpr std::uint16_t kHidppLongUsage = 0x0002;

/**
 * @brief Where a receiver's HID++ collections live.
 *
 * - **Linux (hidraw):** both collections are on one `/dev/hidrawN` node, so
 *   `shortPath == longPath`.
 * - **Windows:** each top-level collection is its own device path
 *   (`…&mi_02&col01…` for short, `…&col02…` for long; PLAN.md C4). Short
 *   reports, which includes the receiver's 0x41 connection notices, only
 *   arrive on the short path.
 */
struct ReceiverPaths {
    std::string shortPath;     ///< Path of the 0xFF00/0x0001 collection.
    std::string longPath;      ///< Path of the 0xFF00/0x0002 collection.
    std::string serial;        ///< USB serial number, if reported.
    std::uint16_t productId{}; ///< USB product id, e.g. kBoltPid.

    /**
     * @brief Whether both collections share one OS handle.
     * @return True when shortPath == longPath (Linux).
     */
    [[nodiscard]] bool sharedHandle() const { return shortPath == longPath; }
};

/**
 * @brief Key that is equal for all HID collections of one receiver interface.
 *
 * Windows reports an empty serial for the Bolt receiver (W1: `serial ''`), so
 * the two collection paths are paired by their device instance id instead:
 * @code
 *   \\?\HID#VID_046D&PID_C548&MI_02&Col01#7&85d146e&0&0000#{4d1e…}
 *   \\?\HID#VID_046D&PID_C548&MI_02&Col02#7&85d146e&0&0001#{4d1e…}
 *   key: hid#vid_046d&pid_c548&mi_02#7&85d146e&0
 * @endcode
 * The key is lower-cased, `&colNN` is removed from the hardware id, and the
 * trailing collection number (`&000N`) is removed from the instance id.
 * Paths without `#` (Linux `/dev/hidrawN`) are returned unchanged.
 *
 * @param path hidapi device path.
 * @return The pairing key.
 * @see PLAN.md C4 for the Windows paths.
 */
[[nodiscard]] std::string collectionKey(std::string_view path);

/**
 * @brief List the HID++ interfaces of all connected Bolt receivers.
 * @return One entry per receiver that has both collections, in hidapi order.
 *         Empty if none are found or hidapi fails to initialise.
 */
[[nodiscard]] std::vector<ReceiverPaths> findReceivers();

/**
 * @brief An open connection to one receiver's HID++ interface.
 *
 * Reports from every collection are merged into one stream. Writes are sent
 * to the collection that matches the report's ID.
 *
 * The receiver is shared with other software (Solaar on Linux, Logi Options+
 * on Windows; PLAN.md C4/C5). Their replies show up in read() too, and are
 * told apart from duckling's by software id (hidpp::kDucklingSwId).
 *
 * @warning Not thread-safe. In M1 the CLI drives it from one thread. The
 *          daemon (task 2.6) gives reading to one HID thread.
 */
class Receiver final : public hidpp::Requester {
public:
    /** @brief A closed receiver. Use open() or openFirst() to get an open one. */
    Receiver() = default;
    /** @brief Closes the handles. */
    ~Receiver() override;
    /** @brief Not copyable: it owns OS handles. */
    Receiver(const Receiver&) = delete;
    /**
     * @brief Not copyable: it owns OS handles.
     * @return Never; deleted.
     */
    Receiver& operator=(const Receiver&) = delete;
    /**
     * @brief Take over another receiver's handles.
     * @param other Receiver to move from. It is left closed.
     */
    Receiver(Receiver&& other) noexcept;
    /**
     * @brief Close this receiver and take over another's handles.
     * @param other Receiver to move from. It is left closed.
     * @return *this.
     */
    Receiver& operator=(Receiver&& other) noexcept;

    /**
     * @brief Open a receiver.
     * @param paths Receiver to open, as returned by findReceivers().
     * @return The open receiver, or std::nullopt if any collection can't be
     *         opened (missing permissions, unplugged meanwhile).
     */
    [[nodiscard]] static std::optional<Receiver> open(const ReceiverPaths& paths);

    /**
     * @brief Open the first receiver findReceivers() reports.
     * @return The open receiver, or std::nullopt if none can be opened.
     */
    [[nodiscard]] static std::optional<Receiver> openFirst();

    /**
     * @brief Whether the handles are open.
     * @return True until close() or kMaxReadErrors read errors in a row.
     */
    [[nodiscard]] bool isOpen() const noexcept;

    /** @brief Close all handles. Safe to call more than once. */
    void close() noexcept;

    /**
     * @brief Paths this receiver was opened from.
     * @return The paths given to open().
     */
    [[nodiscard]] const ReceiverPaths& paths() const noexcept { return paths_; }

    /**
     * @brief Read the next report.
     *
     * Reports that request() set aside while waiting for its reply are
     * returned first, in arrival order.
     *
     * On Windows the long collection is checked before the short one. When
     * both have reports waiting, this returns the keyboard's CHANGE_HOST
     * announcement (long) before the 0x41 OFFLINE notice (short) it sent
     * 37 ms later (PLAN.md C2). Order across the two collections is otherwise
     * not guaranteed.
     *
     * @param timeout How long to wait. Zero means just poll.
     * @return The report, or std::nullopt on timeout, on an undecodable
     *         frame, or after an I/O error. A single error (e.g. a signal
     *         interrupting `poll()` on Linux) leaves the receiver open; after
     *         kMaxReadErrors in a row it is closed.
     */
    [[nodiscard]] std::optional<hidpp::Report> read(std::chrono::milliseconds timeout);

    /**
     * @brief Whether request() set reports aside that read() hasn't returned yet.
     *
     * Those reports arrived while request() was waiting, so their read time
     * isn't their arrival time (`duckling sniff` says so; issue #9).
     * @return True if read() would return a set-aside report next.
     */
    [[nodiscard]] bool hasPending() const noexcept { return !pending_.empty(); }

    /**
     * @brief Send one report.
     * @param report Report to send. Short reports go to the short collection
     *               and long ones to the long collection.
     * @return True if the OS accepted the whole frame.
     */
    bool write(const hidpp::Report& report);

    /**
     * @brief Send a request and wait for its reply.
     *
     * Reports that are not the reply (events, other software's traffic) are
     * kept, in order, for later read() calls. At most kMaxPending are kept;
     * the oldest are dropped first.
     *
     * A request that times out is remembered for kLateReplyWindow. If its
     * reply arrives late, it is dropped, so it can't be taken for the reply
     * to a later request with the same header (hidpp::isReplyTo only
     * compares headers). HID++ 1.0 error replies to register reads are
     * the exception: they don't name the sub-register, so they are never
     * treated as late replies (issue #7).
     *
     * @param request Request to send. For HID++ 2.0 requests the software id
     *                must not be 0 (that is reserved for device events); use
     *                hidpp::kDucklingSwId. HID++ 1.0 register requests
     *                (hidpp::isRegisterAccess) carry a register address in
     *                byte 3 instead and are exempt.
     * @param timeout How long to wait for the reply.
     * @return The reply (check Report::isError()), or std::nullopt on
     *         timeout or I/O error.
     * @throws std::invalid_argument if a HID++ 2.0 request's software id is 0.
     */
    [[nodiscard]] std::optional<hidpp::Report> request(const hidpp::Report& request,
                                                       std::chrono::milliseconds timeout);

    /**
     * @brief request() with kDefaultRequestTimeout (hidpp::Requester interface).
     * @param request Request to send; see the two-argument overload.
     * @return The reply, or std::nullopt on timeout or I/O error.
     * @throws std::invalid_argument if a HID++ 2.0 request's software id is 0.
     */
    [[nodiscard]] std::optional<hidpp::Report> request(const hidpp::Report& request) override {
        return this->request(request, kDefaultRequestTimeout);
    }

    /** @brief Max reports request() keeps for read(). */
    static constexpr std::size_t kMaxPending = 256;
    /**
     * @brief Default request() timeout.
     *
     * The first request after the device has been idle took 0.66 s (C4) and
     * 0.56–1.03 s (task 1.3 probes, W1) on Windows.
     * @see PLAN.md "Timing"
     */
    static constexpr std::chrono::milliseconds kDefaultRequestTimeout{2000};
    /** @brief How long a timed-out request's late reply is still recognised and dropped. */
    static constexpr std::chrono::milliseconds kLateReplyWindow{5000};
    /** @brief Read errors in a row after which the receiver is closed (unplugged). */
    static constexpr int kMaxReadErrors = 3;

private:
    /** @brief A request that timed out and whose reply may still arrive. */
    struct TimedOut {
        hidpp::Report request;                           ///< The request sent.
        std::chrono::steady_clock::time_point expires;  ///< Forget it after this.
    };

    /**
     * @brief Read one report from the OS handles, ignoring the pending queue.
     *
     * Late replies to timed-out requests are dropped here.
     *
     * @param timeout How long to wait.
     * @return The report, or std::nullopt on timeout or error.
     */
    std::optional<hidpp::Report> readDevice(std::chrono::milliseconds timeout);

    /**
     * @brief Read one report from the OS handles, without late-reply filtering.
     * @param timeout How long to wait.
     * @return The report, or std::nullopt on timeout or error.
     */
    std::optional<hidpp::Report> readRaw(std::chrono::milliseconds timeout);

    /**
     * @brief Drop @p report if it is the late reply to a timed-out request.
     * @param report Report just read.
     * @return True if it was a late reply (and that request is now forgotten).
     */
    bool consumeLateReply(const hidpp::Report& report);

    /**
     * @brief Read from one handle.
     * @param handle  hidapi handle.
     * @param timeout Milliseconds to wait (0 = poll).
     * @return The report, or std::nullopt on timeout, junk, or error.
     */
    std::optional<hidpp::Report> readHandle(hid_device_* handle, int timeout);

    hid_device_* short_ = nullptr;  ///< Short collection (same as long_ on Linux).
    hid_device_* long_ = nullptr;   ///< Long collection.
    ReceiverPaths paths_;           ///< Where it was opened from.
    std::deque<hidpp::Report> pending_;  ///< Reports set aside by request().
    std::vector<TimedOut> timedOut_;     ///< Requests whose reply may come late.
    int readErrors_ = 0;                 ///< Read errors in a row.
};

}  // namespace duckling::io
