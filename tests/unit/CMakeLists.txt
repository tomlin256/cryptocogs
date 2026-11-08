include(GoogleTest)

#-------------------------------------------------------------------------------
# Common (exchange-agnostic) unit tests — built regardless of exchange flags
#-------------------------------------------------------------------------------

# TickPrice exact-decimal serialisation (exchange::TickPrice lives in exchange_common)
add_executable(test_tick_price test_tick_price.cpp)
target_link_libraries(test_tick_price exchange_common GTest::gtest_main)
gtest_discover_tests(test_tick_price)

# Shared TOML credentials-file loader (exchange::rest::read_toml_credentials,
# compiled into exchange_common). Like TickPrice, it is exchange-agnostic and
# builds in any flag config.
add_executable(test_credentials_file test_credentials_file.cpp)
target_link_libraries(test_credentials_file exchange_common GTest::gtest_main)
gtest_discover_tests(test_credentials_file)

# The shared MockWsConnection test double (mock_ws_connection.hpp). Its outbound log is read from a second thread by
# Execute.BlocksUntilResponse, so what it promises about threads is tested here (plan tsan-races-in-the-tests, #27). Header-only;
# exchange-agnostic.
add_executable(test_mock_ws_connection test_mock_ws_connection.cpp)
target_link_libraries(test_mock_ws_connection exchange_common GTest::gtest_main)
gtest_discover_tests(test_mock_ws_connection)

# ASSERT_HAS_VALUE (assert_has_value.hpp), the optional check that clang-tidy's bugprone-unchecked-optional-access can follow
# (plan clear-clang-tidy-findings, #46). Header-only and free of exchange types, so it links gtest alone and builds in any flag
# configuration.
add_executable(test_assert_has_value test_assert_has_value.cpp)
target_link_libraries(test_assert_has_value GTest::gtest_main)
gtest_discover_tests(test_assert_has_value)

# IxWsConnection against a loopback ix::WebSocketServer (plan transport-callback-lifetimes, #39): what its destructor promises
# about its callbacks and its I/O thread. Real sockets on 127.0.0.1, no network, no sleeps. The real transport is header-only, so
# the test links ixwebsocket itself, as the examples do; it is exchange-agnostic and builds in any flag configuration.
add_executable(test_ix_ws_connection test_ix_ws_connection.cpp)
target_link_libraries(test_ix_ws_connection exchange_common ixwebsocket GTest::gtest_main)
gtest_discover_tests(test_ix_ws_connection)

# make_curl_header_list (curl_header_list.hpp, compiled into exchange_http): the "Key: value" list that CurlHttpClient::perform hands
# to libcurl, and what it does when libcurl cannot grow the list (plan curl-header-list-append-failure, #48). Real libcurl lists
# and a replaceable append function, no network; exchange-agnostic, so it builds in any flag configuration.
add_executable(test_curl_header_list test_curl_header_list.cpp)
target_link_libraries(test_curl_header_list exchange_http GTest::gtest_main)
gtest_discover_tests(test_curl_header_list)

# CurlHttpClient::perform against a loopback ix::HttpServer (plan curl-header-list-append-failure): what a request carries over a
# real libcurl transfer. Real sockets on 127.0.0.1, no network, no sleeps. Like test_ix_ws_connection it links ixwebsocket for the
# server; exchange-agnostic, so it builds in any flag configuration.
add_executable(test_http_client test_http_client.cpp)
target_link_libraries(test_http_client exchange_http ixwebsocket GTest::gtest_main)
gtest_discover_tests(test_http_client)

#-------------------------------------------------------------------------------
# Kraken unit tests
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_KRAKEN)
    add_executable(kraken_unit_tests
        test_rest_requests.cpp
        test_rest_responses.cpp
        test_client.cpp
        test_signature.cpp
        test_kraken_auth.cpp
    )
    target_link_libraries(kraken_unit_tests
        kapi
        kraken
        GTest::gtest_main
    )
    gtest_discover_tests(kraken_unit_tests)

    # WebSocket client unit tests (no network I/O – mock connection only)
    add_executable(test_ws_client test_ws_client.cpp)
    target_link_libraries(test_ws_client kraken GTest::gtest_main)
    gtest_discover_tests(test_ws_client)

    # WebSocket response parsing unit tests (sample JSON → from_json verification)
    add_executable(test_ws_responses test_ws_responses.cpp)
    target_link_libraries(test_ws_responses kraken GTest::gtest_main)
    gtest_discover_tests(test_ws_responses)

    # What a bare AddOrderRequest, SubscribeRequest and UnsubscribeRequest hold (their enum members have default
    # initialisers), and the underlying type of the two enums in ws_api.hpp (plan clear-clang-tidy-findings, #46)
    add_executable(test_kraken_ws_api_defaults test_kraken_ws_api_defaults.cpp)
    target_link_libraries(test_kraken_ws_api_defaults kraken GTest::gtest_main)
    gtest_discover_tests(test_kraken_ws_api_defaults)

    # OrderType to_string/from_string unit tests (generic vs. Kraken wire format)
    add_executable(test_order_type test_order_type.cpp)
    target_link_libraries(test_order_type kraken GTest::gtest_main)
    gtest_discover_tests(test_order_type)

    # WsReconnectSession unit tests (no network I/O; header-only — links no
    # exchange library, just the generic include tree).
    add_executable(test_ws_reconnect_session test_ws_reconnect_session.cpp)
    target_include_directories(test_ws_reconnect_session PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(test_ws_reconnect_session GTest::gtest_main)
    gtest_discover_tests(test_ws_reconnect_session)
endif()

#-------------------------------------------------------------------------------
# Binance unit tests
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_BINANCE)
    # Binance auth + REST infrastructure unit tests (Step 4)
    add_executable(test_binance_auth test_binance_auth.cpp)
    target_link_libraries(test_binance_auth binance GTest::gtest_main)
    gtest_discover_tests(test_binance_auth)

    # Binance enum converter unit tests (Step 6.1)
    add_executable(test_binance_types test_binance_types.cpp)
    target_link_libraries(test_binance_types binance GTest::gtest_main)
    gtest_discover_tests(test_binance_types)

    # Binance REST public market-data unit tests (Step 5.1)
    add_executable(test_binance_rest_requests test_binance_rest_requests.cpp)
    target_link_libraries(test_binance_rest_requests binance GTest::gtest_main)
    gtest_discover_tests(test_binance_rest_requests)

    add_executable(test_binance_rest_responses test_binance_rest_responses.cpp)
    target_link_libraries(test_binance_rest_responses binance GTest::gtest_main)
    gtest_discover_tests(test_binance_rest_responses)

    # Binance signed client round-trip unit tests (Step 6.5 — mock performer only)
    add_executable(test_binance_client test_binance_client.cpp)
    target_link_libraries(test_binance_client binance GTest::gtest_main)
    gtest_discover_tests(test_binance_client)

    # Binance WebSocket stream + WS-API unit tests (Steps 7/8 — mock connection
    # only). The generic ExchangeWsClient impl comes from exchange_common via
    # binance's PUBLIC link — no kraken reference.
    add_executable(test_binance_ws_client test_binance_ws_client.cpp)
    target_link_libraries(test_binance_ws_client binance GTest::gtest_main)
    gtest_discover_tests(test_binance_ws_client)
