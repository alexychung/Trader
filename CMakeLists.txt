cmake_minimum_required(VERSION 3.20)
project(trader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC-specific flags
if(MSVC)
    add_compile_options(/W4 /permissive- /bigobj)
    add_compile_definitions(_WIN32_WINNT=0x0A00)  # Windows 10
endif()

# Find dependencies via vcpkg
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(GTest CONFIG REQUIRED)

# Core library
add_library(trader_core STATIC
    src/core/config.cpp
    src/core/state_store.cpp
    src/core/staleness_gate.cpp
    src/core/alert_manager.cpp
    src/core/http.cpp
    src/exchange/kalshi/auth.cpp
    src/exchange/kalshi/rest_client.cpp
    src/exchange/kalshi/ws_client.cpp
    src/exchange/kalshi/kalshi_exchange.cpp
    src/exchange/kalshi/order_manager.cpp
    src/exchange/mock_exchange.cpp
    src/strategy/kalshi/calibration.cpp
    src/strategy/nba/win_probability.cpp
    src/strategy/nba/kalshi_nba_parser.cpp
    src/strategy/nba/nba_score_feed.cpp
    src/strategy/nba/nba_strategy.cpp
    src/strategy/nba/resolution_lag_strategy.cpp
    src/feeds/sharp_book_provider.cpp
    src/feeds/injury_news_feed.cpp
    src/core/dashboard.cpp
    src/risk/risk_manager.cpp
    src/risk/kill_switch.cpp
    src/risk/cluster_limiter.cpp
    src/backtest/settled_markets_fetcher.cpp
    src/backtest/nba_pbp_fetcher.cpp
    src/backtest/replay_engine.cpp
    src/backtest/kalshi_candle_provider.cpp
)
target_include_directories(trader_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(trader_core PUBLIC
    Boost::system
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Main executable
add_executable(trader src/main.cpp)
target_link_libraries(trader PRIVATE trader_core)

# Demo smoke test — places and cancels one order on Kalshi demo.
add_executable(trader_demo_smoke src/tools/demo_smoke.cpp)
target_link_libraries(trader_demo_smoke PRIVATE trader_core)

# Catalog inspector — enumerates ticker-series prefixes currently live on Kalshi.
add_executable(trader_catalog_inspect src/tools/catalog_inspect.cpp)
target_link_libraries(trader_catalog_inspect PRIVATE trader_core)

# Portfolio check — read-only: fetch balance, recent fills, positions.
add_executable(trader_portfolio_check src/tools/portfolio_check.cpp)
target_link_libraries(trader_portfolio_check PRIVATE trader_core)

# NBA smoke test — fetches today's scoreboard from cdn.nba.com and prints
# games + our arcsine WP + expected Kalshi ticker. No Kalshi auth required.
add_executable(trader_nba_smoke src/tools/nba_smoke.cpp)
target_link_libraries(trader_nba_smoke PRIVATE trader_core)

# Backtest CLI — replays historical NBA PBP through both strategies and
# reports aggregate metrics. See src/backtest/backtest_main.cpp.
add_executable(trader_backtest src/backtest/backtest_main.cpp)
target_link_libraries(trader_backtest PRIVATE trader_core)

# Tests
enable_testing()
add_executable(trader_tests
    tests/test_scaffold.cpp
    tests/test_types.cpp
    tests/test_interfaces.cpp
    tests/test_config.cpp
    tests/test_event_bus.cpp
    tests/test_kalshi_auth.cpp
    tests/test_kalshi_fees.cpp
    tests/test_kalshi_encoding.cpp
    tests/test_ws_reconnect.cpp
    tests/test_kalshi_rest.cpp
    tests/test_kalshi_ws.cpp
    tests/test_kalshi_exchange.cpp
    tests/test_order_manager.cpp
    tests/test_alert_manager.cpp
    tests/test_settled_markets_fetcher.cpp
    tests/test_kill_switch.cpp
    tests/test_calibration.cpp
    tests/test_dashboard.cpp
    tests/test_bugfixes.cpp
    tests/test_risk_manager.cpp
    tests/test_state_store.cpp
    tests/test_staleness_gate.cpp
    tests/test_cluster_limiter.cpp
    tests/test_mock_exchange.cpp
    tests/test_position_seeding.cpp
    tests/test_nba_win_probability.cpp
    tests/test_nba_kalshi_parser.cpp
    tests/test_nba_score_feed.cpp
    tests/test_nba_strategy.cpp
    tests/test_resolution_lag_strategy.cpp
)
target_link_libraries(trader_tests PRIVATE
    trader_core
    GTest::gtest
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(trader_tests)
