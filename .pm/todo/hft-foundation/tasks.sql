-- Sprint: hft-foundation
-- Multi-Strategy HFT Crypto Trading Bot
-- Altcoin market making + funding rate arbitrage + BTC cross-pair alpha

-- ============================================================
-- Spec 01: Market Data Feed (tasks 1-6)
-- ============================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '01-market-data-feed.md', 1,
 'Project scaffolding — CMake, vcpkg, directory structure',
 'infra', 'infra',
 'CMake project builds with all dependencies (Boost.Beast, nlohmann/json, spdlog, yaml-cpp, OpenSSL, Google Test). Hello world compiles and runs.',
 'Set up the C++20 CMake project with vcpkg package manager. Directory structure:
  src/ (core, exchange, strategy, risk, backtest)
  include/ (public headers)
  tests/ (Google Test)
  config/ (YAML config files)
  docs/ (research)
  Install deps via vcpkg: boost-beast, nlohmann-json, spdlog, yaml-cpp, openssl, gtest.
  Target: Windows 11, MSVC or MinGW.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '01-market-data-feed.md', 2,
 'WebSocket client wrapper using Boost.Beast',
 'network', 'network',
 'Connects to Binance WebSocket, receives raw messages, auto-reconnects on disconnect. Unit test verifies connection lifecycle.',
 'Build a reusable async WebSocket client on Boost.Beast + Boost.Asio with TLS (SSL).
  Features: connect with URI, send text frames, receive callback on message, automatic reconnect with exponential backoff, ping/pong heartbeat.
  Must handle wss:// (TLS) since Binance requires it.
  Must support combined streams URL format for multi-symbol subscription.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '01-market-data-feed.md', 3,
 'JSON message parser for Binance depth/trade/ticker',
 'core', 'core',
 'Parses depthUpdate, trade, and bookTicker messages into typed C++ structs. Unit tests with sample Binance payloads pass.',
 'Create parsers for Binance WebSocket message types:
  - depthUpdate: bids[], asks[], event time, symbol
  - trade: price, qty, buyer/seller maker, trade time
  - bookTicker: best bid/ask price+qty
  Must handle combined stream wrapper format ({"stream":"...","data":{...}}).
  Use nlohmann/json. Zero-copy where possible. Structs should be cache-friendly.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '01-market-data-feed.md', 4,
 'Order book data structure',
 'core', 'core',
 'Sorted order book with O(log n) insert/update/delete. Unit tests for all operations. Benchmark confirms < 1μs per operation.',
 'Implement a local order book using std::map<Price, Quantity> for bids (descending) and asks (ascending).
  Operations: update_level(price, qty) — insert if new, update if exists, remove if qty=0.
  Methods: best_bid(), best_ask(), mid_price(), weighted_mid_price(), spread(), get_depth(n_levels).
  Also: get_obi(n_levels) for order book imbalance computation (used by strategy).
  Must be fast — this is on the hot path.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '01-market-data-feed.md', 5,
 'Multi-symbol order book manager',
 'core', 'core',
 'Manages order books for N symbols. Routes messages by symbol. REST snapshots + incremental updates per Binance spec. Integration test on testnet.',
 'Maintain one OrderBook instance per configured symbol.
  On startup: fetch REST snapshot for each symbol, subscribe to combined depth stream.
  Route incoming depth updates to correct book by symbol field.
  Handle gap detection and re-snapshot per symbol independently.
  Also subscribe to BTC/USDT bookTicker for cross-pair lead signal (no full book needed).');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '01-market-data-feed.md', 6,
 'Binance Futures data feed — funding rates + mark price',
 'network', 'network',
 'Fetches current/predicted funding rates via REST. Subscribes to mark price stream. Unit tests parse responses correctly.',
 'For funding rate arbitrage strategy:
  - GET /fapi/v1/premiumIndex — current and predicted next funding rate per symbol
  - Subscribe to <symbol>@markPrice@1s via futures WebSocket (wss://fstream.binance.com/ws/)
  - Parse funding rate, next funding time, mark price
  - Emit FundingRateEvent to event bus for strategy consumption
  Testnet: https://testnet.binancefuture.com');

-- ============================================================
-- Spec 02: Core Infrastructure (tasks 7-10)
-- ============================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '02-core-infrastructure.md', 7,
 'Common type definitions and constants',
 'core', 'core',
 'All common types compile and are used consistently. No raw doubles for prices in interfaces.',
 'Define core types used across the project:
  Price, Quantity, OrderId, Symbol, Timestamp, Side (Buy/Sell), OrderType (Limit/Market),
  OrderStatus (New, PartiallyFilled, Filled, Canceled, Rejected), TimeInForce (GTC, IOC, FOK).
  Event types: MarketDataEvent, FundingRateEvent, SignalEvent, OrderEvent, FillEvent.
  SignalEvent must support: QuoteUpdate (multiple levels), FundingEntry, FundingExit, NoAction.
  Place in include/common/types.h');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '02-core-infrastructure.md', 8,
 'Configuration loader — multi-strategy YAML parsing',
 'core', 'core',
 'Loads config with multiple strategy instances from YAML. Validates required fields. Unit tests for valid/invalid configs.',
 'Use yaml-cpp to load configuration supporting:
  - Exchange credentials (spot + futures API keys)
  - Multiple strategy definitions (array of strategy configs, each with name + symbol + params)
  - Cross-pair signal config (BTC lead stream)
  - Risk parameters (per-symbol + portfolio-level)
  Strategy config section is a generic key-value map per instance — each strategy type defines its own params.
  Validate: credentials present, symbols non-empty, risk limits sensible.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '02-core-infrastructure.md', 9,
 'Logging setup with spdlog',
 'infra', 'infra',
 'Structured logging works across threads, file rotation works, microsecond timestamps in output.',
 'Configure spdlog with: console sink (colored), rotating file sink (10MB, 3 files).
  Pattern: [timestamp_us] [thread] [level] [component] message.
  Create logger factory: get_logger("market_data"), get_logger("strategy.AVAXUSDT"), get_logger("risk"), etc.
  Per-strategy loggers so you can trace issues on specific symbols.
  Thread-safe by default.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '02-core-infrastructure.md', 10,
 'Lock-free event bus — SPSC queues + event types',
 'core', 'core',
 'Events flow between producer/consumer threads correctly. Benchmark confirms < 100ns per event push/pop.',
 'Implement a lock-free SPSC (single-producer single-consumer) ring buffer queue.
  Template on event type. Fixed capacity (e.g., 65536 slots).
  Operations: try_push(event), try_pop(event&) — both non-blocking.
  Create typed channels: MarketDataBus, SignalBus, ExecutionBus, FundingBus.
  Events must include symbol field so consumers can route to correct strategy instance.');

-- ============================================================
-- Spec 03: Strategy Engine (tasks 11-19)
-- ============================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 11,
 'Define IStrategy interface and input/output structs',
 'core', 'core',
 'IStrategy abstract class compiles. MockStrategy builds against it. Unit test runs with mock.',
 'Define the abstract strategy interface:
  - IStrategy::init(config, logger)
  - IStrategy::on_market_data(MarketDataEvent) — includes symbol
  - IStrategy::on_fill(FillEvent)
  - IStrategy::on_timer(Timestamp)
  - IStrategy::get_parameters() -> ParamMap (for backtesting sweeps)
  - IStrategy::get_symbol() -> Symbol
  - IStrategy::shutdown()
  SignalEvent output supports: QuoteUpdate (N bid/ask levels), FundingEntry/Exit, NoAction.
  Create MockStrategy that logs events.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 12,
 'Strategy factory/registry with multi-instance support',
 'core', 'core',
 'Can register strategies by name, instantiate N instances from config array. Unit test creates 3 market making instances.',
 'StrategyFactory::register("market_making", creator_fn)
  StrategyFactory::register("funding_rate", creator_fn)
  StrategyFactory::create_all(config) -> vector<unique_ptr<IStrategy>>
  Reads strategies[] array from config, creates one instance per entry.
  Each instance gets its own config subsection and logger.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 13,
 'EWMA volatility estimator with configurable halflife',
 'strategy', 'strategy',
 'Produces correct volatility estimates per symbol. Unit tests against known data. Handles startup seeding.',
 'Exponentially weighted moving variance of 1-second mid-price log returns:
  alpha = 1.0 - exp(-1.0 / halflife_seconds)
  ewma_var = (1 - alpha) * ewma_var + alpha * (log_return²)
  sigma = sqrt(ewma_var)
  Support two instances per symbol: short-term (10s halflife) and long-term (300s halflife) for regime detection.
  Seed with first 60 seconds of data before enabling quoting.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 14,
 'Avellaneda-Stoikov quoter — reservation price + optimal spread',
 'strategy', 'strategy',
 'Generates correct bid/ask given mid, inventory, vol, gamma. Unit tests verify inventory skew direction.',
 'Implement the core A-S model:
  r = s - q * γ * σ² * (T - t)     // reservation price
  δ = γ * σ² * (T - t) + (2/γ) * ln(1 + γ/k)   // optimal spread
  bid = r - δ/2, ask = r + δ/2
  Round to tick size. Ensure bid < ask. Skip if spread < 1 tick (negative edge).
  Use weighted mid-price for s. T-t = constant 300s for crypto.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 15,
 'Order book imbalance (OBI) signal computation',
 'strategy', 'strategy',
 'Computes OBI from top N levels, adjusts reservation price. Unit tests for balanced/imbalanced books.',
 'OBI = (bid_qty - ask_qty) / (bid_qty + ask_qty) over top N levels (configurable, default 5).
  Apply to reservation price: r_adjusted = r + obi_weight * OBI * σ
  obi_weight tunable (start 0.3). Scaling by σ keeps proportional to market conditions.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 16,
 'Cross-pair BTC lead signal',
 'strategy', 'strategy',
 'Consumes BTC bookTicker, computes rolling return, adjusts altcoin reservation price. Unit tests verify signal direction.',
 'Subscribe to BTC/USDT bookTicker (via market data bus).
  Compute: btc_return = log(btc_mid_now / btc_mid_Nms_ago) over configurable window (default 500ms).
  Filter: ignore if |btc_return| < min_move_bps (default 5 bps).
  Apply: r_adjusted += btc_lead_weight * btc_return * btc_beta * σ_altcoin
  btc_beta is per-symbol (calibrate from historical data, typically 0.5-1.5).
  When BTC drops, altcoin asks become more aggressive, bids less aggressive.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 17,
 'Multi-level quote generation with geometric size decay',
 'strategy', 'strategy',
 'Generates N levels per side with correct spacing and sizing. Respects tick size per symbol.',
 'Place 3-5 levels per side:
  Level 1: optimal_spread/2 from reservation, 40% of size
  Level 2: +level_spacing_bps, 25% of size
  Level 3: +2*level_spacing_bps, 20% of size
  Spacing wider for altcoins (2-3 bps) than BTC (1 bps).
  All prices rounded to symbol tick size. All quantities rounded to symbol lot size.
  Output: QuoteUpdate with vector of (price, qty) for bid side and ask side.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 18,
 'Inventory penalty amplification and volatility regime detection',
 'strategy', 'strategy',
 'Gamma scales with inventory. Quoting behavior changes in high-vol regimes. Unit tests for edge cases.',
 'Inventory penalty: γ_effective = γ * exp(β * (|q| / q_max)²)
  β = 1.5 default. At 90% inventory → 3.5x gamma → aggressive one-sided quoting.
  Volatility regime: vol_ratio = σ_short / σ_long
  < 0.5: tighten spreads. 0.5-1.5: normal. 1.5-3.0: widen + reduce size. > 3.0: pull quotes.
  Min spread floor: never quote tighter than 2 * maker_fee + safety (20 bps default).');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '03-strategy-engine.md', 19,
 'Funding rate arbitrage strategy',
 'strategy', 'strategy',
 'Monitors funding rates, enters/exits delta-neutral positions. Unit tests for entry/exit logic and edge cases.',
 'Implements IStrategy. Consumes FundingRateEvent from event bus.
  on_timer (every 5 min): check predicted funding for each pair.
  If predicted_funding > min_funding_rate: emit FundingEntry signal (buy spot + short perp).
  If predicted_funding < exit_funding_rate: emit FundingExit signal (sell spot + close perp).
  If basis divergence > max_basis_divergence_bps: emergency exit.
  Position sizing: respect max_notional_per_pair.
  Does NOT use POST_ONLY — uses IOC for speed on entry/exit.');

-- ============================================================
-- Spec 04: Order Execution (tasks 20-25)
-- ============================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '04-order-execution.md', 20,
 'HTTP REST client with HMAC-SHA256 signing (Spot + Futures)',
 'network', 'network',
 'Signed requests pass both Spot and Futures testnet validation. Unit tests verify signature generation.',
 'Build HTTP client for Binance REST API with HMAC-SHA256 signing.
  Must support both base URLs:
  - Spot: https://testnet.binance.vision (prod: https://api.binance.com)
  - Futures: https://testnet.binancefuture.com (prod: https://fapi.binance.com)
  Signing: query_string + timestamp → HMAC-SHA256 with secret → append signature.
  Support GET, POST, DELETE. Handle HTTP errors and rate limit headers (X-MBX-USED-WEIGHT).');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '04-order-execution.md', 21,
 'Spot order placement — POST_ONLY + self-trade prevention',
 'network', 'network',
 'Places LIMIT_MAKER orders with selfTradePreventionMode on testnet. Verifies rejection on would-cross.',
 'Implement spot order operations:
  - place_limit_maker(symbol, side, price, qty) → uses type=LIMIT_MAKER + selfTradePreventionMode=EXPIRE_MAKER
  - cancel_order(symbol, order_id)
  - cancel_all_orders(symbol) — for kill switch
  - get_account_info() → balances per asset
  POST_ONLY is mandatory — never accidentally take liquidity.
  Handle error codes: insufficient balance, invalid price, LIMIT_MAKER rejection (would cross).');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '04-order-execution.md', 22,
 'Atomic cancel-replace for quote updates',
 'network', 'network',
 'Uses cancelReplace endpoint to update quotes in single call. Handles partial fill during replace.',
 'POST /api/v3/order/cancelReplace — primary requoting mechanism.
  Atomically cancels existing order and places new one.
  Eliminates the window where you are unquoted between cancel and new placement.
  Handle edge case: original order partially filled before cancel (need to track partial fill).
  If cancelReplace fails (original already filled), handle gracefully — do not double-place.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '04-order-execution.md', 23,
 'Futures order placement for funding rate strategy',
 'network', 'network',
 'Places and cancels orders on Binance Futures testnet. Supports IOC for funding entries/exits.',
 'Implement futures order operations via /fapi/v1/order:
  - place_futures_order(symbol, side, type, price, qty) — supports LIMIT and MARKET
  - cancel_futures_order(symbol, order_id)
  - get_futures_position(symbol) → position size, entry price, unrealized PnL
  - get_futures_account() → margin balance, available margin
  Funding rate strategy uses IOC orders for speed (not maker-only).
  Same HMAC signing as spot but different base URL.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '04-order-execution.md', 24,
 'User Data Stream — fills across all symbols (spot + futures)',
 'network', 'network',
 'Receives executionReport events for all symbols. Routes to correct strategy. Parses fills correctly.',
 'Spot User Data Stream: single listenKey covers all symbols on the account.
  Futures User Data Stream: separate listenKey via /fapi/v1/listenKey.
  Parse executionReport: symbol, side, order status, fill price, fill qty, commission.
  Route fill events to correct strategy instance by symbol.
  Keepalive both streams every 30 minutes.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '04-order-execution.md', 25,
 'Multi-symbol order state machine and tracking',
 'core', 'core',
 'Tracks open orders per symbol across spot + futures. Reconciles from fill events. Unit tests for multi-symbol scenarios.',
 'Maintain per-symbol maps of OrderId → OrderState.
  State machine: Pending → New → (PartiallyFilled → Filled | Canceled)
  Must distinguish spot vs futures orders (different APIs for cancel/query).
  On fill: update filled qty, compute avg fill price, emit FillEvent with symbol.
  Provide: get_open_orders(symbol), get_all_open_orders(), get_open_order_count_total().');

-- ============================================================
-- Spec 05: Risk Management (tasks 26-30)
-- ============================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '05-risk-management.md', 26,
 'Multi-symbol position tracker — per-symbol + portfolio P&L',
 'core', 'core',
 'Tracks position and P&L per symbol and aggregated across all strategies. Unit tests for multi-symbol fill sequences.',
 'Track per symbol: net inventory, avg entry price, realized P&L, unrealized P&L, fees.
  Track portfolio-wide: total realized P&L, total unrealized P&L, total exposure (sum of |position_usd|).
  Must distinguish market making positions from funding rate positions.
  Funding positions: track spot leg + futures leg separately, net to delta-neutral.
  Daily P&L reset at configurable time (00:00 UTC).
  Drawdown tracking: per-symbol and portfolio-level peak-to-trough.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '05-risk-management.md', 27,
 'Volatility-scaled + portfolio-level position limits',
 'core', 'core',
 'Per-symbol vol-scaled limits + portfolio exposure limit + correlation adjustment. Unit tests for each.',
 'Per-symbol: effective_max = base_max * (target_vol / current_vol)
  Portfolio: total |position_usd| across all symbols < max_portfolio_exposure_usd
  Correlation adjustment: most altcoins are 0.5-0.8 correlated with BTC.
  correlation_adjusted_exposure = Σ |position_usd(i)| * avg_pairwise_correlation
  If corr-adjusted > limit, flag for position reduction starting with least profitable pair.
  Funding positions count at 20% of notional (basis risk only).');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '05-risk-management.md', 28,
 'Pre-trade risk gate — per-symbol AND portfolio checks',
 'core', 'core',
 'Blocks orders failing any check. Unit tests for each limit type including portfolio limits.',
 'Every order must pass ALL checks:
  1. Per-symbol vol-scaled position limit
  2. Portfolio exposure limit (total across all symbols)
  3. Correlation-adjusted exposure limit
  4. Daily loss limit (portfolio-wide)
  5. Max drawdown from peak (portfolio-wide)
  6. Per-symbol order rate limit
  7. Data freshness per symbol (last book update < stale threshold)
  Return: Approved or Rejected(reason). Log all rejections.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '05-risk-management.md', 29,
 'Stale data detector — per-symbol monitoring',
 'core', 'core',
 'Detects stale data per symbol even when WebSocket connected. Cancels that symbol orders. Escalates to kill switch.',
 'Track last_book_update_timestamp per symbol.
  If now - last_update > stale_data_timeout_ms for any symbol:
  1. Cancel all orders for THAT symbol only
  2. Log warning with symbol and staleness duration
  3. If stale for > 3x timeout: trigger full kill switch
  Important: stale data on one symbol should not kill other symbols unless prolonged.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '05-risk-management.md', 30,
 'Kill switch — cancel ALL across ALL symbols, flatten, halt',
 'core', 'core',
 'Cancels all orders on all symbols (spot + futures). Flattens all positions. < 1 second on testnet.',
 'Kill switch must handle multi-strategy multi-symbol state:
  1. Cancel all spot open orders (per symbol: DELETE /api/v3/openOrders)
  2. Cancel all futures open orders (per symbol: DELETE /fapi/v1/allOpenOrders)
  3. Flatten spot positions: aggressive IOC sell/buy per symbol
  4. Flatten futures positions: close all perp positions
  5. Set trading_enabled = false globally
  6. Log: trigger reason, all positions, all pending orders at trigger time
  Must work even if strategy thread is hung (separate watchdog thread).
  NEVER auto-restart.');

-- ============================================================
-- Spec 06: Backtesting (tasks 31-35)
-- ============================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '06-backtesting.md', 31,
 'Historical data downloader — multi-symbol + funding rates',
 'core', 'core',
 'Downloads 30 days of aggTrades + klines for AVAXUSDT, ARBUSDT, BTCUSDT, plus funding rate history.',
 'Fetch data from Binance Public Data (https://data.binance.vision/) and REST API:
  - aggTrades for each target symbol + BTCUSDT (for lead signal backtest)
  - 1s klines for each symbol
  - Historical funding rates: GET /fapi/v1/fundingRate for each futures pair
  Store as CSV in data/<symbol>/ directories.
  Handle rate limiting. Show download progress. Support incremental updates.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '06-backtesting.md', 32,
 'Multi-symbol market data replay engine',
 'core', 'core',
 'Replays data for N symbols simultaneously. Merges chronologically. Routes to correct strategy instance.',
 'Read historical CSV data for all configured symbols.
  Merge all events into single chronological stream (timestamp-sorted).
  Feed through same MarketDataEvent / FundingRateEvent interfaces as live.
  BTC data fed as cross-pair signal events.
  Configurable speed: 1x, 10x, max. Pause/resume. Seek to timestamp.
  Strategy instances should not know if data is live or replayed.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '06-backtesting.md', 33,
 'Queue-aware simulated order matcher',
 'core', 'core',
 'Default conservative fill model. Queue position tracking. Unit tests for fill/no-fill edge cases.',
 'IFillModel interface with pluggable implementations.
  Queue-aware (default): fill only when trade at level is seller-initiated AND
  volume exceeds queue_ahead (assume back of queue).
  queue_ahead = book_qty_at_price_when_order_placed
  Optimistic model (sanity check only): fill when price crosses level.
  Fee model: configurable maker/taker rates (default 0.075%/0.075% with BNB).
  Must work per-symbol with independent fill state.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '06-backtesting.md', 34,
 'Performance metrics with PnL attribution — per-strategy and portfolio',
 'core', 'core',
 'Per-symbol metrics + portfolio aggregated + PnL decomposition. Unit tests with known trade sequences.',
 'Per-symbol metrics: P&L, Sharpe, Sortino, max drawdown, win rate, profit factor, fill rate, adverse selection.
  Portfolio metrics: aggregated across all symbols and strategy types.
  PnL attribution per strategy: spread capture + inventory PnL - fees - adverse selection + signal alpha.
  Separate attribution for market making vs funding rate returns.
  Output: formatted console table + CSV export. Compare per-symbol to identify best/worst pairs.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '06-backtesting.md', 35,
 'Walk-forward optimization harness',
 'core', 'core',
 'Rolling in-sample/out-of-sample optimization per symbol. Reports aggregated OOS performance.',
 'Walk-forward: optimize months 1-3, test month 4, roll forward.
  Run per-symbol (each pair may have different optimal params).
  Parameters to sweep: gamma, obi_weight, btc_lead_weight, level_spacing, min_spread.
  Report: in-sample vs out-of-sample Sharpe per symbol.
  Flag overfitting: if OOS Sharpe < 0.5 * IS Sharpe, warn.
  Output recommended params per symbol.');

-- ============================================================
-- Spec 07: Integration (tasks 36-39)
-- ============================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '07-integration-and-main-loop.md', 36,
 'Main event loop with multi-strategy instance management',
 'core', 'core',
 'All components start/stop correctly. N strategy instances from config. Clean shutdown across all symbols.',
 'Wire all components:
  1. Load config → init logging → create event buses
  2. Connect market data (combined stream for all symbols + BTC + futures)
  3. Build order books per symbol (REST snapshots)
  4. StrategyFactory::create_all(config) → vector of strategy instances
  5. Start execution engine (routes signals from any strategy to correct API)
  6. Connect user data streams (spot + futures)
  7. Start risk monitor watchdog thread
  Main thread: user input (quit, kill switch, status).
  Shutdown: disable all → cancel all orders all symbols → disconnect → exit.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '07-integration-and-main-loop.md', 37,
 'Console dashboard — per-strategy and portfolio stats',
 'core', 'core',
 'Shows per-symbol P&L, positions, spreads; portfolio totals; funding status; risk utilization. Updates without flicker.',
 'Console dashboard sections:
  [Market Making] Per-symbol row: symbol | mid | spread(bps) | our_bid/ask | position | unrealized_pnl | fills
  [Funding Rate] Per-pair row: pair | funding_rate | position | basis | pnl
  [Portfolio] Total P&L | Total exposure | Correlation-adjusted | Risk utilization (% of limits)
  [System] Uptime | msg/sec | active strategies | risk status (green/yellow/red)
  ANSI escape codes for in-place update. Refresh every 100ms.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '07-integration-and-main-loop.md', 38,
 'E2E testnet: altcoin market making on 2+ pairs with BTC lead',
 'e2e', 'e2e',
 'Bot runs on testnet making markets on 2+ altcoin pairs. BTC lead signal active. 10+ minutes, no crashes.',
 'Full integration on Binance testnet:
  1. Market data streaming for 2+ altcoin pairs + BTC bookTicker
  2. Strategy instances generating quotes with OBI + BTC lead signal
  3. Orders placed/cancelled via cancelReplace
  4. Fills tracked, positions updated, P&L computed
  5. Portfolio risk limits enforced
  6. Kill switch tested
  7. Dashboard displays correctly
  8. Graceful shutdown works');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('hft-foundation', '07-integration-and-main-loop.md', 39,
 'E2E testnet: funding rate strategy',
 'e2e', 'e2e',
 'Funding rate strategy monitors rates, enters/exits on futures testnet. Spot+perp legs tracked. P&L correct.',
 'Integration test for funding rate arbitrage:
  1. Funding rate monitoring active for 2+ pairs
  2. When conditions met: spot buy + futures short placed
  3. Position tracked as delta-neutral
  4. Funding payment events processed
  5. Exit when funding drops below threshold
  6. Basis divergence emergency exit tested
  7. Kill switch flattens both legs
  Note: testnet funding may not match mainnet — verify logic even if rates are different.');

-- ============================================================
-- Dependencies
-- ============================================================

-- Task 1 (scaffolding) is the root
INSERT INTO task_dependencies VALUES ('hft-foundation', 2, 'hft-foundation', 1);
INSERT INTO task_dependencies VALUES ('hft-foundation', 3, 'hft-foundation', 1);
INSERT INTO task_dependencies VALUES ('hft-foundation', 4, 'hft-foundation', 1);
INSERT INTO task_dependencies VALUES ('hft-foundation', 5, 'hft-foundation', 2);
INSERT INTO task_dependencies VALUES ('hft-foundation', 5, 'hft-foundation', 3);
INSERT INTO task_dependencies VALUES ('hft-foundation', 5, 'hft-foundation', 4);
INSERT INTO task_dependencies VALUES ('hft-foundation', 6, 'hft-foundation', 2);
INSERT INTO task_dependencies VALUES ('hft-foundation', 6, 'hft-foundation', 3);

-- Core infra
INSERT INTO task_dependencies VALUES ('hft-foundation', 7, 'hft-foundation', 1);
INSERT INTO task_dependencies VALUES ('hft-foundation', 8, 'hft-foundation', 7);
INSERT INTO task_dependencies VALUES ('hft-foundation', 9, 'hft-foundation', 1);
INSERT INTO task_dependencies VALUES ('hft-foundation', 10, 'hft-foundation', 7);

-- Strategy: interface needs types + event bus
INSERT INTO task_dependencies VALUES ('hft-foundation', 11, 'hft-foundation', 7);
INSERT INTO task_dependencies VALUES ('hft-foundation', 11, 'hft-foundation', 10);
INSERT INTO task_dependencies VALUES ('hft-foundation', 12, 'hft-foundation', 11);
INSERT INTO task_dependencies VALUES ('hft-foundation', 12, 'hft-foundation', 8);
-- Strategy implementation needs interface + order book (for OBI)
INSERT INTO task_dependencies VALUES ('hft-foundation', 13, 'hft-foundation', 11);
INSERT INTO task_dependencies VALUES ('hft-foundation', 14, 'hft-foundation', 13);
INSERT INTO task_dependencies VALUES ('hft-foundation', 15, 'hft-foundation', 4);
INSERT INTO task_dependencies VALUES ('hft-foundation', 15, 'hft-foundation', 11);
INSERT INTO task_dependencies VALUES ('hft-foundation', 16, 'hft-foundation', 11);
INSERT INTO task_dependencies VALUES ('hft-foundation', 16, 'hft-foundation', 5);
INSERT INTO task_dependencies VALUES ('hft-foundation', 17, 'hft-foundation', 14);
INSERT INTO task_dependencies VALUES ('hft-foundation', 17, 'hft-foundation', 15);
INSERT INTO task_dependencies VALUES ('hft-foundation', 18, 'hft-foundation', 13);
INSERT INTO task_dependencies VALUES ('hft-foundation', 18, 'hft-foundation', 17);
-- Funding rate strategy needs interface + futures data
INSERT INTO task_dependencies VALUES ('hft-foundation', 19, 'hft-foundation', 11);
INSERT INTO task_dependencies VALUES ('hft-foundation', 19, 'hft-foundation', 6);

-- Execution: needs types + websocket
INSERT INTO task_dependencies VALUES ('hft-foundation', 20, 'hft-foundation', 7);
INSERT INTO task_dependencies VALUES ('hft-foundation', 20, 'hft-foundation', 2);
INSERT INTO task_dependencies VALUES ('hft-foundation', 21, 'hft-foundation', 20);
INSERT INTO task_dependencies VALUES ('hft-foundation', 22, 'hft-foundation', 21);
INSERT INTO task_dependencies VALUES ('hft-foundation', 23, 'hft-foundation', 20);
INSERT INTO task_dependencies VALUES ('hft-foundation', 24, 'hft-foundation', 2);
INSERT INTO task_dependencies VALUES ('hft-foundation', 24, 'hft-foundation', 7);
INSERT INTO task_dependencies VALUES ('hft-foundation', 25, 'hft-foundation', 21);
INSERT INTO task_dependencies VALUES ('hft-foundation', 25, 'hft-foundation', 23);
INSERT INTO task_dependencies VALUES ('hft-foundation', 25, 'hft-foundation', 24);

-- Risk: needs order tracking
INSERT INTO task_dependencies VALUES ('hft-foundation', 26, 'hft-foundation', 25);
INSERT INTO task_dependencies VALUES ('hft-foundation', 27, 'hft-foundation', 26);
INSERT INTO task_dependencies VALUES ('hft-foundation', 27, 'hft-foundation', 13);
INSERT INTO task_dependencies VALUES ('hft-foundation', 28, 'hft-foundation', 27);
INSERT INTO task_dependencies VALUES ('hft-foundation', 29, 'hft-foundation', 5);
INSERT INTO task_dependencies VALUES ('hft-foundation', 29, 'hft-foundation', 7);
INSERT INTO task_dependencies VALUES ('hft-foundation', 30, 'hft-foundation', 21);
INSERT INTO task_dependencies VALUES ('hft-foundation', 30, 'hft-foundation', 23);
INSERT INTO task_dependencies VALUES ('hft-foundation', 30, 'hft-foundation', 28);

-- Backtesting: needs REST client + strategy + event bus
INSERT INTO task_dependencies VALUES ('hft-foundation', 31, 'hft-foundation', 20);
INSERT INTO task_dependencies VALUES ('hft-foundation', 32, 'hft-foundation', 10);
INSERT INTO task_dependencies VALUES ('hft-foundation', 32, 'hft-foundation', 31);
INSERT INTO task_dependencies VALUES ('hft-foundation', 33, 'hft-foundation', 12);
INSERT INTO task_dependencies VALUES ('hft-foundation', 33, 'hft-foundation', 32);
INSERT INTO task_dependencies VALUES ('hft-foundation', 34, 'hft-foundation', 33);
INSERT INTO task_dependencies VALUES ('hft-foundation', 35, 'hft-foundation', 34);

-- Integration: needs everything
INSERT INTO task_dependencies VALUES ('hft-foundation', 36, 'hft-foundation', 5);
INSERT INTO task_dependencies VALUES ('hft-foundation', 36, 'hft-foundation', 10);
INSERT INTO task_dependencies VALUES ('hft-foundation', 36, 'hft-foundation', 12);
INSERT INTO task_dependencies VALUES ('hft-foundation', 36, 'hft-foundation', 18);
INSERT INTO task_dependencies VALUES ('hft-foundation', 36, 'hft-foundation', 19);
INSERT INTO task_dependencies VALUES ('hft-foundation', 36, 'hft-foundation', 30);
INSERT INTO task_dependencies VALUES ('hft-foundation', 37, 'hft-foundation', 36);
INSERT INTO task_dependencies VALUES ('hft-foundation', 38, 'hft-foundation', 37);
INSERT INTO task_dependencies VALUES ('hft-foundation', 39, 'hft-foundation', 37);
