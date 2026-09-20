include(FetchContent)

#-------------------------------------------------------------------------------
# backward-cpp (stack traces on crash for example programs)
#-------------------------------------------------------------------------------

FetchContent_Declare(
  backward
  GIT_REPOSITORY https://github.com/bombela/backward-cpp.git
  GIT_TAG        v1.6
)
FetchContent_MakeAvailable(backward)

add_library(example_backward OBJECT examples/backward_init.cpp)
add_backward(example_backward)

#-------------------------------------------------------------------------------
# Find spdlog
#-------------------------------------------------------------------------------

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(spdlog)

#-------------------------------------------------------------------------------
# CLI11
#-------------------------------------------------------------------------------

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(CLI11)

#-------------------------------------------------------------------------------
# Kraken example apps (+ the legacy KAPI reference library they depend on)
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_KRAKEN)
    # Legacy KAPI library (lives in examples/, not installed/exported)
    add_library(kapi STATIC examples/kapi.cpp)
    target_include_directories(kapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    target_link_libraries(kapi CURL::libcurl OpenSSL::SSL)

    add_executable(private_rest examples/private_rest.cpp)
    target_link_libraries(private_rest kraken spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable(get_balance examples/get_balance.cpp)
    target_link_libraries(get_balance kraken spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (public_rest examples/public_rest.cpp)
    target_link_libraries(public_rest kraken spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (public_ws examples/public_ws.cpp)
    target_link_libraries(public_ws kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (private_ws examples/private_ws.cpp)
    target_link_libraries(private_ws kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (kraken_example examples/kraken_example.cpp)
    target_link_libraries(kraken_example kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (ws_client_example examples/ws_client_example.cpp)
    target_link_libraries(ws_client_example kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (rest_client_example examples/rest_client_example.cpp)
    target_link_libraries(rest_client_example kraken spdlog::spdlog CLI11::CLI11 example_backward)
endif()

#-------------------------------------------------------------------------------
# Binance example apps
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_BINANCE)
    add_executable(binance_rest_client_example examples/binance/binance_rest_client_example.cpp)
    target_link_libraries(binance_rest_client_example
        binance spdlog::spdlog CLI11::CLI11 example_backward
    )

    # The generic ExchangeWsClient impl comes from exchange_common, reached
    # transitively via binance's PUBLIC link — no kraken reference.
    add_executable(binance_ws_client_example examples/binance/binance_ws_client_example.cpp)
    target_link_libraries(binance_ws_client_example
        binance ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )

    add_executable(binance_ws_api_example examples/binance/binance_ws_api_example.cpp)
    target_link_libraries(binance_ws_api_example
        binance ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )
endif()

#-------------------------------------------------------------------------------
# Coinbase example apps
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_COINBASE)
    add_executable(coinbase_rest_client_example examples/coinbase/coinbase_rest_client_example.cpp)
    target_link_libraries(coinbase_rest_client_example
        coinbase spdlog::spdlog CLI11::CLI11 example_backward
    )

    # The bespoke CoinbaseStreamClient reaches the IWsConnection interface via
    # coinbase's PUBLIC link to exchange_common; the real transport
    # (IxWsConnection) needs ixwebsocket linked here.
    add_executable(coinbase_ws_client_example examples/coinbase/coinbase_ws_client_example.cpp)
    target_link_libraries(coinbase_ws_client_example
        coinbase ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )
endif()

#-------------------------------------------------------------------------------
# Crypto.com example apps
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_CRYPTOCOM)
    add_executable(cryptocom_rest_client_example examples/cryptocom/cryptocom_rest_client_example.cpp)
    target_link_libraries(cryptocom_rest_client_example
        cryptocom spdlog::spdlog CLI11::CLI11 example_backward
    )

    # Reuses the generic ExchangeWsClient (via cryptocom's PUBLIC link to
    # exchange_common); the real transport (IxWsConnection) needs ixwebsocket here.
    add_executable(cryptocom_ws_client_example examples/cryptocom/cryptocom_ws_client_example.cpp)
    target_link_libraries(cryptocom_ws_client_example
        cryptocom ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )
endif()

#-------------------------------------------------------------------------------
# Unit tests
#-------------------------------------------------------------------------------

add_subdirectory(unit)