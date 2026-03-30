# 07 — Integration + Live Trading

**Status**: Not Started

---

## Scope

Wire all components together into a running system. Build the console dashboard for monitoring. Run end-to-end tests on Kalshi demo (paper trading), then deploy live with $100.

**This spec covers**:
- Main event loop connecting all threads
- Console dashboard showing positions, PnL, model performance
- E2E test on Kalshi demo API (paper trading)
- Go-live checklist and first $100 deployment

**Out of scope**:
- Individual component logic (all previous specs)
- Phase 2 dYdX integration (future sprint)

---

## What's Done

| Item | Status |
|------|--------|
| Main loop + dashboard | Not started |
| E2E paper trade | Not started |
| Live deployment | Not started |

---

## Technical Details

### Main Event Loop

```cpp
int main() {
    // 1. Load config
    Config config = Config::load("config/config.yaml");

    // 2. Initialize shared components
    auto event_bus = std::make_shared<EventBus>();
    auto risk_manager = std::make_shared<RiskManager>(config.risk);
    auto calibration = std::make_shared<CalibrationLogger>("data/calibration.db");

    // 3. Initialize Kalshi exchange
    auto kalshi = std::make_shared<KalshiExchange>(config.kalshi);

    // 4. Initialize data feeds
    auto data_feeds = std::make_shared<DataFeedManager>(config.feeds);

    // 5. Initialize probability engine
    auto prob_engine = std::make_shared<ProbabilityEngine>();
    prob_engine->register_model("economics", std::make_unique<EconModel>());
    prob_engine->register_model("weather", std::make_unique<WeatherModel>());
    prob_engine->register_model("fed", std::make_unique<FedRateModel>());

    // 6. Initialize strategy
    auto strategy = std::make_shared<KalshiEventStrategy>(
        kalshi, prob_engine, risk_manager, calibration, config.strategy
    );

    // 7. Start threads
    //    Thread 1: Data feeds (Kalshi WS + public data polling)
    //    Thread 2: Strategy (probability + edge detection + signal generation)
    //    Thread 3: Execution (order placement + fill tracking)
    //    Thread 4: Risk + Calibration (monitoring + logging)

    // 8. Run console dashboard on main thread
    Dashboard dashboard(risk_manager, strategy, calibration);
    dashboard.run();  // Blocks until quit signal

    // 9. Shutdown: kill switch → cancel orders → save state → exit
}
```

### Console Dashboard

Simple terminal UI refreshing every 5 seconds:

```
╔══════════════════════════════════════════════════════════╗
║  KALSHI EVENT TRADER          Mode: PAPER   Uptime: 2h  ║
╠══════════════════════════════════════════════════════════╣
║  BALANCE: $98.50    EXPOSURE: $42.00    AVAILABLE: $56.50║
║  DAILY P&L: +$1.30  TOTAL P&L: +$3.50                   ║
╠══════════════════════════════════════════════════════════╣
║  POSITIONS (5 active)                                    ║
║  CPI-25APR-T3.5  YES  5 @ $0.32  now $0.38  +$0.30     ║
║  NFP-25APR-T200  NO   3 @ $0.45  now $0.42  -$0.09     ║
║  TEMP-NYC-25APR  YES  8 @ $0.20  now $0.25  +$0.40     ║
║  FED-25MAY-HOLD  YES 10 @ $0.72  now $0.75  +$0.30     ║
║  GDP-25Q1-T2.0   NO   4 @ $0.55  now $0.50  -$0.20     ║
╠══════════════════════════════════════════════════════════╣
║  OPEN ORDERS (2)                                         ║
║  CPI-25MAY-T3.0  BUY YES  5 @ $0.28  (edge: 8.2%)     ║
║  TEMP-CHI-25APR  BUY NO   3 @ $0.60  (edge: 6.1%)     ║
╠══════════════════════════════════════════════════════════╣
║  MODEL PERFORMANCE                                       ║
║  Brier Score: 0.182 (115 predictions)                    ║
║  Economics: 0.165 (42)  Weather: 0.201 (38)  Fed: 0.178 ║
║  Calibration: slightly overconfident in 60-80% bucket    ║
╠══════════════════════════════════════════════════════════╣
║  RISK STATUS: OK     Kill Switch: ARMED                  ║
║  [Q]uit  [K]ill  [R]efresh  [C]alibration report        ║
╚══════════════════════════════════════════════════════════╝
```

### E2E Paper Trade Test

Run the full system against Kalshi demo API for 48-72 hours:

**Success criteria**:
1. System stays connected without crashes for 48+ hours
2. Data feeds refresh on schedule
3. Probability models produce estimates for active markets
4. At least 5 trades are placed automatically
5. Risk limits are respected (no over-position)
6. Settlements are detected and capital is recycled
7. Calibration records are logged correctly
8. Kill switch activates and recovers correctly when tested manually

### Go-Live Checklist

Before deploying $100:

```
□ Paper trade ran 48+ hours without crash
□ At least 10 paper trades placed with positive expected value
□ Brier score < 0.25 on paper trades (better than random)
□ Kill switch tested and working
□ Config switched from demo to live API
□ API key created with minimum permissions (trade + read, no withdrawal)
□ Risk parameters set for $100 capital
□ Logging configured to capture every trade for post-analysis
□ Start with max 3 active markets, expand after 1 week
```

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 19 | Main event loop + multi-thread wiring | All components instantiated, threads launched, SPSC buses connected. System starts, runs for 60 seconds in paper mode without crash, then shuts down cleanly. |
| 20 | Console dashboard | Terminal UI shows balance, positions, PnL, open orders, model performance, risk status. Refreshes every 5s. Keyboard input for quit, kill switch, calibration report. Runs correctly when wired to live data feed. |
| 21 | E2E paper trade + go-live deployment | Full system runs against Kalshi demo API for 48+ hours. Trades placed, settlements detected, calibration logged. Go-live checklist completed. System deployed with $100 on live Kalshi. |
