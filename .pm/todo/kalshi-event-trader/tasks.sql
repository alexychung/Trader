-- Sprint: kalshi-event-trader
-- Phase 1 of dual-venue strategy (Kalshi event trading)
-- 21 tasks, 7 specs, targeting 6-8 weeks to live trading

-- =============================================================================
-- Spec 01: Project Scaffolding + Shared Core
-- =============================================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '01-scaffolding-and-core.md', 1,
 'CMake scaffolding + vcpkg + directory structure',
 'infra', 'cmake,vcpkg',
 'cmake --build succeeds, all deps link (Boost.Beast, nlohmann/json, spdlog, yaml-cpp, OpenSSL, GTest), hello world runs, Google Test runs a trivial test',
 'Create CMakeLists.txt (root + src + tests), vcpkg.json with all dependencies, full directory structure for dual-venue architecture (src/core, src/exchange/kalshi, src/exchange/dydx (empty), src/strategy/kalshi, src/risk, tests/). Include config.example.yaml template. Build target is MSVC on Windows 11.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '01-scaffolding-and-core.md', 2,
 'Common types + IExchange + IStrategy interfaces',
 'core', 'cpp',
 'Types compile, interfaces defined with pure virtual methods, unit tests verify type sizes and enum conversions',
 'Define shared types in src/core/types.hpp: Price, Quantity, Probability, ContractPrice, Timestamp, OrderId, Side, OrderStatus, MarketEvent. Define IExchange interface (connect, disconnect, place_order, cancel_order, get_positions, get_balance). Define IStrategy interface (on_market_update, on_data_signal, on_fill, on_settlement, generate_signals). These interfaces must support both Kalshi (Phase 1) and dYdX (Phase 2).');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '01-scaffolding-and-core.md', 3,
 'Config loader + spdlog logging + SPSC event bus',
 'core', 'cpp',
 'Config loads from YAML file with nested sections (kalshi, risk, logging, feeds). Spdlog logs to file + console with configurable level. SPSC push/pop works across two threads in unit test with < 100ns per message.',
 'Implement YAML config loader using yaml-cpp (src/core/config.hpp/.cpp). Initialize spdlog with rotating file sink + console sink (src/core/logging.hpp/.cpp). Implement lock-free SPSC ring buffer template (src/core/event_bus.hpp) with cache-line alignment (alignas(64)), std::atomic head/tail. Unit test: producer thread pushes 1M messages, consumer thread pops all, verify none lost and throughput > 10M msg/sec.');

-- =============================================================================
-- Spec 02: Kalshi Exchange Connectivity
-- =============================================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '02-kalshi-exchange.md', 4,
 'Kalshi REST client + RSA-PSS auth + market catalog',
 'network', 'cpp,http',
 'Authenticates with Kalshi demo API using RSA-PSS signing, fetches market list, filters by category, caches markets locally. Unit tests mock HTTP responses. Integration test hits demo API and returns real market data.',
 'Build REST client using Boost.Beast HTTPS (src/exchange/kalshi/rest_client.hpp/.cpp). Implement RSA-PSS authentication: sign (timestamp_ms + method + path) with SHA256/PSS padding using OpenSSL EVP_PKEY API. Send three headers: KALSHI-ACCESS-KEY, KALSHI-ACCESS-TIMESTAMP (milliseconds!), KALSHI-ACCESS-SIGNATURE (base64). Implement market catalog: GET /markets with category filter, parse JSON into KalshiMarket structs (note: prices are strings like "0.6500", quantities are count_fp strings like "10.00"). Orderbook returns bids only in ascending order. Cache with 5-min TTL. Demo base URL: https://demo-api.kalshi.co/trade-api/v2. Production URL: https://api.elections.kalshi.com/trade-api/v2');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '02-kalshi-exchange.md', 5,
 'Kalshi WebSocket client + real-time updates',
 'network', 'cpp,websocket',
 'Connects to Kalshi demo WebSocket, subscribes to orderbook and trade channels, maintains local best bid/ask per market. Unit test verifies message parsing. Integration test connects to demo WS and receives updates.',
 'Build WebSocket client using Boost.Beast (src/exchange/kalshi/ws_client.hpp/.cpp). Connect to demo WS endpoint: wss://demo-api.kalshi.co/trade-api/ws/v2. Authenticate using same RSA-PSS headers. Subscribe to channels: orderbook_delta (public), ticker, trade, fill (private), user_orders (private). Note: even public channels require auth. Parse incoming JSON messages and publish MarketUpdate events to SPSC bus. Handle reconnection with exponential backoff. Maintain local cache of best bid/ask per subscribed market.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '02-kalshi-exchange.md', 6,
 'Order placement + fill tracking + settlement monitoring',
 'network', 'cpp,http',
 'Places limit orders on demo API, tracks fills via WebSocket + REST polling, detects settlements and updates position state. Integration test places and cancels an order on demo.',
 'Implement order management: POST /portfolio/orders with JSON body using string prices ("0.6500") and count_fp ("5.00"). Always set post_only: true for maker fees (4x lower than taker). DELETE /portfolio/orders/{id} for cancel. Compute maker fee per order: ceil(0.0175 * contracts * price * (1-price)). Track order state machine: Pending → Open → Filled/Cancelled. Poll GET /portfolio/settlements to detect resolved markets. On settlement: update position PnL, free capital, publish Settlement event to bus. Implement KalshiExchange class implementing IExchange interface.');

