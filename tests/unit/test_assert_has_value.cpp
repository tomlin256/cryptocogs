// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// Tests of ASSERT_HAS_VALUE (assert_has_value.hpp; plan clear-clang-tidy-findings, #46).
//
// Pinned here: an engaged optional passes and can be used afterwards; an empty one fails the test fatally, once, names the
// expression, and returns from the function; text streamed after the macro follows on its own line, as it does for gtest's own
// assertions; and the macro can be the then- or else-branch of an `if` that has an `else`.
//
// Not pinned here, because gtest cannot observe it: that clang-tidy's bugprone-unchecked-optional-access sees the check. The
// gate does. Every optional below comes back from a call, so the analysis cannot know it holds a value, and the tests dereference
// it after the macro: `clang_tidy_tests_unit_test_assert_has_value` goes red if the macro stops being a check that the analysis
// can follow, which is what happened to ASSERT_TRUE(opt.has_value()), the form the macro replaces.
//
// The failing cases run inside a callable under a ScopedFakeTestPartResultReporter (gtest-spi.h), which collects what the
// callable reports, so that a failure that is expected does not reach the running test. The callable returns void, as the
// macro requires.

#include "assert_has_value.hpp"

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Failure {
    testing::TestPartResult::Type type{testing::TestPartResult::kSuccess};
    std::string                   message;
};

// Runs fn and returns every failure it reported, in order. gtest ends the text of every failure with a newline when it has no
// stack trace to add (and with the trace when it has), which depends on the platform and is not the macro's: summary() drops
// the trace and the one trailing newline is trimmed, leaving the text that the macro and the code streaming after it wrote.
template <typename Fn>
std::vector<Failure> capture(const Fn& fn) {
    testing::TestPartResultArray failures;
    {
        const testing::ScopedFakeTestPartResultReporter reporter(
            testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &failures);
        fn();
    }
    std::vector<Failure> seen;
    for (int i = 0; i < failures.size(); ++i) {
        const testing::TestPartResult& part = failures.GetTestPartResult(i);
        std::string                    text = part.summary();
        if (!text.empty() && text.back() == '\n') {
            text.pop_back();
        }
        seen.push_back(Failure{part.type(), std::move(text)});
    }
    return seen;
}

struct Inner {
    int a{0};
    int b{0};
};

struct Response {
    bool                       ok{false};
    std::optional<Inner>       result;
    std::optional<std::string> name;
};

// The values come back from calls, so an analysis that stops at a call cannot know what they hold.
template <typename T>
std::optional<T> unknown(T value) {
    return value;
}

Response make_response() {
    Response r;
    r.ok     = true;
    r.result = Inner{1, 2};
    r.name   = "x";
    return r;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// An engaged optional
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssertHasValue, EngagedPassesAndTheValueIsUsable) {
    const std::optional<int> o = unknown(7);
    ASSERT_HAS_VALUE(o);
    EXPECT_EQ(*o, 7);
}

TEST(AssertHasValue, EngagedReportsNothingAndTheCodeAfterItRuns) {
    bool       ran_after = false;
    const auto failures  = capture([&ran_after] {
        const std::optional<int> o = unknown(7);
        ASSERT_HAS_VALUE(o);
        ran_after = true;
    });
    EXPECT_TRUE(failures.empty());
    EXPECT_TRUE(ran_after);
}

// ─────────────────────────────────────────────────────────────────────────────
// An empty optional
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssertHasValue, EmptyFailsOnceFatallyNamesTheExpressionAndReturns) {
    bool       ran_after = false;
    const auto failures  = capture([&ran_after] {
        const std::optional<int> o = std::nullopt;
        ASSERT_HAS_VALUE(o);
        ran_after = true;
    });
    ASSERT_EQ(failures.size(), 1u);
    EXPECT_EQ(failures[0].type, testing::TestPartResult::kFatalFailure);
    EXPECT_EQ(failures[0].message, "o has no value");
    EXPECT_FALSE(ran_after) << "a failed ASSERT_HAS_VALUE must return from the function, as ASSERT_TRUE does";
}

TEST(AssertHasValue, TextStreamedAfterTheMacroFollowsOnItsOwnLine) {
    const auto failures = capture([] {
        const std::optional<std::string> name = std::nullopt;
        ASSERT_HAS_VALUE(name) << "while reading " << 3 << " fields";
    });
    ASSERT_EQ(failures.size(), 1u);
    EXPECT_EQ(failures[0].type, testing::TestPartResult::kFatalFailure);
    EXPECT_EQ(failures[0].message, "name has no value\nwhile reading 3 fields");
}

