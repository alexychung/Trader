# 01 — Project Scaffolding + Shared Core

**Status**: Not Started

---

## Scope

Bootstrap the C++20 project and build the shared core infrastructure that both Kalshi (Phase 1) and dYdX (Phase 2) will use. This is the foundation everything else depends on.

**This spec covers**:
- CMake + vcpkg build system
- Directory structure for dual-venue architecture
- Common types (Price, Quantity, OrderId, Timestamp, etc.)
- YAML configuration loader
- spdlog structured logging
- Lock-free SPSC event bus for inter-thread communication

**Out of scope**:
- Exchange-specific connectivity → `02-kalshi-exchange.md`
- Strategy logic → `04-probability-engine.md`
- Risk management → `06-risk-and-calibration.md`

---

## What's Done

| Item | Status |
|------|--------|
| CMake + vcpkg setup | Not started |
| Common types | Not started |
| Config + logging + event bus | Not started |

---

## Technical Details

### Directory Structure

```
src/
  core/
    types.hpp              # Price, Quantity, Timestamp, OrderId, Side, etc.
    config.hpp / config.cpp # YAML loader
    logging.hpp / logging.cpp
    event_bus.hpp           # SPSC ring buffer template
  exchange/
    iexchange.hpp           # Exchange interface (Kalshi implements, dYdX later)
    kalshi/                 # Phase 1
    dydx/                   # Phase 2 (empty for now)
  strategy/
    istrategy.hpp           # Strategy interface
    kalshi/                 # Phase 1
  risk/
    risk_manager.hpp
    kill_switch.hpp
    pnl_tracker.hpp
  main.cpp
include/                    # Public headers (mirrors src/)
tests/
config/
  config.yaml              # Example config (gitignored production copy)
  config.example.yaml      # Template checked in
```

### Dependencies (vcpkg.json)

```json
{
  "name": "trader",
  "version": "0.1.0",
  "dependencies": [
    "boost-beast",
    "boost-asio",
    "nlohmann-json",
    "spdlog",
    "yaml-cpp",
    "openssl",
    "gtest"
  ]
}
```

### Common Types

```cpp
// Core types shared across all venues
using Price = double;
using Quantity = double;
using Timestamp = std::chrono::system_clock::time_point;
using OrderId = std::string;

enum class Side { Buy, Sell };
enum class OrderStatus { Pending, Open, Filled, Cancelled, Rejected };

// Kalshi-specific (but in core since prediction markets may expand)
using Probability = double;  // [0.0, 1.0]
using ContractPrice = double; // [0.0, 1.0] — maps to probability

struct MarketEvent {
    std::string ticker;
    std::string category;      // "economics", "weather", "fed"
    Timestamp close_time;
    Timestamp resolution_time;
};
```

### SPSC Event Bus

Lock-free single-producer single-consumer ring buffer. Template parameterized on message type. Cache-line aligned to avoid false sharing.

```cpp
template<typename T, size_t Capacity = 4096>
class SPSCQueue {
    // alignas(64) to prevent false sharing
    // std::atomic<size_t> head_, tail_
    // std::array<T, Capacity> buffer_
    bool try_push(const T& item);
    bool try_pop(T& item);
};
```

### Config Structure

```yaml
# config.example.yaml
venue: "kalshi"
mode: "paper"  # "paper" or "live"

kalshi:
  api_base: "https://demo-api.kalshi.co/trade-api/v2"
  ws_url: "wss://demo-api.kalshi.co/trade-api/ws/v2"
  api_key_file: "secrets/kalshi_api_key.pem"  # gitignored

# Phase 2 (unused for now)
dydx:
  api_base: "https://indexer.dydx.trade/v4"

risk:
  max_position_per_market: 10
  max_total_exposure: 80
  max_daily_loss: 15
  kill_switch_loss: 30

logging:
  level: "info"
  file: "logs/trader.log"
  console: true
```

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | CMake scaffolding + vcpkg + directory structure + hello world | `cmake --build` succeeds, all deps link, hello world runs, Google Test runs a trivial test |
| 2 | Common types + IExchange interface + IStrategy interface | Types compile, interfaces defined with pure virtual methods, unit tests verify type sizes and enum conversions |
| 3 | Config loader + spdlog logging + SPSC event bus | Config loads from YAML, logs to file + console, SPSC push/pop works across two threads in unit test, event bus benchmark shows < 100ns per message |