-- =============================================================================
-- Spec 03: Public Data Feeds
-- =============================================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '03-public-data-feeds.md', 7,
 'Open-Meteo ensemble + NWS weather clients',
 'network', 'cpp,http',
 'Fetches GFS 31-member + ECMWF 51-member ensemble data for all 6 Kalshi station coordinates via Open-Meteo API. Parses into EnsembleForecast structs. Fetches NWS point forecasts. Caches with 6h TTL. Unit tests verify parsing against saved sample responses. Integration test fetches real ensemble data and verifies member count.',
 'Build Open-Meteo client (src/strategy/kalshi/open_meteo_feed.hpp/.cpp). Primary weather data source — clean JSON, no API key. Endpoint: https://ensemble-api.open-meteo.com/v1/ensemble with params: latitude, longitude, models=gfs_seamless+ecmwf_ifs025, hourly=temperature_2m, forecast_days=7, temperature_unit=fahrenheit. Parse response: extract temperature_2m_member01 through member31 (GFS) and member51 (ECMWF) for each hour. Compute daily high per member. Station coords: KNYC(40.71,-74.01), KORD(41.97,-87.91), KMIA(25.80,-80.29), KLAX(33.94,-118.41), KDEN(39.86,-104.67), KAUS(30.20,-97.67). Also build NWS client for supplementary point forecasts (api.weather.gov, no key needed, include User-Agent). Cache both with 6h TTL.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '03-public-data-feeds.md', 8,
 'BLS + FRED API clients with caching',
 'network', 'cpp,http',
 'Fetches CPI (CUUR0000SA0), NFP (CES0000000001), Fed funds rate (FEDFUNDS), GDPNow, gasoline prices. Parses into DataSignal structs. Caches with TTL. Unit tests verify parsing against sample responses. Integration test fetches real data.',
 'Build BLS API client (src/strategy/kalshi/econ_feed.hpp/.cpp): GET timeseries data for CPI-U, NFP, unemployment rate. Parse BLS JSON format (nested Results.series[].data[]). Build FRED API client: GET /fred/series/observations for FEDFUNDS, GDPNOW, GASREGW (weekly gasoline — CPI energy input), T10Y2Y. Parse FRED JSON. Both clients: cache responses as JSON files in data/cache/ with configurable TTL (BLS: until next release, FRED: 24h), return DataSignal structs.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '03-public-data-feeds.md', 9,
 'Data feed manager + scheduling + event publishing',
 'core', 'cpp',
 'Coordinates all data feeds on configurable schedule. Publishes DataSignal events to SPSC bus. Handles fetch failures gracefully (use cached data, log warning). Unit test verifies scheduling and event publishing.',
 'Build DataFeedManager (src/strategy/kalshi/data_feed_manager.hpp/.cpp). Runs on configurable schedule: Open-Meteo ensemble every 6h (aligned with 00Z/06Z/12Z/18Z model runs), NWS every 6h, BLS on release days, FRED daily. On each fetch: call appropriate client, publish DataSignal/EnsembleForecast events to strategy thread via SPSC bus. On failure: log warning, use most recent cached value, retry with backoff. Track last_fetch_time and next_fetch_time per source for dashboard display. Also includes GEFS reforecast downloader: bulk download historical ensemble data from NOAA NCEI for backtesting weather model against 20+ years of data.');

