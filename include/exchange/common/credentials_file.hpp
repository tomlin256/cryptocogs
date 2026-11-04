// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/credentials_file.hpp
// Shared TOML credentials-file loader used by every adapter's
// <Exchange>Credentials::from_file. Resolves a per-exchange key file, parses it as TOML,
// and returns the requested string fields. The TOML dependency (toml++) is an
// implementation detail of src/exchange/common/credentials_file.cpp — this
// header exposes only standard-library types, so consumers need no TOML library.
//
// File format (uniform across exchanges), e.g. ~/.kraken/default:
//   api_key    = "PUBLICKEY..."
//   api_secret = "base64secret..."
//   # passphrase = "..."   # Coinbase only
//
// Namespace: exchange::rest

#include "exchange/common/cxx_standard.hpp"

#include <string>
#include <vector>

namespace exchange::rest {

// Read named string fields from a per-exchange TOML credentials file.
//
//   path = location.empty() ? <$HOME>/<default_dir>/<name>
//                           : <location>/<name>
//
// Returns the value of each entry in `keys`, in the same order. Throws
// std::runtime_error when:
//   - location is empty and $HOME is unset,
//   - the file cannot be opened or is not valid TOML,
//   - any requested key is missing, not a string, or an empty string.
std::vector<std::string> read_toml_credentials(
    const std::string&              name,
    const std::string&              default_dir,   // e.g. ".kraken"
    const std::string&              location,      // "" => $HOME/<default_dir>
    const std::vector<std::string>& keys);

} // namespace exchange::rest