TEST(AssertHasValue, TheFailureNamesTheExpressionAsWritten) {
    const auto member = capture([] {
        const Response resp{};
        ASSERT_HAS_VALUE(resp.result);
    });
    const auto pointer = capture([] {
        const auto got = std::make_shared<std::optional<int>>();
        ASSERT_HAS_VALUE(*got);
    });
    const auto text = capture([] {
        const Response resp{};
        ASSERT_HAS_VALUE(resp.name);
    });
    ASSERT_EQ(member.size(), 1u);
    EXPECT_EQ(member[0].message, "resp.result has no value");
    ASSERT_EQ(pointer.size(), 1u);
    EXPECT_EQ(pointer[0].message, "*got has no value");
    ASSERT_EQ(text.size(), 1u);
    EXPECT_EQ(text[0].message, "resp.name has no value");
}

// ─────────────────────────────────────────────────────────────────────────────
// The forms the tests use it on
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssertHasValue, WorksOnMembersOfAStructAndOnAStringOptional) {
    const Response resp = make_response();
    ASSERT_HAS_VALUE(resp.result);
    ASSERT_HAS_VALUE(resp.name);
    EXPECT_EQ(resp.result->a, 1);
    EXPECT_EQ(resp.result->b, 2);
    EXPECT_EQ(resp.name->size(), 1u);
}

TEST(AssertHasValue, WorksThroughAPointerToAnOptional) {
    const auto got = std::make_shared<std::optional<int>>(unknown(4));
    ASSERT_HAS_VALUE(*got);
    EXPECT_EQ(**got, 4);
}

TEST(AssertHasValue, WorksOnALocalThatACallbackSets) {
    std::optional<int> got;
    const auto         on_event = [&got](const int v) { got = v; };
    on_event(5);
    ASSERT_HAS_VALUE(got);
    EXPECT_EQ(*got, 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// A branch of an if
//
// The macro ends in an if/else of its own, so it is a branch of an `if` only where the outer `if` has an `else` too: an `if`
// without one is rejected (-Wdangling-else), as it is for gtest's own assertions under GCC, and needs braces.
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssertHasValue, CanBeTheThenBranchOfAnIfWithAnElse) {
    const auto run = [](const bool flag, const std::optional<int>& o, std::string& trace) {
        return capture([flag, &o, &trace] {
            if (flag)
                ASSERT_HAS_VALUE(o);
            else
                trace += "else;";
            trace += "after;";
        });
    };
    const std::optional<int> empty = std::nullopt;
    const std::optional<int> full  = unknown(1);
    std::string              trace;

    auto failures = run(true, full, trace);
    EXPECT_TRUE(failures.empty());
    EXPECT_EQ(trace, "after;");

    trace.clear();
    failures = run(false, empty, trace);  // the macro is not evaluated, so an empty optional is fine here
    EXPECT_TRUE(failures.empty());
    EXPECT_EQ(trace, "else;after;");

    trace.clear();
    failures = run(true, empty, trace);
    ASSERT_EQ(failures.size(), 1u);
    EXPECT_EQ(failures[0].message, "o has no value");
    EXPECT_EQ(trace, "") << "a failed check returns before the code after the if";
}

TEST(AssertHasValue, CanBeTheElseBranchOfAnIf) {
    const auto run = [](const bool flag, const std::optional<int>& o, std::string& trace) {
        return capture([flag, &o, &trace] {
            if (flag)
                trace += "then;";
            else
                ASSERT_HAS_VALUE(o) << "in the else branch";
            trace += "after;";
        });
    };
    const std::optional<int> empty = std::nullopt;
    const std::optional<int> full  = unknown(1);
    std::string              trace;

    auto failures = run(true, empty, trace);  // the macro is not evaluated
    EXPECT_TRUE(failures.empty());
    EXPECT_EQ(trace, "then;after;");

    trace.clear();
    failures = run(false, full, trace);
    EXPECT_TRUE(failures.empty());
    EXPECT_EQ(trace, "after;");

    trace.clear();
    failures = run(false, empty, trace);
    ASSERT_EQ(failures.size(), 1u);
    EXPECT_EQ(failures[0].message, "o has no value\nin the else branch");
    EXPECT_EQ(trace, "") << "a failed check returns before the code after the if";
}
