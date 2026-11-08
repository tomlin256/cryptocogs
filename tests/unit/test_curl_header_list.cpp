// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// make_curl_header_list: the "Key: value" list that CurlHttpClient::perform hands to libcurl, and what it does when libcurl cannot
// grow the list (plan curl-header-list-append-failure, #48). The lists are real libcurl lists, read back by walking them. The
// only double is the function that grows a list, and it is libcurl's own until it is told to fail. No network, no sleeps.

#include "exchange/common/curl_header_list.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using exchange::rest::detail::CurlSlist;
using exchange::rest::detail::Headers;
using exchange::rest::detail::make_curl_header_list;

// The lines of a list, read back by walking it.
std::vector<std::string> lines_of(const CurlSlist& list) {
    std::vector<std::string> lines;
    for (const curl_slist* node = list.get(); node != nullptr; node = node->next)
        lines.emplace_back(node->data);
    return lines;
}

// libcurl's curl_slist_append, recording the list it is given on each call, and failing on the call numbered fail_on (1-based; 0
// never fails). Until it fails it is the real function, so every list it builds is a real list.
struct RecordingAppend {
    std::size_t                    fail_on{0};
    std::size_t                    calls{0};
    std::vector<const curl_slist*> given;

    curl_slist* operator()(curl_slist* list, const char* line) {
        ++calls;
        given.push_back(list);
        if (calls == fail_on)
            return nullptr;
        return curl_slist_append(list, line);
    }
};

// An append function that puts the new line in front: it returns a new head whose next is the list it was given.
curl_slist* prepend(curl_slist* list, const char* line) {
    curl_slist* const node = curl_slist_append(nullptr, line);
    if (node != nullptr)
        node->next = list;
    return node;
}

} // namespace

TEST(MakeCurlHeaderList, NoHeadersGiveNoListAndNoCall) {
    RecordingAppend append;
    const CurlSlist list = make_curl_header_list(Headers{}, std::ref(append));
    EXPECT_EQ(list.get(), nullptr);
    EXPECT_EQ(append.calls, 0U);
}

// The default append function is libcurl's own, so this is also the production path.
TEST(MakeCurlHeaderList, LinesAreKeyColonSpaceValueInKeyOrder) {
    const std::string long_value(200, 'x');
    const Headers     headers{{"B", "2"}, {"A", "1"}, {"C", long_value}};
    const CurlSlist   list = make_curl_header_list(headers);
    const std::vector<std::string> expected{"A: 1", "B: 2", "C: " + long_value};
    EXPECT_EQ(lines_of(list), expected);
}

TEST(MakeCurlHeaderList, AppendsOntoTheListBuiltSoFar) {
    RecordingAppend append;
    const CurlSlist list = make_curl_header_list(Headers{{"A", "1"}, {"B", "2"}, {"C", "3"}}, std::ref(append));
    ASSERT_EQ(append.given.size(), 3U);
    EXPECT_EQ(append.given[0], nullptr);     // the first line starts a list
    EXPECT_EQ(append.given[1], list.get());  // every later line is added to the list so far, whose head libcurl keeps
    EXPECT_EQ(append.given[2], list.get());
    EXPECT_EQ(lines_of(list).size(), 3U);
}

// libcurl leaves the list it was given alone when it fails, and the old loop released that list before the call: the nodes were
// leaked, the lines before the failure were not sent, and the lines after it started a new list. Now the failure throws and nothing
// is appended after it.
TEST(MakeCurlHeaderList, AFailureAtAnyPositionThrowsAndStops) {
    const Headers headers{{"A", "1"}, {"B", "2"}, {"C", "3"}};
    for (std::size_t position = 1; position <= headers.size(); ++position) {
        SCOPED_TRACE(std::string("the append for header ").append(std::to_string(position)).append(" fails"));
        RecordingAppend append;
        append.fail_on = position;
        try {
            static_cast<void>(make_curl_header_list(headers, std::ref(append)));
            ADD_FAILURE() << "expected std::runtime_error";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find("curl_slist_append"), std::string::npos) << e.what();
        }
        EXPECT_EQ(append.calls, position);
    }
}

// The man page says the grown list is what is returned, so the head is not promised to stay. Here every call returns a new head.
TEST(MakeCurlHeaderList, AdoptsTheHeadTheAppendFunctionReturns) {
    const CurlSlist list = make_curl_header_list(Headers{{"A", "1"}, {"B", "2"}, {"C", "3"}}, prepend);
    const std::vector<std::string> expected{"C: 3", "B: 2", "A: 1"};
    EXPECT_EQ(lines_of(list), expected);
}
