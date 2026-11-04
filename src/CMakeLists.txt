#-------------------------------------------------------------------------------
# Common scaffold — generic, exchange-agnostic ExchangeWsClient implementation.
#
# This is the one piece of non-template WS-client code shared across every
# exchange adapter. It is a genuine extraction from cryptocogs (Step 9): both
# exchange libraries link it PUBLIC, so a Binance-only build never references
# the Kraken library. It needs nlohmann_json transitively, but NOT OpenSSL or
# libcurl — the generic dispatch loop does no crypto or HTTP itself.
#-------------------------------------------------------------------------------

add_library(exchange_common STATIC
    exchange/common/ws_client.cpp
    exchange/common/ws.cpp
    exchange/common/tick_price.cpp
    exchange/common/types.cpp
    exchange/common/credentials_file.cpp
)
add_library(cryptocogs::common ALIAS exchange_common)
target_include_directories(exchange_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)
# nlohmann_json is header-only and FetchContent'd (a non-installable local
# target), so it can't go in our install export. We link it for the BUILD only
# and vendor its headers into our install prefix (see the top-level CMakeLists),
# making the installed package self-contained — consumers need no separate
# nlohmann_json. The INSTALL_INTERFACE include dir already covers the vendored
# copy.
# toml++ backs the shared credentials_file.cpp loader only — it is header-only,
# used in no public header, so link it PRIVATE and BUILD-only (same export guard
# as nlohmann_json) to keep it out of the install export (plan 021).
target_link_libraries(exchange_common
    PUBLIC  $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
    PRIVATE $<BUILD_INTERFACE:tomlplusplus::tomlplusplus>)
# Export the C++23 requirement (cryptocogs#32, plan 029) so downstream find_package(cryptocogs)
# consumers inherit it automatically — the top-level CMAKE_CXX_STANDARD is directory-scoped
# and does not reach the export (plan 017). cxx_std_23 is a floor, not a pin. test_standard_requirement
# holds every library created in this directory to it.
target_compile_features(exchange_common PUBLIC cxx_std_23)

#-------------------------------------------------------------------------------
# Common HTTP transport — generic libcurl-backed CurlHttpClient (plan 010).
#
# Kept separate from exchange_common so the latter stays curl/ssl-free (the WS
# engine needs no HTTP). Both REST adapters link this; a future exchange reuses
# it. It owns the one piece of shared curl code the per-adapter rest_client.cpp
# files used to duplicate.
#-------------------------------------------------------------------------------

add_library(exchange_http STATIC
    exchange/common/http_client.cpp
)
add_library(cryptocogs::http ALIAS exchange_http)
target_include_directories(exchange_http PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(exchange_http PUBLIC
    CURL::libcurl
    $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)
target_compile_features(exchange_http PUBLIC cxx_std_23)

#-------------------------------------------------------------------------------
# Kraken exchange adapter
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_KRAKEN)
    add_library(kraken STATIC
        kraken/types.cpp
        kraken/auth.cpp
        kraken/rest_api.cpp
        kraken/rest_client.cpp
        kraken/ws_api.cpp
        kraken/ws_client.cpp
    )
    add_library(cryptocogs::kraken ALIAS kraken)
    # exchange_http supplies the shared CurlHttpClient (and libcurl, PUBLIC).
    # OpenSSL stays PUBLIC because auth.hpp's inline HMAC/SHA is exposed in a
    # public header.
    target_link_libraries(kraken
        PUBLIC exchange_common exchange_http OpenSSL::SSL OpenSSL::Crypto
    )
    target_compile_features(kraken PUBLIC cxx_std_23)
endif()

