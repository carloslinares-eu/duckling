/**
 * @file requester.hpp
 * @brief Interface for sending a HID++ request and getting its reply.
 */
#pragma once

#include <optional>

#include "hidpp/report.hpp"

namespace duckling::hidpp {

/**
 * @brief Something that sends a request and returns its reply.
 *
 * io::Receiver implements it. Keeping hidpp independent of hidapi lets the
 * tests replay recorded replies.
 */
class Requester {
public:
    /** @brief Virtual destructor for a polymorphic base. */
    virtual ~Requester() = default;

    /**
     * @brief Send @p request and wait for its reply with the default timeout.
     * @param request Request with a non-zero software id (kDucklingSwId).
     * @return The reply or an error reply (Report::isError()), or
     *         std::nullopt on timeout or I/O error.
     */
    [[nodiscard]] virtual std::optional<Report> request(const Report& request) = 0;

protected:
    /** @brief Only derived classes construct it. */
    Requester() = default;
    /** @brief Copyable by derived classes only (no slicing through the base). */
    Requester(const Requester&) = default;
    /** @brief Movable by derived classes only. */
    Requester(Requester&&) = default;
    /**
     * @brief Copy assignment for derived classes.
     * @return *this.
     */
    Requester& operator=(const Requester&) = default;
    /**
     * @brief Move assignment for derived classes.
     * @return *this.
     */
    Requester& operator=(Requester&&) = default;
};

}  // namespace duckling::hidpp
