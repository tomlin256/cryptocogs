// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

#include <gtest/gtest.h>

// ASSERT_HAS_VALUE(optional_expr): fails the test, fatally, if the std::optional (or anything else with has_value()) is empty.
// It stands where ASSERT_TRUE(optional_expr.has_value()) stood, and the code after it dereferences the optional as before
// (plan clear-clang-tidy-findings, #46).
//
// Why not ASSERT_TRUE: clang-tidy's bugprone-unchecked-optional-access does not take ASSERT_TRUE(opt.has_value()), ASSERT_TRUE(opt)
// or ASSERT_NE(opt, std::nullopt) as a check, and reports every `*opt` and `opt->` after one, whatever the test asserted
// (measured with clang-tidy 23.1.4 and 23.1.6; gtest's assertion branches on an AssertionResult, not on the optional). This macro
// is a plain `if` on has_value() whose else branch returns, which the analysis follows. It hides nothing: an access with no check
// before it, and `opt.value()` in place of `*opt`, are still reported.
//
// Use it as ASSERT_TRUE is used: as a statement in a function that returns void (the failure `return`s), on an optional that the
// analysis can name: a variable, a member chain (`resp.result`) or `*ptr`. The macro reads the expression once and the code after
// it reads it again, so bind a call to a local first (`const auto r = f(); ASSERT_HAS_VALUE(r);`), never `ASSERT_HAS_VALUE(f())`.
// The same goes for an element reached by an index or `.at()` (`v[i].opt`, `v.at(i).opt`): the analysis takes each call for a
// different object, so a check on one and a use of the next are not matched. Bind the element once, `const auto& e = v[i];`, and
// check `e.opt`. The failure names the expression, and text streamed after the macro goes on its own line, as for gtest's assertions:
//
//     ASSERT_HAS_VALUE(resp.result) << "for " << symbol;     // resp.result has no value
//                                                            // for BTC/USD
//
// The macro ends in an if/else of its own, so as a branch of an `if` it needs the `if` to have an `else`, or braces: a bare
// `if (x) ASSERT_HAS_VALUE(o);` is rejected by -Wdangling-else, as gtest's own assertions are under GCC. Where a macro cannot go
// (a lambda or helper that returns a value), check the optional in plain sight instead:
// `if (!opt) { ADD_FAILURE() << "opt has no value"; return ...; }`.
//
// GTEST_FATAL_FAILURE_ is what gtest's own ASSERT_TRUE expands to; naming it, and not FAIL(), keeps gtest's layout for
// the message (the text first, whatever is streamed after it on the next line). test_assert_has_value.cpp pins the layout.
#define ASSERT_HAS_VALUE(optional_expr)                              \
    if ((optional_expr).has_value()) {                               \
    } else                                                           \
        GTEST_FATAL_FAILURE_(#optional_expr " has no value")