-- =============================================================================
-- Spec 04: Probability Engine
-- =============================================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '04-probability-engine.md', 10,
 'Bayesian probability framework + model interface',
 'strategy', 'cpp',
 'IProbabilityModel interface defined with estimate(), confidence(), rationale(). ProbabilityEngine registers models per category and dispatches markets to appropriate model. Unit test verifies mock model produces probability in [0,1] with rationale string.',
 'Define IProbabilityModel interface (src/strategy/kalshi/probability_engine.hpp). Implement WeatherEnsembleModel as primary model: takes EnsembleForecast data, counts members above contract threshold for raw probability, applies bias correction (rolling 30-60 day model-vs-actual residuals per station), applies forecast horizon scaling (1d=±2F, 3d=±4F, 7d=±6F sigma), blends GFS (weight 0.4) + ECMWF (weight 0.6) when both available. Implement ProbabilityEngine registry that dispatches markets to appropriate model by category. Backtest against at least 30 days of historical weather data with Brier score computation.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '04-probability-engine.md', 11,
 'Economic models: CPI, NFP, Fed rate',
 'strategy', 'cpp',
 'CPI model estimates P(CPI > threshold) using component-based approach. NFP model uses ADP + claims regression. Fed rate model mirrors FedWatch probabilities. Unit tests verify each model against at least 3 historical events with known outcomes.',
 'Implement three IProbabilityModel subclasses (src/strategy/kalshi/econ_models.hpp/.cpp). CPI model: bottom-up estimation using BLS component weights (shelter 33%, food 13.5%, energy 7%, core goods 20%, core services 25%). Use Cleveland Fed Nowcast as strong prior, adjust with latest gasoline prices (FRED GASREGW). Convert point estimate to probability via calibrated forecast error sigma from last 24 months. NFP model: linear regression on ADP + jobless claims trend + ISM employment sub-index + prior NFP. Fed rate model: use CME FedWatch-implied probabilities as baseline (Kalshi diverges 3-54pp from FedWatch — but note Fed is most efficient Kalshi category per UCD study). All models: fee-aware edge using actual formula ceil(0.0175 * C * P * (1-P)), not flat fee.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '04-probability-engine.md', 12,
 'Weather model + cross-market consistency checker',
 'strategy', 'cpp',
 'Temperature model calibrates NWS forecast error by horizon (1-day ±2F, 3-day ±4F, 7-day ±6F). Consistency checker detects logical violations. Unit tests verify probability against historical NWS accuracy. Consistency test with synthetic prices.',
 'Implement ConsistencyChecker in ProbabilityEngine: for groups of related markets (same event, different thresholds), verify monotonicity (P(temp>80) must be <= P(temp>75)). Flag violations with magnitude for arbitrage detection. Implement reforecast backtester: loads historical GEFS ensemble data (from NOAA NCEI reforecast archive or Open-Meteo historical API) + historical station observations (GHCN/Iowa Environmental Mesonet). Runs weather ensemble model against full history computing Brier score + calibration curve (10 buckets: 0-10%, 10-20%, ..., 90-100%). MUST achieve Brier < 0.20 on historical weather data before system goes live with real money. This is the key validation gate.');

