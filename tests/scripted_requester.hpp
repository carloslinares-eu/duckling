/**
 * @file scripted_requester.hpp
 * @brief Test double for hidpp::Requester: checks requests against a script
 *        and returns recorded replies.
 */
#pragma once

#include <doctest/doctest.h>

#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "hidpp/hex.hpp"
#include "hidpp/requester.hpp"

namespace duckling::test {

/** @brief Requester that checks each request against a script and returns its reply. */
class ScriptedRequester final : public hidpp::Requester {
public:
    /**
     * @brief Expect a request and answer it.
     * @param requestHex Expected request bytes.
     * @param replyHex   Reply bytes, or "" for a timeout.
     * @return *this, for chaining.
     */
    ScriptedRequester& expect(std::string requestHex, std::string replyHex) {
        script_.emplace_back(std::move(requestHex), std::move(replyHex));
        return *this;
    }

    /**
     * @brief Check the request and return the scripted reply.
     * @param request Request sent by the code under test.
     * @return The scripted reply, or std::nullopt for "".
     */
    std::optional<hidpp::Report> request(const hidpp::Report& request) override {
        REQUIRE_MESSAGE(!script_.empty(), "unexpected request " << hidpp::toHex(request.bytes()));
        auto [want, reply] = script_.front();
        script_.pop_front();
        CHECK(hidpp::toHex(request.bytes()) == want);
        if (reply.empty()) return std::nullopt;
        const auto bytes = hidpp::parseHex(reply);
        REQUIRE(bytes.has_value());
        return hidpp::Report::decode(*bytes);
    }

    /**
     * @brief Whether every scripted request was made.
     * @return True if the script is used up.
     */
    [[nodiscard]] bool done() const { return script_.empty(); }

private:
    std::deque<std::pair<std::string, std::string>> script_;  ///< Expected requests and replies.
};

/** @brief 16 zero bytes, to pad long frames in the script. */
inline const std::string kZeros16 = " 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00";

}  // namespace duckling::test
