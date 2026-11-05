// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/kraken/rest_client.hpp"
#include "exchange/kraken/ws_api.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

using namespace exchange::kraken::rest;

int main(int argc, char* argv[])
{
   bool curl_ready = false;  // true once curl_global_init has run: the handlers below then clean up as the normal path does
   try {
      CLI::App app{"Kraken private WebSocket example — subscribe to balances channel"};

      std::string creds_name = "default";
      app.add_option("-c,--credentials", creds_name,
                     "Credentials profile name (TOML file at ~/.kraken/<name>)")
          ->capture_default_str();

      CLI11_PARSE(app, argc, argv);

      curl_global_init(CURL_GLOBAL_ALL);
      curl_ready = true;

      // we need a token to subscribe to private channels, so we need to call a private method first
      KrakenRestClient client;
      auto creds = KrakenCredentials::from_file(creds_name);
      auto resp = client.execute(GetWebSocketsTokenRequest{}, creds);
      if (!resp.ok || !resp.result) {
         for (const auto& e : resp.errors)
            spdlog::error("Error: {}", e);
         curl_global_cleanup();
         return 1;
      }
      std::string token = resp.result->token;
      spdlog::info("Token: {}", token);

      ix::WebSocket webSocket;
      webSocket.setUrl("wss://ws-auth.kraken.com/v2");

      webSocket.setOnMessageCallback(
         [&](const ix::WebSocketMessagePtr& msg)
         {
            if (msg->type == ix::WebSocketMessageType::Open)
            {
               spdlog::info("ws opened");

               exchange::kraken::ws::SubscribeRequest req;
               req.req_id = 10;
               req.channel = exchange::kraken::ws::SubscribeChannel::Balances;
               req.token   = token;
               webSocket.send(req.to_json().dump());
            }
            else if (msg->type == ix::WebSocketMessageType::Close)
            {
               spdlog::info("ws closed");
            }
            else if (msg->type == ix::WebSocketMessageType::Error)
            {
               spdlog::error("ws error: {}", msg->errorInfo.reason);
            }
            else if (msg->type == ix::WebSocketMessageType::Message)
            {
               auto j = nlohmann::json::parse(msg->str);
               switch (exchange::kraken::ws::identify_message(j))
               {
                  case exchange::kraken::ws::MessageKind::SubscribeResponse:
                  {
                     auto r = exchange::kraken::ws::SubscribeResponse::from_json(j);
                     spdlog::info("subscribe {}({}): {}",
                        r.method,
                        r.req_id.value_or(-1),
                        r.success ? "success" : "failure");
                     break;
                  }
                  case exchange::kraken::ws::MessageKind::Balances:
                  {
                     auto bals = exchange::kraken::ws::BalancesMessage::from_json(j);
                     for (const auto& b : bals.data)
                        spdlog::info("Balance {}: {:.8f} (hold: {:.8f})", b.asset, b.balance, b.hold_trade);
                     break;
                  }
                  default:
                     spdlog::debug("ws message: {}", msg->str);
                     break;
               }
            }
         }
      );

      webSocket.start();
      std::this_thread::sleep_for(std::chrono::seconds(10));
      webSocket.stop();

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
