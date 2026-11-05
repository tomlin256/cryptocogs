// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "exchange/kraken/rest_client.hpp"

using namespace exchange::kraken::rest;

int main(int argc, char* argv[])
{
    bool curl_ready = false;  // true once curl_global_init has run: the handlers below then clean up as the normal path does
    try {
        CLI::App app{"Kraken private REST example — obtain a WebSocket session token"};

        std::string creds_name = "default";
        app.add_option("-c,--credentials", creds_name,
                       "Credentials profile name (TOML file at ~/.kraken/<name>)")
            ->capture_default_str();

        CLI11_PARSE(app, argc, argv);

        curl_global_init(CURL_GLOBAL_ALL);
        curl_ready = true;

        try {
            KrakenRestClient client;
            auto creds = KrakenCredentials::from_file(creds_name);

            auto resp = client.execute(GetWebSocketsTokenRequest{}, creds);
            if (resp.ok && resp.result) {
                spdlog::info("token:   {}", resp.result->token);
                spdlog::info("expires: {}", resp.result->expires);
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
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        if (curl_ready) curl_global_cleanup();
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        if (curl_ready) curl_global_cleanup();
        return EXIT_FAILURE;
    }
}