-- =============================================================================
-- Spec 05: Trading Strategy
-- =============================================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '05-trading-strategy.md', 13,
 'Edge detection + fractional Kelly position sizing',
 'strategy', 'cpp',
 'Compares model probability vs market price. Calculates edge per market. Kelly sizing with configurable fraction (default 25%). Unit tests verify edge calc for known inputs, Kelly sizing clamps at max_position_per_market.',
 'Implement EdgeDetector (src/strategy/kalshi/edge_detector.hpp/.cpp). For each market: compute fee-aware EV: net_EV = model_prob * $1.00 - market_price - ceil(0.0175 * 1 * price * (1-price)). Compute edge_yes and edge_no. If max edge > min_edge_threshold (configurable, default 5 cents) AND confidence > min_confidence (default 0.5), generate TradeSignal. Always use post_only (maker) orders — never taker. Position sizing: Kelly f* = (b*p - q)/b where p=model_prob, b=(1-price)/price. Apply kelly_fraction (0.25 conservative, 0.50 moderate) and clamp to max_position_per_market. Avoid sub-$0.10 contracts (favorite-longshot bias: -60% average return per UCD study).');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '05-trading-strategy.md', 14,
 'Market making mode + market selection filter',
 'strategy', 'cpp',
 'Quotes both sides when spread > min_spread_to_mm (8 cents) and confidence > 0.7. Market filter selects by category, time to resolution, volume, spread, price range. Unit tests verify MM quotes dont cross, filter correctly includes/excludes.',
 'Implement MarketMaker mode in edge detector: when spread is wide (>8c) and model confidence is high, compute bid = model_prob - half_spread, ask = model_prob + half_spread. Verify bid < ask and (ask-bid) > 2*maker_fee where maker_fee = ceil(0.0175 * 1 * mid_price * (1-mid_price)). Always post_only. Implement MarketFilter (src/strategy/kalshi/market_filter.hpp/.cpp): primary category = weather (daily resolution), secondary = economics (monthly). Accept weather markets 1-7 days out, econ 1-30 days. Volume > 50, spread > $0.03, price $0.15-$0.85 (skip sub-$0.10 contracts per UCD favorite-longshot bias). Skip: sports, crypto, stocks, company events. Return filtered list sorted by edge magnitude.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '05-trading-strategy.md', 15,
 'KalshiEventStrategy full integration',
 'strategy', 'cpp',
 'IStrategy implementation wires probability engine + edge detection + sizing + market filter into 30-60s tick loop. Publishes TradeSignals to event bus. Integration test runs one full tick with mock market data and verifies correct signal generation.',
 'Implement KalshiEventStrategy (src/strategy/kalshi/event_strategy.hpp/.cpp) implementing IStrategy. Main tick loop (30-60s configurable): 1) refresh market prices from latest WS data, 2) check for new DataSignals from feeds, 3) re-run probability models if new data, 4) scan all filtered markets for edge, 5) generate TradeSignals with Kelly sizing, 6) check risk limits via RiskManager, 7) publish approved signals to execution thread via SPSC bus. Handle on_fill and on_settlement callbacks to update positions.');

-- =============================================================================
-- Spec 06: Risk Management + Calibration
-- =============================================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '06-risk-and-calibration.md', 16,
 'Risk manager + position tracking + pre-trade gate',
 'core', 'cpp',
 'RiskManager tracks positions, exposure, daily PnL. Pre-trade gate rejects orders violating limits. Capital recycling on settlement. Unit tests verify all 6 risk checks with edge cases (at limit, over limit, exactly at limit).',
 'Implement RiskManager (src/risk/risk_manager.hpp/.cpp). Track positions per market (quantity, avg_cost, current_value, unrealized_pnl). Compute total_exposure (sum of position costs), available_capital (balance - exposure), daily_pnl (reset at midnight UTC). Pre-trade gate checks: 1) per-market position < max, 2) total exposure < max, 3) daily PnL > -max_loss, 4) active markets < max, 5) available capital > order cost + reserve, 6) kill switch not active. On settlement: update position as settled, credit/debit balance, free capital.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '06-risk-and-calibration.md', 17,
 'Kill switch',
 'core', 'cpp',
 'Separate watchdog monitors risk state. Cancels all open orders on trigger. Blocks new trades. Logs state snapshot. Unit test verifies trigger conditions (loss threshold, connectivity loss, manual). Order cancellation walks all open orders.',
 'Implement KillSwitch (src/risk/kill_switch.hpp/.cpp). Runs as separate watchdog check on risk monitor thread. Triggers: 1) daily loss > kill_switch_loss, 2) manual (atomic bool set by dashboard keyboard input), 3) API connectivity lost > 60s, 4) 3 consecutive order placement failures. On trigger: iterate all open orders and cancel via exchange, set kill_active flag, log full state snapshot (balance, positions, PnL, trigger reason, timestamp). Require explicit reset (config flag or restart) to resume trading. Note: do NOT auto-flatten Kalshi positions — binary contracts resolve on their own.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '06-risk-and-calibration.md', 18,
 'Calibration logger + Brier score + calibration curve',
 'core', 'cpp,sqlite',
 'SQLite-backed calibration DB. Logs every trade prediction. Computes Brier score per category. Generates calibration curve data (10 buckets). Unit test inserts mock records, verifies Brier = 0.0 for perfect predictions and 0.25 for random.',
 'Implement CalibrationLogger (src/strategy/kalshi/calibration.hpp/.cpp). Create SQLite DB (data/calibration.db) with schema: market_ticker, category, trade_time, model_probability, market_price, edge, side, quantity, entry_price, maker_fee (computed via ceil(0.0175*C*P*(1-P))), resolution_time (nullable), outcome (nullable), pnl (nullable). Log on every trade. On settlement: update resolution_time, outcome, pnl. Compute Brier score: mean((predicted - actual)^2) per category and overall. Target: < 0.20 (Polymarket achieves 0.187 across 2847 markets). Decompose into Reliability + Resolution + Uncertainty. Calibration curve: bucket predictions into 10 bins (0-10%, 10-20%, ...), compute avg_prediction and actual_frequency per bin. Return CalibrationBucket structs for dashboard.');