endif()

#-------------------------------------------------------------------------------
# Coinbase unit tests (plan coinbase-exchange-adapter)
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_COINBASE)
    # Coinbase auth — base64/HMAC-SHA256 primitives + CB-ACCESS-* header injection
    add_executable(test_coinbase_auth test_coinbase_auth.cpp)
    target_link_libraries(test_coinbase_auth coinbase GTest::gtest_main)
    gtest_discover_tests(test_coinbase_auth)

    # Coinbase enum converters + parse_coinbase_response REST envelope
    add_executable(test_coinbase_types test_coinbase_types.cpp)
    target_link_libraries(test_coinbase_types coinbase GTest::gtest_main)
    gtest_discover_tests(test_coinbase_types)

    # Coinbase REST request building + response parsing
    add_executable(test_coinbase_rest_requests test_coinbase_rest_requests.cpp)
    target_link_libraries(test_coinbase_rest_requests coinbase GTest::gtest_main)
    gtest_discover_tests(test_coinbase_rest_requests)

    add_executable(test_coinbase_rest_responses test_coinbase_rest_responses.cpp)
    target_link_libraries(test_coinbase_rest_responses coinbase GTest::gtest_main)
    gtest_discover_tests(test_coinbase_rest_responses)

    # Coinbase signed client round-trip (mock performer only — no network)
    add_executable(test_coinbase_client test_coinbase_client.cpp)
    target_link_libraries(test_coinbase_client coinbase GTest::gtest_main)
    gtest_discover_tests(test_coinbase_client)

    # Coinbase WebSocket stream client (MockWsConnection only — no network).
    # Links only coinbase (reaches the IWsConnection interface via exchange_common).
    add_executable(test_coinbase_ws_client test_coinbase_ws_client.cpp)
    target_link_libraries(test_coinbase_ws_client coinbase GTest::gtest_main)
    gtest_discover_tests(test_coinbase_ws_client)
endif()

#-------------------------------------------------------------------------------
# Crypto.com unit tests (plan cryptocom-exchange-adapter)
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_CRYPTOCOM)
    # Crypto.com auth — HMAC-SHA256/hex primitives, params_to_str serialisation,
    # and CryptoComCredentials::sign() digest composition.
    add_executable(test_cryptocom_auth test_cryptocom_auth.cpp)
    target_link_libraries(test_cryptocom_auth cryptocom GTest::gtest_main)
    gtest_discover_tests(test_cryptocom_auth)

    # Crypto.com enum converters + parse_cryptocom_response REST envelope
    add_executable(test_cryptocom_types test_cryptocom_types.cpp)
    target_link_libraries(test_cryptocom_types cryptocom GTest::gtest_main)
    gtest_discover_tests(test_cryptocom_types)

    # Crypto.com REST request building + response parsing
    add_executable(test_cryptocom_rest_requests test_cryptocom_rest_requests.cpp)
    target_link_libraries(test_cryptocom_rest_requests cryptocom GTest::gtest_main)
    gtest_discover_tests(test_cryptocom_rest_requests)

    add_executable(test_cryptocom_rest_responses test_cryptocom_rest_responses.cpp)
    target_link_libraries(test_cryptocom_rest_responses cryptocom GTest::gtest_main)
    gtest_discover_tests(test_cryptocom_rest_responses)

    # Crypto.com signed REST client round-trip (mock performer only — no network)
    add_executable(test_cryptocom_client test_cryptocom_client.cpp)
    target_link_libraries(test_cryptocom_client cryptocom GTest::gtest_main)
    gtest_discover_tests(test_cryptocom_client)

    # Crypto.com WebSocket: heartbeat decorator + subscribe/ack/push lifecycle
    # (MockWsConnection only — no network). Links only cryptocom (reaches the
    # IWsConnection interface via exchange_common).
    add_executable(test_cryptocom_ws_client test_cryptocom_ws_client.cpp)
    target_link_libraries(test_cryptocom_ws_client cryptocom GTest::gtest_main)
    gtest_discover_tests(test_cryptocom_ws_client)
endif()
