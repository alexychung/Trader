cmake_minimum_required(VERSION 3.20)
project(trader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC-specific flags
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_WIN32_WINNT=0x0A00)  # Windows 10
endif()

# Find dependencies via vcpkg
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(GTest CONFIG REQUIRED)

# Core library (shared across all venues)
add_library(trader_core STATIC
    src/core/config.cpp
    src/exchange/kalshi/auth.cpp
    src/exchange/kalshi/rest_client.cpp
    src/exchange/kalshi/ws_client.cpp
    src/exchange/kalshi/kalshi_exchange.cpp
    src/strategy/kalshi/weather_feed.cpp
    src/strategy/kalshi/econ_feed.cpp
    src/strategy/kalshi/probability_engine.cpp
    src/strategy/kalshi/edge_detector.cpp
    src/strategy/kalshi/data_feed_manager.cpp
    src/strategy/kalshi/econ_models.cpp
    src/strategy/kalshi/market_filter.cpp
    src/risk/risk_manager.cpp
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

# Tests
enable_testing()
add_executable(trader_tests
    tests/test_main.cpp
    tests/test_scaffold.cpp
    tests/test_types.cpp
    tests/test_interfaces.cpp
    tests/test_config.cpp
    tests/test_event_bus.cpp
    tests/test_kalshi_auth.cpp
    tests/test_kalshi_rest.cpp
    tests/test_kalshi_ws.cpp
    tests/test_kalshi_exchange.cpp
    tests/test_weather_feed.cpp
    tests/test_econ_feed.cpp
    tests/test_probability_engine.cpp
    tests/test_edge_detector.cpp
    tests/test_data_feed_manager.cpp
    tests/test_econ_models.cpp
    tests/test_market_filter.cpp
    tests/test_risk_manager.cpp
)
target_link_libraries(trader_tests PRIVATE
    trader_core
    GTest::gtest
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(trader_tests)
