cmake_minimum_required(VERSION 3.20)
project(coinbase-orderbook VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies (provided by Conan)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# Main executable
add_executable(coinbase-orderbook
    src/main.cpp
    src/websocket_client.cpp
    src/orderbook.cpp
    src/tui_renderer.cpp
)

target_include_directories(coinbase-orderbook PRIVATE include)

target_link_libraries(coinbase-orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_compile_options(coinbase-orderbook PRIVATE
    -Wall -Wextra -Wpedantic -O2
)

# Benchmarks
find_package(benchmark REQUIRED)

add_executable(orderbook-bench
    bench/orderbook_bench.cpp
    src/orderbook.cpp
)

target_include_directories(orderbook-bench PRIVATE include)

target_link_libraries(orderbook-bench PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
)

target_compile_options(orderbook-bench PRIVATE
    -Wall -Wextra -Wpedantic -O2
)

# Tests
find_package(GTest REQUIRED)
include(GoogleTest)
enable_testing()

add_executable(orderbook-test
    test/flat_orderbook_test.cpp
    test/orderbook_test.cpp
    test/seqlock_test.cpp
    src/orderbook.cpp
)

target_include_directories(orderbook-test PRIVATE include)

target_link_libraries(orderbook-test PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

target_compile_options(orderbook-test PRIVATE
    -Wall -Wextra -Wpedantic -O2
)

gtest_discover_tests(orderbook-test)
