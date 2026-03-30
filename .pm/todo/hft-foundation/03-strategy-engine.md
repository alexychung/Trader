# Strategy Engine — Multi-Strategy Architecture

**Status**: Not Started

---

## Scope

Implement the strategy engine with a pluggable architecture supporting multiple concurrent strategies. The primary strategy is Avellaneda-Stoikov market making on altcoin pairs with cross-pair (BTC lead) alpha signals. A secondary funding rate arbitrage strategy runs alongside.

**This spec covers**:
- Abstract strategy interface (`IStrategy`)
- Input/output data structures
- Strategy lifecycle (init, on_market_data, on_fill, shutdown)
- Strategy factory/registry for runtime selection
- **Strategy #1: Altcoin Market Making** — Avellaneda-Stoikov with OBI + BTC lead signal
- **Strategy #2: Funding Rate Arbitrage** — delta-neutral spot/perp carry trade
- EWMA volatility estimator (shared component)
- Order book imbalance (OBI) signal for quote skewing
- Cross-pair BTC lead signal for directional skew
- Multi-level quoting with geometric size decay
- Inventory penalty amplification near limits
- Multi-instance support (one strategy instance per symbol)

**Out of scope**:
- Order placement mechanics → `04-order-execution.md`
- Risk limits and kill switch → `05-risk-management.md`
- Backtesting the strategy → `06-backtesting.md`

---

## What's Done

| Item | Status |
|------|--------|
| Strategy interface definition | Not started |
| Input/output structs | Not started |
| Strategy factory/registry | Not started |
| EWMA volatility estimator | Not started |
| Avellaneda-Stoikov quoter | Not started |
| OBI signal | Not started |
| BTC lead cross-pair signal | Not started |
| Multi-level quoting | Not started |
| Funding rate strategy | Not started |

---

## Technical Context

### Strategy Interface Contract

```cpp
// Strategies register themselves so main loop can instantiate by name from config
// e.g., config.yaml: strategy.name: "market_making"
StrategyFactory::register("market_making", []() { return std::make_unique<MarketMakingStrategy>(); });
```

```
IStrategy::init(config, logger)          → called once at startup
IStrategy::on_market_data(event)         → called on every order book / trade update
IStrategy::on_fill(fill_event)           → called when our orders get filled
IStrategy::on_timer(timestamp)           → called on configurable interval (e.g., every 100ms)
IStrategy::get_parameters() → ParamMap   → expose tunable parameters for backtesting
IStrategy::shutdown()                    → called on clean exit
```

Inputs:
  - OrderBook snapshot (bids, asks, mid-price, spread)
  - Trade stream (price, qty, timestamp, is_buyer_maker)
  - Current position (inventory, unrealized P&L)
  - Strategy config parameters

Outputs:
  - SignalEvent: QuoteUpdate with multiple bid/ask levels, or NoAction

### Avellaneda-Stoikov Model

**Reservation price** (indifference price adjusted for inventory):

```
r(s, q, t) = s - q * γ * σ² * (T - t)
```

- `s` = current mid price (use weighted mid: `(best_bid * ask_qty + best_ask * bid_qty) / (bid_qty + ask_qty)`)
- `q` = current inventory (positive = long, negative = short)
- `γ` = risk aversion coefficient (tunable, start at 0.01)
- `σ` = asset volatility (EWMA estimated, see below)
- `T - t` = rolling time horizon (use constant 300s for 24/7 crypto — no session end)

**Optimal spread** (distance from reservation price to each quote):

```
δ(t) = γ * σ² * (T - t) + (2/γ) * ln(1 + γ/k)
```

- `k` = order arrival intensity. Estimated from historical fill rates: `λ(δ) = A * exp(-k * δ)`, where `λ(δ)` is fill rate at distance `δ` from mid. Fit `A` and `k` from backtest data.

**Final quotes**:
```
bid = r - δ/2
ask = r + δ/2
```

Round to nearest valid tick size. Ensure bid < ask. If optimal spread < 1 tick, skip quoting (negative edge after fees).

### EWMA Volatility Estimator

Use exponentially weighted moving variance of 1-second mid-price log returns:

```cpp
double alpha = 1.0 - exp(-1.0 / halflife_seconds);  // halflife = 60s
double log_return = log(mid_price / prev_mid_price);
ewma_var = (1.0 - alpha) * ewma_var + alpha * (log_return * log_return);
sigma = sqrt(ewma_var);
```

This adapts quickly to regime changes (volatility spikes) while remaining smooth enough to not whipsaw quotes. Halflife of 60 seconds is the starting point — tune via backtest.

On startup, seed the EWMA with variance computed from the first 60 seconds of data before enabling quoting.

### Order Book Imbalance Signal

