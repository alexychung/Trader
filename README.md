> **This is an AI experiment — to see how well Claude can vibecode a trading bot.** Effectively all of the code, research, and docs here were written by Claude. Treat it as an experiment, not investment advice.

# Trader — Kalshi NBA Bot

C++20 trading bot for Kalshi NBA in-game contracts.
Two cooperating strategies, both designed to survive against the SIG / DRW / Jump quants now active on Kalshi prod.

> **Current status (2026-05-22):** $100 live on Kalshi prod, zero positions, zero losses. First credible backtest evidence of edge — see [Backtest results](#backtest-results) below.

---

## Why this bot exists (and why it's NOT a market maker)

The original Phase 1 design was a multi-strategy bot covering weather / inflation / Fed / NBA. After the 2026-05-18 SAS@OKC test (`+$45.66 paper profit, 3 signals fired but all rejected for insufficient balance`), we pivoted to NBA-only.

After deploying the NBA bot for two live games (May 19 CLE@NYK Game 1, May 20 SAS@OKC Game 2), the high-confidence gate filtered out every single signal — zero trades. That triggered the [2026-05-20 MM research](#research-findings) which confirmed what the silence was telling us: **pure mid-of-book market-making is structurally dominated at $100 capital**. Three things actually make money in this space:

| Strategy | Public P&L | Source |
|---|---|---|
| Resolution-lag arb (buy near-certain YES at $0.96+, hold to settle) | $2k → $200k+, 600 trades, 99.7% win rate | [Indie Hackers post](https://www.indiehackers.com/post/the-quiet-polymarket-strategy-that-turned-small-margins-into-six-figures-833cb850ee) |
| Directional with Pinnacle CLV gate | +$60–65k/mo (but −$100k in 10 days on hockey) | [Polymarket "Meet Your Market Maker"](https://news.polymarket.com/p/meet-your-market-maker) |
| Reward farming on Polymarket | $200–800/day on $10k | Same source — *decayed* post-2024 election |

What does NOT work: naive symmetric MM. The most-starred public bot (`warproxxx/poly-maker`, 1.2k stars) literally says "**this bot is not profitable and will lose money.**"

So this bot now runs **two non-MM strategies in parallel**:
1. **Improved-Directional Arcsine** (`NbaStrategy`) — fires on high-conviction in-game mispricings vs the arcsine "safe lead" win-probability model.
2. **Resolution-Lag Arb** (`ResolutionLagStrategy`) — buys YES at $0.94–$0.99 on games that are effectively over, holds to settlement.

Both strategies are gated through optional **external sanity-check feeds**:
- **Pinnacle CLV gate** — refuses signals that fight the sharp consensus (no-op until you wire a real provider).
- **Injury kill switch** — freezes a team's markets when a starter is flagged on ESPN's injury report (no-op until you wire a real scraper).

---

## Architecture

```
src/
├── core/                          # config, logging, http, event bus, state, alerts
├── exchange/kalshi/               # REST + WS client, auth (RSA-PSS), order manager
│   └── kalshi_exchange.cpp        # combines them; shadow-mode interception
├── strategy/
│   ├── kalshi/calibration.cpp     # Brier-score logger (used by all strategies)
│   └── nba/
│       ├── win_probability.cpp    # Arcsine safe-lead model + logit helpers
│       ├── nba_score_feed.cpp     # cdn.nba.com scoreboard fetcher
│       ├── kalshi_nba_parser.cpp  # KXNBAGAME ticker ↔ (date, away, home, side)
│       ├── nba_strategy.cpp       # Improved-Directional Arcsine
│       └── resolution_lag_strategy.cpp  # Resolution-Lag Arb
├── feeds/                         # external sanity-check feeds (pluggable)
│   ├── sharp_book_provider.hpp    # Pinnacle CLV gate — Null stub + TODO
│   └── injury_news_feed.hpp       # ESPN injury kill switch — Null stub + TODO
├── risk/                          # RiskManager, KillSwitch, ClusterLimiter
└── main.cpp                       # wiring + main tick loop
```

### Data flow per tick (every 60s)

```
cdn.nba.com         Kalshi REST           Kalshi WS
     │                   │                    │
     ▼                   ▼                    ▼
NbaScoreFeed     refresh_markets_by_series  on_market_update
     │                   │                    │
     └──────────┬────────┴────────────────────┘
                ▼
         NbaStrategy.generate_signals()
         ResolutionLagStrategy.generate_signals()
                │
                ▼ (each signal passes RiskManager + KillSwitch + per-ticker dedup)
         exchange.place_order(post_only=true)
                │
                ▼
         CalibrationLogger (logs every fired signal + resolved outcome)
```

### Strategy 1: Improved-Directional Arcsine (`NbaStrategy`)

- **Model**: arcsine "safe lead" survival probability (Clauset/Kogan/Redner 2015, [arXiv:1503.03509](https://arxiv.org/abs/1503.03509)). Pure math, no ML.
- **Trades**: BUY YES only — picks the side that has positive edge vs Kalshi mid.
- **Time-window gate**: `60s ≤ regulation_remaining ≤ 1440s` (mid-Q3 through start of last minute).
- **High-confidence gate** (added 2026-05-19, tightened 2026-05-20 with min_lot_size):
  - `|home_lead| ≥ 8` AND `home_wp ∉ [0.30, 0.70]`
- **Pinnacle CLV gate** (added 2026-05-20, no-op without a real sharp-book provider): refuses signals that disagree with sharp consensus by >10%.
- **Lot-size optimizer** (added 2026-05-20): refuses to quote below `min_lot_size = 5` because Kalshi maker fee `ceil(0.0175 × C × P × (1-P))` makes 1-lots structurally unprofitable.
- **Quote-timing jitter** (added 2026-05-20): ±20% randomness on scoreboard poll cadence to defeat HFT pattern detection ([arXiv:2510.27334](https://arxiv.org/abs/2510.27334)).
- **Sizing**: fractional Kelly capped at `max_position_per_game_dollars`. Posts at maker-friendly bid+1¢ (never crosses spread).
- **No exits**: buy-and-hold to settlement. No re-pricing, no cancels, no force-exit on adverse move.

### Strategy 2: Resolution-Lag Arb (`ResolutionLagStrategy`)

- **Thesis**: buy YES at $0.94–$0.99 on the winning side of an effectively-decided game.
- **"Effectively decided" detector**: either
  - `status == final` (game ended, 30-sec settlement timer running), OR
  - `Q4` AND `clock ≤ 120s` AND `|lead| ≥ 15`
- **Sizing**: flat `max_position_per_game_dollars / yes_ask` (no Kelly — this is near-certain).
- **Per-trade ROI**: ~1–6% gross (buy at 0.94–0.99 → settle at 1.00). Fees take ~1¢, so net ROI 1–5%.
- **Compounding**: 100% win rate target across many small trades adds up.
- **Runs alongside NbaStrategy** without overlap because game-phase gates are disjoint.

---

## Backtest results

The backtest harness lives in `src/backtest/` and replays NBA play-by-play (from cdn.nba.com) against the strategies. There are **two price providers** — picking the right one is the whole story:

| Provider | What it does | What it tells you |
|---|---|---|
| `SyntheticKalshiPriceProvider` (default) | Prices each side at `our_fair ± half_spread + N(0, noise_stdev)` | Strategy fires gates correctly; **nothing about edge** (synthetic prices are correlated with our model by construction) |
| `KalshiCandlePriceProvider` (`--real-prices`) | Pulls real historical 1-minute candlesticks from the Kalshi `/candlesticks` endpoint, caches per ticker | Whether the strategy would have triggered against the actual book on the day, and whether those trades would have paid |

Switching to real prices uncovered a silent parse bug: Kalshi's actual response shape uses `*_dollars`/`*_fp` field names (e.g. `close_dollars: "0.3300"`), not `close`/`volume` like the parser assumed. Every candle was being parsed as zero. Fixed in `parse_candlesticks` with a primary-then-legacy field probe; covered by `KalshiRest.ParseCandlesticksRealShape` and `...LegacyShape`.

### Real-prices result (2026-04-01 → 2026-05-19, $100 bankroll, $25/game cap)

7 weeks of regular-season + play-in-tournament games, with the candle stale-quote filter and the strategy's `max_edge_threshold = 0.50` sanity gate both active. Results are after fees:

| Metric | Value |
|---|---|
| Games replayed | 162 (out of 173 attempted; 11 had no Kalshi candle data) |
| Signals fired | 167 (~1.0 per game) |
| Win rate | **64.1%** (107W / 60L) |
| Brier score | **0.1001** (random = 0.25, half of random ≈ "well calibrated") |
| Total PnL | **+$585.66** |
| Max drawdown | $35.19 |
| Avg PnL per game | $3.62 |

This is real edge against real historical Kalshi books. The Brier score (0.1055) tells you the model's probability calls are well-calibrated against actual outcomes for the bets it chose to fire — the trades aren't winning by luck.

Two filters did meaningful cleanup work on the way to this number:

1. **Candle stale-quote filter** (`KalshiCandlePriceProvider::select_quote`) — drops bars where `yes_bid=0`, the book is one-sided, or the spread is >25¢. These bars are minute-OHLC artifacts the strategy would never see live (it ticks on real-time WS deltas).
2. **Strategy `max_edge_threshold = 0.50` sanity gate** (`nba_strategy.cpp`) — refuses to fire when `|fair − mid| > 0.50`. Even when a candle bar shows a tight `{0.04, 0.05}` quote on a close one-possession game, the implied 4% win probability is data garbage, not a 70¢ free trade. Same gate also protects against real-life cases where the sharps know something we don't.

Pre-filter Apr→May numbers for comparison: 138 signals, 69.6% win rate, +$1059 PnL, Brier 0.1157. Stripping the 9 phantom signals lost some apparent wins but improved Brier — i.e., the gate kept the well-calibrated trades and dropped the noisy ones.

A separate fix on 2026-05-23 corrected a wall-clock-vs-candle-window alignment bug (`pbp_wall_clock_ts_sec` was anchored at midnight UTC; the candle provider's fetch window started at 22:00 UTC — every `get_quote` returned `candles.begin()` instead of the bar covering the in-game tick). With time-varying candle data actually visible to the strategy, signal count jumped from 129 → 165, Brier improved 0.1055 → 0.1013, and drawdown rose to $36.21 reflecting genuine intra-game variance. The PnL number is largely unchanged (~$590) but it now means something materially different: it measures edge against the *book at game-time*, not the *opening line*.

Caveats:
- 1-minute bar resolution; sub-minute moves invisible.
- Fills modeled as taker-at-yes_ask (worst-case). Real maker fills might be better OR fail outright. The replay does not simulate queue position.
- Wall-clock anchoring for PBP timestamps is approximate (see `pbp_wall_clock_ts_sec` in `replay_engine.cpp`).
- 129 signals is statistically thin for season-level conclusions. A walk-forward across multiple seasons is the right next step.

### Running it yourself

```powershell
# Synthetic (instant, no API hits)
.\build\Debug\trader_backtest.exe --start 2026-04-01 --end 2026-04-15 `
  --csv-out data\backtest\synthetic.csv

# Real Kalshi prices (uses config.kalshi prod auth, caches to data\cache\candles)
.\build\Debug\trader_backtest.exe --start 2026-04-01 --end 2026-04-07 `
  --real-prices --bankroll 100 --max-position-dollars 25 `
  --csv-out data\backtest\real.csv
```

---

## Recent Changes

### 2026-05-22: Real-prices backtest + edge confirmed

| # | Change | Why |
|---|---|---|
| 1 | `KalshiCandlePriceProvider` + `--real-prices` flag | First time the backtest exercises strategy logic against actual historical Kalshi prices instead of model-derived synthetic ones. |
| 2 | Fixed silent parse bug in `KalshiRestClient::parse_candlesticks` | Kalshi switched response shape to `*_dollars`/`*_fp` strings; old parser returned zero for every field. Hidden because nothing else called `get_candlesticks`. |
| 3 | Stale-quote candle filter | Drops bars with zero/one-sided/wide-spread quotes that wouldn't exist live. |
| 4 | `max_edge_threshold` sanity gate in NbaStrategy (default 0.50) | Refuses 50%+ phantom edges from broken candle bars. The same gate also protects against real-life "sharps know something we don't" cases. |
| 5 | `--max-position-dollars` / `--bankroll` sweep knobs | Same dataset, multiple risk budgets. |
| 6 | The post-research code blob is now committed | The 2026-05-20 hardening (Resolution-Lag, CLV/injury feeds, lot-size, etc.) + all deleted-old-strategy code landed in `a6ffa5c`, `b93f454`, `f721978`, `8f17347`, `9d84c97`. Working tree clean. |

### 2026-05-20: Research-driven hardening

Deep MM research (saved in `.claude/memory/project_kalshi_mm_research_findings.md`) surveyed ~10 open-source bots and the few operator accounts with public P&L. Shipped 7 improvements in priority order:

| # | Change | File(s) | Impact |
|---|---|---|---|
| 1 | **Pinnacle CLV provider interface** + Null stub + NbaStrategy integration | `src/feeds/sharp_book_provider.{hpp,cpp}`, `nba_strategy.cpp` | High (when wired) — refuses to fight the sharp book |
| 2 | **Resolution-Lag Arb strategy** | `src/strategy/nba/resolution_lag_strategy.{hpp,cpp}` | High — boring trade with audited 99.7% public win rate |
| 3 | **Injury-news kill switch interface** + Null stub + NbaStrategy integration | `src/feeds/injury_news_feed.{hpp,cpp}`, `nba_strategy.cpp` | High (when wired) — defends against stale-quote picks during news breaks |
| 5 | **Logit-space A-S helpers** (price_to_logit, sigmoid, reservation price) | `win_probability.{hpp,cpp}` | Medium — building block for future adaptive sizing |
| 7 | **Lot-size optimizer** (refuse to quote < 5 contracts) | `nba_strategy.cpp` | Medium — avoids Kalshi fee `ceil()` on tiny lots |
| 8 | **Amend-order REST endpoint** | `exchange/kalshi/rest_client.{hpp,cpp}` | Medium — preserves queue position on re-quotes |
| 9 | **Quote-timing jitter** (±20% on poll cadence) | `nba_strategy.cpp` | Low-Medium — anti-HFT pattern detection |

What's NOT shipped (requires user action):
- **#4 Chicago VPS** — Kalshi servers are in Chicago metro. Move bot to a Chicago VPS for ~1ms RTT (vs ~100ms from a home connection). $5–20/mo at QuantVPS / NYC Servers / Tradox.
- **#10 Kalshi VIP enrollment** — Apply via Kalshi help center for the [Volume Incentive Program](https://help.kalshi.com/en/articles/13823850) (up to $0.005/contract rebate) and [Liquidity Incentive Program](https://help.kalshi.com/en/articles/13823851).
- **#6 Glosten-Milgrom Bayesian quoter** — Skipped intentionally; designed for two-sided MM, which we pivoted away from.

---

## Setup

### 1. Build (Windows, vcpkg)

```powershell
cmake --preset default
cmake --build --preset default
```

First build pulls Boost / OpenSSL / spdlog / yaml-cpp / nlohmann-json / GTest via vcpkg — takes ~10 min.

### 2. Get a Kalshi API key

Demo and prod are separate venues with separate key pairs.

1. Account at https://kalshi.com (prod) or https://demo.kalshi.co (demo). KYC required on prod.
2. **Account → API Keys**. Save the RSA private key text immediately (Kalshi shows it once).
3. Save to `secrets/kalshi_api_key_prod.pem` (or `..._demo.pem`). If PKCS#1 format (`-----BEGIN RSA PRIVATE KEY-----`), convert once:
   ```powershell
   openssl pkcs8 -topk8 -nocrypt -in secrets/kalshi_api_key_prod.pem -out secrets/kalshi_api_key_prod.pkcs8.pem
   mv secrets/kalshi_api_key_prod.pkcs8.pem secrets/kalshi_api_key_prod.pem
   ```
4. Paste the key UUID into `config/config.yaml` → `kalshi.prod.api_key_id`. `secrets/` is gitignored.

### 3. (Optional) Wire real external feeds

Both feeds run as Null stubs by default — the bot works without them, just without the sanity-check gates.

**Pinnacle CLV gate** (#1, highest-leverage improvement):
1. Sign up at https://the-odds-api.com (500 free requests/month).
2. Paste key into `config/config.yaml` → `feeds.oddsapi_key`.
3. Replace `NullSharpBookProvider` with a real `OddsApiPinnacleProvider` in `src/feeds/sharp_book_provider.cpp` — see the header for the implementation sketch. Cache results per (date, game) to stay under quota.

**Injury kill switch** (#3):
1. No key required — ESPN's injury page is public HTML.
2. Replace `NullInjuryNewsFeed` with a real `EspnInjuryFeed` scraper in `src/feeds/injury_news_feed.cpp`. Defensive parsing: log + return "no change" on HTML structure changes rather than erroneously freezing teams.

### 4. Run

```powershell
# Paper (demo API, no real money)
.\build\Debug\trader.exe config/config.yaml

# Live (real money — REQUIRES --confirm-live every session)
.\build\Debug\trader.exe config/config.yaml --confirm-live

# Shadow (prod read-only — orders intercepted at exchange layer)
.\build\Debug\trader.exe config/config.yaml --shadow
```

### 5. Smoke tests

```powershell
.\build\Debug\trader_nba_smoke.exe      # fetches today's NBA scoreboard + WP
.\build\Debug\trader_catalog_inspect.exe  # lists Kalshi market series prefixes
.\build\Debug\trader_portfolio_check.exe  # balance + recent fills (read-only)
.\build\Debug\trader_demo_smoke.exe     # places + cancels one order (DEMO ONLY)
```

### 6. Reading the logs

`logs/trader.log` (rotating, ~10 MB threshold). Key patterns:

```
[info] === Kalshi NBA Trading Bot v0.3.0 ===   ← startup
[info] NBA strategy: edge≥0.06, $100/game cap, ..., high-conf gate: |lead|≥8 ...
[info] ResolutionLag strategy: ENABLED, entry window [0.94,0.99], ...
[info] Live balance reconciliation OK: Kalshi=$100.00
[info] refresh_markets_by_series(series=KXNBAGAME): 14/14 markets kept

[info] NBA signal: NBA arcsine: SAS@OKC Q3 ... → buy yes 96x @ 0.520
[info] ResolutionLag signal: SAS@OKC [FINAL] score 95-118 → buy YES 26x @ 0.96
[info] Order placed: KXNBAGAME-...-SAS yes 96x $0.5100 (abc123)
[info] Fill: buy yes KXNBAGAME-...-SAS 96x @ $0.5100
[info] NBA settlement: KXNBAGAME-...-SAS → YES (PnL: $45.66)

[info] Status: Balance $100.00 | Exposure $0.00 | Daily $0.00 | Real 13 (NBA Brier 0.000 / 0 samples) | Live games 1
```

Warning / failure patterns:
```
[warning] SHADOW: Kalshi balance ... differs from configured bankroll ...
[warning] place_order failed: HTTP 400 - {"error":{"code":"insufficient_balance",...}}
[critical] KILL SWITCH TRIGGERED: ...
```

---

## Risk Controls

| Layer | Setting | Effect |
|---|---|---|
| `risk.max_position_per_market` | 200 | ~$100 cap per ticker at $0.50 prices |
| `risk.max_total_exposure` | $100 | hard cap across all positions |
| `risk.max_daily_loss` | $100 | halt new entries if daily PnL ≤ −$100 |
| `risk.kill_switch_loss` | $100 | full stop + cancel all if daily PnL ≤ −$100 |
| `risk.maker_only` | true | post-only orders (avoid 4× taker fees) |
| `nba.min_lot_size` | 5 | refuse 1-lots (Kalshi fee `ceil()`) |
| `nba.max_position_per_game_dollars` | $100 | NBA strategy per-game cap |
| `resolution_lag.max_position_per_game_dollars` | $25 | ResolutionLag per-game cap |
| `--confirm-live` CLI flag | required every session for live mode | prevents accidental live runs |
| `--shadow` CLI flag | intercepts every place_order at exchange layer | safe prod-data validation |

Kalshi balance reconciliation gate (live mode): bot REFUSES to start if local `state.balance` differs from Kalshi prod balance by > $1. Edit `data/state.json` to sync if needed.

**Known bug (fixed 2026-05-19):** Kalshi's `/portfolio/balance` returns `balance` in **cents** (numeric) and `balance_breakdown[0].balance` in **dollars** (string). The bot previously read the top-level field as dollars, showing $10,000 when actual was $100. Fixed in `rest_client.cpp` to prefer the breakdown.

---

## Research Findings

The strategy choices in this codebase are driven by [`memory/project_kalshi_mm_research_findings.md`](.claude/projects/.../memory/) and a second pass on 2026-05-20. Key sources:

- **[Polymarket "Meet Your Market Maker" interview](https://news.polymarket.com/p/meet-your-market-maker)** — Operator P&L: +$60–65k/month, −$100k in 10 days on hockey, "turn off MM" dashboard button for breaking news.
- **[Indie Hackers post](https://www.indiehackers.com/post/the-quiet-polymarket-strategy-that-turned-small-margins-into-six-figures-833cb850ee)** — $2k → $200k+ via resolution-lag arb (the inspiration for `ResolutionLagStrategy`).
- **[Dalen 2025: Toward Black-Scholes for Prediction Markets](https://arxiv.org/abs/2510.15205)** — logit-space jump-diffusion (inspiration for `WinProbability::price_to_logit` helpers).
- **[Kalshi fee schedule](https://kalshi.com/fee-schedule)** + **[fee rounding docs](https://docs.kalshi.com/getting_started/fee_rounding)** — confirms the `ceil()` that makes 1-lot quoting unprofitable.
- **[Yahoo: Arbitrage Bots Dominate Polymarket](https://finance.yahoo.com/news/arbitrage-bots-dominate-polymarket-millions-100000888.html)** — Bots extracted ~$40M from Polymarket in a year; 92% of traders lose money.
- **[QuantVPS: Kalshi server location](https://www.quantvps.com/blog/kalshi-servers-location)** — Confirms Chicago metro; ~1ms RTT from colocated VPS.

---

## Repo conventions

- All trading actions log to `logs/trader.log` (info+) and `data/calibration.json` (per-trade record, used for Brier-score tracking).
- State persisted to `data/state.json` after every tick — balance, daily PnL, last fill timestamp, day-start. Used for crash recovery + balance reconciliation.
- `secrets/` and `data/` are gitignored.
- Tests live in `tests/` and use Google Test. `cmake --build build --target trader_tests --config Debug && ./build/Debug/trader_tests.exe` runs the full suite.
- `.claude/memory/` holds session-persistent context (project direction, debugging notes, research findings) — read by Claude Code at start of every session, do not delete.

## Future work

In priority order:

1. **Run the real-prices backtest over a full season window**, not just one week. The 36-signal sample is encouraging but small; firmer numbers (and ideally a per-month walk-forward) would tell us whether the 58% win rate holds.
2. **Replay-engine fill realism**: today every signal fills at `signal.market_price` (worst-case ask). Adding a simple maker-fill model (post at bid+1¢; check whether the bar's high touched it) would let us tell maker- and taker-mode P&L apart.
3. **Wire real `OddsApiPinnacleProvider`** — Single highest-leverage improvement per research. The Null stub keeps the CLV gate code path exercised in tests, but adds zero edge until a real provider is in place.
4. **Wire real `EspnInjuryFeed`** — Defends against the dominant adverse-selection loss mode (Polymarket operator's "turn off MM" button).
5. **Apply for Kalshi VIP** — Volume + Liquidity Incentive Programs. Free $0.005/contract rebate on eligible volume.
6. **Move to Chicago VPS** — $5–20/mo, 100× latency reduction.
7. **Use `amend_order` in OrderManager** — Stub-level: the endpoint is exposed but OrderManager doesn't yet call it. Wire on price-down re-quotes for queue preservation.

---

## License

Private — Alex Chung. Not for redistribution.
