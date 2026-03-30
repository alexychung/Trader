# Dual-Venue Automated Trading System

## Project Overview
C++20 automated trading system with two phases:
- **Phase 1 (Active)**: Kalshi event trading bot — quantitative probability models on public data to find mispriced prediction market contracts
- **Phase 2 (Planned)**: dYdX v4 perp market making — Avellaneda-Stoikov model on altcoin perpetuals with wider spreads and lower fees than Binance

Both phases share core infrastructure (types, config, logging, event bus, risk management).

### Phase 1: Kalshi Event Trading
- **Venue**: Kalshi (CFTC-regulated prediction market, USD-settled)
- **Edge**: Quantitative probability models (economic data, weather, Fed) vs market mispricing
- **Capital**: $100 starting — contracts cost $0.01-$0.99 each
- **Strategy**: Directional bets on mispriced events + market making on thin books
- **Data Sources**: BLS API (CPI, NFP), NOAA Weather API, FRED API (Fed rates, GDP)
- **Key advantage**: No latency requirements, regulatory safety, wide spreads (2-40%)

### Phase 2: dYdX v4 Perp Market Making (Future)
- **Venue**: dYdX v4 (Cosmos appchain, 14-26 bps altcoin spreads, 1 bps maker fee)
- **Strategy**: Avellaneda-Stoikov with OBI + BTC-lead signals (from existing research)
- **Reuses**: ~88% of Phase 1 core infrastructure
- See `docs/RESEARCH.md` for A-S model research, `docs/VENUE_ANALYSIS.md` for venue comparison

## Tech Stack
- **Language**: C++20 (MSVC on Windows 11)
- **Build**: CMake + vcpkg
- **Dependencies**: Boost.Beast (WebSocket/HTTP), nlohmann/json, spdlog, yaml-cpp, OpenSSL, Google Test
- **Exchange**: Kalshi (demo API first, then production)

## Project Structure
```
src/
  core/       — types, event bus, config, logging, dashboard
  exchange/
    kalshi/   — Kalshi REST + WebSocket clients, auth, market catalog
    dydx/     — (Phase 2, empty for now)
  strategy/
    istrategy.hpp — Strategy interface (shared)
    kalshi/   — probability engine, economic/weather/fed models, edge detection
  risk/       — position tracking, risk limits, kill switch, PnL
tests/        — Google Test unit and integration tests
config/       — YAML configuration files
data/
  cache/      — cached API responses (BLS, NOAA, FRED)
  calibration.db — model prediction tracking (SQLite)
docs/         — RESEARCH.md, VENUE_ANALYSIS.md
```

## Task Management
- Sprint tasks tracked in `.pm/tasks.db` (SQLite)
- **Active sprint**: `kalshi-event-trader` in `.pm/todo/kalshi-event-trader/`
- **Archived**: `hft-foundation` in `.pm/todo/hft-foundation/` (Binance plan, superseded by venue analysis)
- Use `/tdd-agent` for task implementation

## Development Rules
- Always target Kalshi **demo API** during development (`https://demo-api.kalshi.co/trade-api/v2`)
- Never commit API keys — use config files that are gitignored
- Kill switch is the highest priority safety feature — must cancel ALL open orders
- All components communicate via lock-free SPSC event bus
- Risk management: per-market limits + portfolio-level exposure + daily loss limit
- Model calibration: log every prediction for Brier score tracking
- Fee-aware: account for $0.007/contract/side in all edge calculations
- At $100 capital: max 10 contracts per market, $80 max exposure, $20 reserve