-- =============================================================================
-- Spec 07: Integration + Live Trading
-- =============================================================================

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '07-integration-and-live.md', 19,
 'Main event loop + multi-thread wiring',
 'core', 'cpp',
 'All components instantiated from config, threads launched, SPSC buses connected. System starts in paper mode, runs 60 seconds without crash, shuts down cleanly with proper thread join.',
 'Implement main.cpp: load config, instantiate all components (KalshiExchange, DataFeedManager, ProbabilityEngine with 3 models, KalshiEventStrategy, RiskManager, CalibrationLogger), create SPSC buses for inter-thread communication (data→strategy, strategy→execution, execution→risk). Launch 4 threads. Handle SIGINT/SIGTERM for clean shutdown: set running flag, join threads, flush logs. Graceful shutdown must cancel open orders before exit.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '07-integration-and-live.md', 20,
 'Console dashboard',
 'frontend', 'cpp',
 'Terminal UI shows balance, positions, PnL, open orders, model Brier score, risk status. Refreshes every 5 seconds. Keyboard: Q=quit, K=kill switch, R=force refresh, C=calibration report.',
 'Implement Dashboard (src/core/dashboard.hpp/.cpp). Render to stdout using ANSI escape codes for formatting (or simple text if ANSI not supported on Windows). Sections: header (mode, uptime), balance/exposure/PnL, active positions table (ticker, side, qty, cost, current, unrealized PnL), open orders table (ticker, side, qty, price, edge%), model performance (Brier score overall + per category), risk status (OK/WARNING/KILLED). Non-blocking keyboard input: check for keypress on each refresh cycle. Q triggers clean shutdown, K triggers kill switch.');

INSERT INTO tasks (sprint, spec, task_num, title, type, skills, done_when, description) VALUES
('kalshi-event-trader', '07-integration-and-live.md', 21,
 'E2E paper trade + go-live with $100',
 'e2e', 'testing',
 'System runs against Kalshi demo API for 48+ hours without crash. At least 5 trades placed automatically. Settlements detected. Calibration logged. Kill switch tested manually. Go-live checklist completed. $100 deployed.',
 'Run full system against Kalshi demo API (https://demo-api.kalshi.co/trade-api/v2) for 48-72 hours. Verify: 1) no crashes or memory leaks, 2) data feeds refresh on schedule, 3) probability models produce estimates for available markets, 4) trades placed when edge exceeds threshold, 5) risk limits respected, 6) settlements detected and capital recycled, 7) calibration records logged correctly, 8) kill switch triggers and recovers on manual test. After paper trade passes, complete go-live checklist: switch to production API, create API key with trade+read permissions (no withdrawal), set risk params for $100, start with max 3 active markets.');