OBI is the single strongest short-term predictor in crypto markets (Pearson correlation ~0.15-0.25 with next-second mid-price change on BTC/USDT):

```
OBI = (bid_qty - ask_qty) / (bid_qty + ask_qty)   // range [-1, 1]
```

Use top 1-5 levels of the book. Deeper levels add information with diminishing returns.

Apply to reservation price:
```
r_adjusted = r + obi_weight * OBI * σ
```

Where `obi_weight` is tunable (start at 0.3). Scaling by σ keeps the signal proportional to current market conditions.

### Multi-Level Quoting

Place 3-5 levels per side instead of single bid/ask:

| Level | Distance from reservation | Size (% of total) |
|-------|--------------------------|-------------------|
| 1 | optimal_spread / 2 | 40% |
| 2 | optimal_spread / 2 + level_spacing | 25% |
| 3 | optimal_spread / 2 + 2 * level_spacing | 20% |
| 4 | optimal_spread / 2 + 3 * level_spacing | 15% |

`level_spacing` = configurable, start at 1 bps (0.0001).

Benefits:
- Better queue priority at the inside
- Larger capture when volatility spikes
- More total fills (at cost of worse average fill price on outer levels)

### Inventory Penalty Amplification

As inventory approaches max, exponentially increase effective gamma to aggressively unwind:

```
γ_effective = γ * exp(β * (|q| / q_max)²)
```

- `β` = amplification factor (start at 1.5)
- At 50% of max inventory: γ is ~1.5x base
- At 90% of max inventory: γ is ~3.5x base
- At 100%: γ is ~4.5x base → very aggressive one-sided quoting

This creates smooth, increasingly aggressive inventory reduction without a hard cliff.

### Volatility Regime Detection

Use ratio of short-term to long-term volatility:

```
vol_ratio = σ_short (10s halflife) / σ_long (300s halflife)
```

| vol_ratio | Regime | Action |
|-----------|--------|--------|
| < 0.5 | Very calm | Tighten spreads, max fill rate |
| 0.5 - 1.5 | Normal | Standard quoting |
| 1.5 - 3.0 | Elevated | Widen spreads, reduce size |
| > 3.0 | Extreme | Pull quotes (let kill switch evaluate) |

### Cross-Pair BTC Lead Signal

BTC price movements lead altcoin movements by 100-500ms. This is the strongest directional alpha signal for altcoin market making:

```
btc_return = log(btc_mid_now / btc_mid_500ms_ago)
btc_signal = btc_return * btc_beta(symbol)    // beta is pair-specific
r_adjusted = r + obi_weight * OBI * σ + btc_lead_weight * btc_signal * σ
```

- Subscribe to `BTCUSDT@bookTicker` (lowest latency BTC feed)
- Compute rolling BTC return over configurable window (default 500ms)
- Only act on moves > `min_move_bps` (default 5 bps) to filter noise
- `btc_lead_weight` is tunable per symbol (start at 0.2)
- `btc_beta` calibrated from historical correlation (typically 0.5-1.5 for altcoins)

**Effect**: When BTC drops suddenly, the altcoin market maker immediately lowers its ask (more aggressive selling) and raises its bid (less aggressive buying), avoiding adverse selection from the BTC-led move propagating to the altcoin.

### Minimum Spread Floor

Never quote tighter than the fee breakeven:

```
min_spread = 2 * maker_fee_rate + safety_margin
```

At 0.075% maker fee (BNB discount): min_spread = 0.15% + 0.05% safety = 20 bps.
Without BNB discount (0.1%): min_spread = 0.20% + 0.05% = 25 bps.

If the model's optimal spread is below this floor, use the floor. This prevents quoting with negative expected value.

### Strategy #2: Funding Rate Arbitrage

A secondary strategy that runs alongside market making, providing delta-neutral carry income:

**How it works**:
1. Monitor predicted funding rates via `GET /fapi/v1/premiumIndex` for target pairs
2. When predicted funding > `min_funding_rate` (e.g., 0.02% per 8h period):
   - Buy spot (e.g., buy 100 AVAX)
   - Short equivalent perpetual (short 100 AVAX-PERP)
   - Net position is zero — delta neutral
3. Collect funding payment every 8 hours
4. Exit when predicted funding drops below `exit_funding_rate`

**Implementation**:
- Implements `IStrategy` interface like market making
- `on_timer` checks funding rates every 5 minutes
- Entry/exit signals emitted as `SignalEvent`
- Position tracking must distinguish spot vs. futures legs
- Uses IOC orders for entry/exit (not maker — speed matters more than fees here)

**Risk controls**:
- Max notional per pair for funding positions
- Exit immediately if funding flips negative
- Monitor basis (spot-perp price difference) — exit if basis diverges beyond threshold
- Funding positions count toward portfolio-level risk limits

