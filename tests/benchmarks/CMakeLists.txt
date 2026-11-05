# This project's own warning flags (plan 036): the first act of the directory. The root has fetched google benchmark before it adds
# this directory, so only the benchmarks below get them. The root CMakeLists.txt has the reasons, and the rule for a new directory.
add_compile_options(${_cryptocogs_warning_options})

if(CRYPTOCOGS_BUILD_KRAKEN)
  add_executable(kraken_benchmarks bench_smoke.cpp bench_kraken_rest.cpp bench_kraken_ws.cpp bench_tick_price.cpp)
  target_link_libraries(kraken_benchmarks kraken benchmark::benchmark_main)
endif()

if(CRYPTOCOGS_BUILD_BINANCE)
  add_executable(binance_benchmarks bench_binance_ws.cpp)
  target_link_libraries(binance_benchmarks binance benchmark::benchmark_main)
endif()

if(CRYPTOCOGS_BUILD_COINBASE)
  add_executable(coinbase_benchmarks bench_coinbase_ws.cpp)
  target_link_libraries(coinbase_benchmarks coinbase benchmark::benchmark_main)
endif()

if(CRYPTOCOGS_BUILD_CRYPTOCOM)
  add_executable(cryptocom_benchmarks bench_cryptocom_ws.cpp)
  target_link_libraries(cryptocom_benchmarks cryptocom benchmark::benchmark_main)
endif()
