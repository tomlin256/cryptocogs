// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include <stdexcept>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "exchange/kraken/rest_client.hpp"

using namespace exchange::kraken::rest;

int main(int argc, char* argv[])
{
    CLI::App app{"Kraken private REST example — account balance"};

    std::string creds_name = "default";
    app.add_option("-c,--credentials", creds_name,
                   "Credentials profile name (TOML file at ~/.kraken/<name>)")
        ->capture_default_str();

    CLI11_PARSE(app, argc, argv);

    curl_global_init(CURL_GLOBAL_ALL);

    try {
        KrakenRestClient client;
        auto creds = KrakenCredentials::from_file(creds_name);

        auto resp = client.execute(GetAccountBalanceRequest{}, creds);
        if (resp.ok && resp.result) {
            for (const auto& [asset, qty] : resp.result->balances)
                spdlog::info("{:8} {:.8f}", asset, qty);
        } else {
            for (const auto& e : resp.errors)
                spdlog::error("Error: {}", e);
        }
    }
    catch (std::exception& e) {
        spdlog::error("Error: {}", e.what());
    }
    catch (...) {
        spdlog::error("Unknown exception.");
    }

    curl_global_cleanup();
    return 0;
}
