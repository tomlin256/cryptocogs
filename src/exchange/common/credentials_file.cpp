// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/credentials_file.hpp"

#include <cstdlib>    // std::getenv
#include <stdexcept>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

namespace exchange::rest {

namespace {

// head + key + tail + filepath, built by appending onto one string. A chain of
// operator+ makes a temporary string per link, and these messages are thrown from
// the loop below (performance-inefficient-string-concatenation).
std::string key_message(std::string_view   head,
                        const std::string& key,
                        std::string_view   tail,
                        const std::string& filepath) {
    std::string msg;
    msg.reserve(head.size() + key.size() + tail.size() + filepath.size());
    msg.append(head);
    msg += key;
    msg.append(tail);
    msg += filepath;
    return msg;
}

} // namespace

std::vector<std::string> read_toml_credentials(
    const std::string&              name,
    const std::string&              default_dir,
    const std::string&              location,
    const std::vector<std::string>& keys) {

    std::string dir;
    if (location.empty()) {
        const char* home = std::getenv("HOME");
        if (!home) throw std::runtime_error("HOME environment variable not set");
        dir = std::string(home) + "/" + default_dir;
    } else {
        dir = location;
    }
    const std::string filepath = dir + "/" + name;

    // toml::parse_file throws toml::parse_error for both an unopenable file and
    // malformed TOML; fold either into the project's std::runtime_error contract.
    toml::table tbl;
    try {
        tbl = toml::parse_file(filepath);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error("Failed to read credentials file " + filepath +
                                 ": " + std::string(e.description()));
    }

    std::vector<std::string> out;
    out.reserve(keys.size());
    for (const auto& key : keys) {
        const auto node = tbl[key];
        if (!node) {
            throw std::runtime_error(
                key_message("Missing key '", key, "' in credentials file: ", filepath));
        }
        // value<std::string>() yields a value only when the node is genuinely a
        // string (no int->string coercion), so a non-string entry is rejected.
        const auto value = node.value<std::string>();
        if (!value) {
            throw std::runtime_error(
                key_message("Key '", key, "' in credentials file is not a string: ", filepath));
        }
        if (value->empty()) {
            throw std::runtime_error(
                key_message("Key '", key, "' in credentials file is empty: ", filepath));
        }
        out.push_back(*value);
    }
    return out;
}

} // namespace exchange::rest
