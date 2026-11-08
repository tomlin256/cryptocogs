// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// canary_dep.hpp — a header that clang-tidy must not report on, because it sits where a dependency's would
// (plan clang-tidy-opt-in-test-step, #34). See canary.cpp.

#pragma once

#include <cstddef>
#include <string>

// The same defect as canary_own.hpp: performance-unnecessary-value-param, if the path filter let it through.
inline std::size_t canary_dep_size(std::string s) { return s.size(); }
