// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// IWYU pragma: always_keep

// exchange/common/cxx_standard.hpp
// The one place that says cryptocogs is C++23 (cryptocogs#32, plan cmake4-cxx23), for a build that does not use CMake.
// It declares nothing, so include-cleaner would call every include of it unused; always_keep (above) says it
// is kept on purpose.
//
// Linking cryptocogs::<adapter> from CMake asks for C++23 (cxx_std_23); a build that does not use CMake must pass
// -std=c++23 itself, and gets the error below if it does not. This tests for newer than C++20, not for C++23's own
// value: GCC 13 reports 202100L for -std=c++23, so ">= 202302L" would reject it, and no CI leg would notice
// (GCC 15.2 and Apple Clang 21 report 202302L). A public header that includes no other exchange/ header must
// include this one first; test_standard_guard fails until it does.

#if __cplusplus <= 202002L
#error "cryptocogs requires C++23: compile with -std=c++23, or link cryptocogs::<adapter> from CMake, which asks for it"
#endif