```yaml
funding_rate:
  min_funding_rate: 0.0002        # 0.02% per period minimum to enter
  exit_funding_rate: 0.0001       # Exit below this
  max_notional_per_pair: 2000     # $2000 max per pair
  max_basis_divergence_bps: 50    # Exit if spot-perp basis > 50 bps
  check_interval_s: 300           # Check rates every 5 minutes
  pairs: ["AVAXUSDT", "ARBUSDT", "NEARUSDT"]
```

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | Define IStrategy interface and input/output structs | Interface compiles, documented, mock strategy builds against it |
| 2 | Strategy factory/registry with multi-instance support | Can register strategies by name, instantiate multiple instances (one per symbol) from config |
| 3 | EWMA volatility estimator with configurable halflife | Produces correct volatility estimates, unit tests against known data, handles startup seeding |
| 4 | Avellaneda-Stoikov quoter (reservation price + optimal spread) | Generates correct bid/ask given mid, inventory, vol, gamma; unit tests for inventory skew behavior |
| 5 | Order book imbalance signal computation | Computes OBI from top N levels, adjusts reservation price, unit tests for balanced/imbalanced books |
| 6 | Cross-pair BTC lead signal | Consumes BTC bookTicker, computes rolling return, adjusts altcoin reservation price; unit tests for signal direction |
| 7 | Multi-level quote generation with geometric size decay | Generates N levels per side with correct spacing and sizing, respects tick size |
| 8 | Inventory penalty amplification and volatility regime detection | Gamma scales with inventory, quoting widens in high-vol regimes, unit tests for edge cases |
| 9 | Funding rate arbitrage strategy | Monitors funding rates, enters/exits delta-neutral positions, handles spot + futures legs, unit tests for entry/exit logic |

---

## Default Parameters

```yaml
# Market making strategy (one instance per altcoin pair)
market_making:
  gamma: 0.01                    # Risk aversion coefficient (increase for less liquid pairs)
  sigma_halflife_s: 60           # EWMA volatility halflife (seconds)
  sigma_short_halflife_s: 10     # Short-term vol for regime detection
  sigma_long_halflife_s: 300     # Long-term vol for regime detection
  time_horizon_s: 300            # Rolling T-t (seconds)
  order_arrival_k: 1.5           # Order arrival intensity (calibrate per pair from backtest)
  obi_weight: 0.3                # Order book imbalance signal weight
  obi_levels: 5                  # Book levels for OBI calculation
  btc_lead_weight: 0.2           # Cross-pair BTC lead signal weight
  btc_lead_window_ms: 500        # BTC return lookback window
  btc_min_move_bps: 5            # Ignore BTC moves smaller than this
  num_levels: 3                  # Quote levels per side
  level_spacing_bps: 2.0         # Spacing between levels (wider for altcoins)
  min_spread_bps: 20             # Floor: never quote tighter than this
  inventory_amp_beta: 1.5        # Inventory penalty amplification factor
  requote_interval_ms: 200       # Minimum time between quote updates
  vol_regime_extreme: 3.0        # Vol ratio threshold to pull quotes
  startup_seed_seconds: 60       # Seconds of data to collect before quoting

# Funding rate strategy
funding_rate:
  min_funding_rate: 0.0002       # Min predicted funding to enter (0.02% per 8h)
  exit_funding_rate: 0.0001      # Exit below this
  max_notional_per_pair: 2000    # $2000 max per funding position
  max_basis_divergence_bps: 50   # Exit if spot-perp basis diverges
  check_interval_s: 300          # Check funding rates every 5 minutes
```

## Backtest Expectations

| Metric | Target | Notes |
|--------|--------|-------|
| Sharpe Ratio | > 3.0 | Annualized, for HFT this should be high |
| Max Drawdown | < 5% of capital | Per rolling 24h window |
| Win Rate | > 55% | Of completed round trips |
| Profit Factor | > 1.5 | Gross profit / gross loss |
| Avg Trade PnL | > 2x round-trip fees | After all fees |
| Fill Rate | 10-30% | Of quote updates resulting in fill |
| Adverse Selection | < 0.5x spread | Avg move against post-fill |

---

## References

- Avellaneda, M. & Stoikov, S. (2008). "High-frequency trading in a limit order book." Quantitative Finance, 8(3), 217-224.
- Gueant, O., Lehalle, C.A., & Fernandez-Tapia, J. (2012). "Dealing with the inventory risk." Mathematics and Financial Economics.
- Cartea, A., Jaimungal, S., & Penalva, J. (2015). Algorithmic and High-Frequency Trading. Cambridge University Press.
- Full research: `docs/RESEARCH.md`