-- =============================================================================
-- Dependencies
-- =============================================================================

-- Task 2 (types/interfaces) depends on Task 1 (scaffolding)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 2, 'kalshi-event-trader', 1);

-- Task 3 (config/logging/bus) depends on Task 1
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 3, 'kalshi-event-trader', 1);

-- Task 4 (Kalshi REST) depends on Task 2 (types) and Task 3 (config/logging)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 4, 'kalshi-event-trader', 2),
('kalshi-event-trader', 4, 'kalshi-event-trader', 3);

-- Task 5 (Kalshi WS) depends on Task 4 (REST client for auth)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 5, 'kalshi-event-trader', 4);

-- Task 6 (orders) depends on Task 4 (REST) and Task 5 (WS)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 6, 'kalshi-event-trader', 4),
('kalshi-event-trader', 6, 'kalshi-event-trader', 5);

-- Task 7 (BLS/FRED feeds) depends on Task 3 (config/logging/bus)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 7, 'kalshi-event-trader', 3);

-- Task 8 (weather feed) depends on Task 3
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 8, 'kalshi-event-trader', 3);

-- Task 9 (feed manager) depends on Task 7 and Task 8
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 9, 'kalshi-event-trader', 7),
('kalshi-event-trader', 9, 'kalshi-event-trader', 8);

-- Task 10 (probability framework) depends on Task 2 (types/interfaces)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 10, 'kalshi-event-trader', 2);

-- Task 11 (econ models) depends on Task 10 (framework) and Task 7 (BLS/FRED data)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 11, 'kalshi-event-trader', 10),
('kalshi-event-trader', 11, 'kalshi-event-trader', 7);

-- Task 12 (weather model + consistency) depends on Task 10 and Task 8
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 12, 'kalshi-event-trader', 10),
('kalshi-event-trader', 12, 'kalshi-event-trader', 8);

-- Task 13 (edge detection) depends on Task 10 (probability engine)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 13, 'kalshi-event-trader', 10);

-- Task 14 (MM mode + filter) depends on Task 13 (edge detection) and Task 4 (market catalog)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 14, 'kalshi-event-trader', 13),
('kalshi-event-trader', 14, 'kalshi-event-trader', 4);

-- Task 15 (strategy integration) depends on Task 11, 12 (models), Task 14 (MM + filter), Task 6 (orders)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 15, 'kalshi-event-trader', 11),
('kalshi-event-trader', 15, 'kalshi-event-trader', 12),
('kalshi-event-trader', 15, 'kalshi-event-trader', 14),
('kalshi-event-trader', 15, 'kalshi-event-trader', 6);

-- Task 16 (risk manager) depends on Task 2 (types) and Task 3 (config)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 16, 'kalshi-event-trader', 2),
('kalshi-event-trader', 16, 'kalshi-event-trader', 3);

-- Task 17 (kill switch) depends on Task 16 (risk manager) and Task 6 (order management)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 17, 'kalshi-event-trader', 16),
('kalshi-event-trader', 17, 'kalshi-event-trader', 6);

-- Task 18 (calibration) depends on Task 3 (logging) and Task 10 (probability framework)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 18, 'kalshi-event-trader', 3),
('kalshi-event-trader', 18, 'kalshi-event-trader', 10);

-- Task 19 (main loop) depends on Task 15 (strategy), Task 16 (risk), Task 17 (kill), Task 9 (feeds), Task 18 (calibration)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 19, 'kalshi-event-trader', 15),
('kalshi-event-trader', 19, 'kalshi-event-trader', 16),
('kalshi-event-trader', 19, 'kalshi-event-trader', 17),
('kalshi-event-trader', 19, 'kalshi-event-trader', 9),
('kalshi-event-trader', 19, 'kalshi-event-trader', 18);

-- Task 20 (dashboard) depends on Task 19 (main loop)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 20, 'kalshi-event-trader', 19);

-- Task 21 (E2E + go-live) depends on Task 20 (dashboard)
INSERT INTO task_dependencies (sprint, task_num, depends_on_sprint, depends_on_task) VALUES
('kalshi-event-trader', 21, 'kalshi-event-trader', 20);