#-------------------------------------------------------------------------------
# Binance exchange adapter (Step 4+)
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_BINANCE)
    add_library(binance STATIC
        binance/types.cpp
        binance/auth.cpp
        binance/rest_api.cpp
        binance/rest_client.cpp
        # WS streams + trading API: event/request bodies, frame descriptors,
        # signing, and the conn-based factories live here (plan 016). The
        # generic ExchangeWsClient impl still comes from exchange_common —
        # BinanceStreamClient / BinanceWsApiClient remain bare aliases for it.
        # None of this links ixwebsocket (the URL factory is in the header-only
        # ix_ws_connection.hpp carve-out).
        binance/ws_streams.cpp
        binance/ws_api.cpp
    )
    add_library(cryptocogs::binance ALIAS binance)
    # exchange_http supplies the shared CurlHttpClient (and libcurl, PUBLIC).
    # OpenSSL stays PUBLIC because auth.hpp's inline HMAC/SHA is exposed in a
    # public header.
    target_link_libraries(binance
        PUBLIC exchange_common exchange_http OpenSSL::SSL OpenSSL::Crypto
    )
    target_compile_features(binance PUBLIC cxx_std_23)
endif()

#-------------------------------------------------------------------------------
# Coinbase exchange adapter (plan 018)
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_COINBASE)
    add_library(coinbase STATIC
        coinbase/auth.cpp
        coinbase/types.cpp
        coinbase/rest_api.cpp
        coinbase/rest_client.cpp
        # WebSocket feed: bespoke CoinbaseStreamClient (plan 018 §2 Option A) over
        # the shared IWsConnection. No id-correlated ExchangeWsClient here, and no
        # ixwebsocket link (the example builds the IxWsConnection and passes it in).
        coinbase/ws_streams.cpp
    )
    add_library(cryptocogs::coinbase ALIAS coinbase)
    # exchange_http supplies the shared CurlHttpClient (and libcurl, PUBLIC).
    # OpenSSL stays PUBLIC because auth.hpp's HMAC/base64 helpers are declared in
    # a public header (and reused by the WS user-channel signer).
    target_link_libraries(coinbase
        PUBLIC exchange_common exchange_http OpenSSL::SSL OpenSSL::Crypto
    )
    target_compile_features(coinbase PUBLIC cxx_std_23)
endif()

#-------------------------------------------------------------------------------
# Crypto.com exchange adapter (plan 020)
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_CRYPTOCOM)
    add_library(cryptocom STATIC
        cryptocom/auth.cpp
        cryptocom/types.cpp
        cryptocom/rest_api.cpp
        cryptocom/rest_client.cpp
        # WebSocket: heartbeat-responder decorator + market/user feed (plan 020
        # §2 Option A). Reuses the generic ExchangeWsClient from exchange_common;
        # no ixwebsocket link (the example builds the IxWsConnection and passes
        # it in).
        cryptocom/heartbeat_connection.cpp
        cryptocom/ws.cpp
    )
    add_library(cryptocogs::cryptocom ALIAS cryptocom)
    # exchange_http supplies the shared CurlHttpClient (and libcurl, PUBLIC).
    # OpenSSL stays PUBLIC because auth.hpp's HMAC helpers are declared in a
    # public header (and reused by the WS user-channel signer).
    target_link_libraries(cryptocom
        PUBLIC exchange_common exchange_http OpenSSL::SSL OpenSSL::Crypto
    )
    target_compile_features(cryptocom PUBLIC cxx_std_23)
endif()

#-------------------------------------------------------------------------------
# Install the library targets into the cryptocogsTargets export set (plan 012).
# The export itself + headers + package config are installed from the top-level
# CMakeLists. The two common libs are always present; the adapters only if their
# flag built them.
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_INSTALL)
    set(_cryptocogs_targets exchange_common exchange_http)
    if(TARGET kraken)
        list(APPEND _cryptocogs_targets kraken)
    endif()
    if(TARGET binance)
        list(APPEND _cryptocogs_targets binance)
    endif()
    if(TARGET coinbase)
        list(APPEND _cryptocogs_targets coinbase)
    endif()
    if(TARGET cryptocom)
        list(APPEND _cryptocogs_targets cryptocom)
    endif()
    install(TARGETS ${_cryptocogs_targets}
            EXPORT      cryptocogsTargets
            ARCHIVE     DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
