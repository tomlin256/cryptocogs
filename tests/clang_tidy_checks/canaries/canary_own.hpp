// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// canary_own.hpp — a header that clang-tidy must report on when it sits under include/exchange/ (plan
// clang-tidy-opt-in-test-step, #34). See canary.cpp.

#pragma once

#include <cstddef>
#include <string>

// The string is copied for each call and only read: performance-unnecessary-value-param.
inline std::size_t canary_own_size(std::string s) { return s.size(); }
