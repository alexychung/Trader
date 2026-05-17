# Trader — Data Sources & Algorithms Research (2026)

> A comprehensive reference on the best-quality data sources, state-of-the-art algorithms, and current Kalshi platform state for the Phase 1 Kalshi Event Trading bot. Complements `KALSHI_RESEARCH.md` (strategy-centric), `RESEARCH.md` (Phase 2 crypto), and `VENUE_ANALYSIS.md` (venue comparison).
>
> Scope: what goes into the model (data), what the model does (algorithms), and what goes out to Kalshi (execution). Written April 2026, after the CFTC sports-contract Third Circuit ruling, the Kalshi fixed-point API migration, and the operational debut of AI-native weather models.

---

## Table of Contents

1. [Executive Summary & Data-Quality Scorecard](#1-executive-summary--data-quality-scorecard)
2. [Kalshi Platform — Current State (2026)](#2-kalshi-platform--current-state-2026)
3. [Data Sources: Weather & Tropical Cyclones](#3-data-sources-weather--tropical-cyclones)
4. [Data Sources: Macroeconomics](#4-data-sources-macroeconomics)
5. [Data Sources: Market-Implied Priors](#5-data-sources-market-implied-priors)
6. [Data Sources: Polling, Sentiment, Miscellaneous](#6-data-sources-polling-sentiment-miscellaneous)
7. [Algorithms: Probability Forecasting](#7-algorithms-probability-forecasting)
8. [Algorithms: Calibration](#8-algorithms-calibration)
9. [Algorithms: Position Sizing (Kelly & Extensions)](#9-algorithms-position-sizing-kelly--extensions)
10. [Algorithms: Portfolio Construction & Risk](#10-algorithms-portfolio-construction--risk)
11. [Algorithms: Forecast Combination](#11-algorithms-forecast-combination)
12. [Algorithms: Backtesting Discipline](#12-algorithms-backtesting-discipline)
13. [Recommended Implementation Roadmap](#13-recommended-implementation-roadmap)
14. [Appendix A: Formula Cheat Sheet](#appendix-a-formula-cheat-sheet)
15. [Appendix B: Complete URL Index](#appendix-b-complete-url-index)

---

## 1. Executive Summary & Data-Quality Scorecard

### 1.1 Summary findings

- **The biggest 2024-2026 edge amplifier is AI weather models.** GraphCast, GenCast, ECMWF AIFS, and Pangu-Weather routinely beat ECMWF's legendary HRES on 2m temperature, tropical-cyclone tracks, and extreme-event probabilities. All are free for research / commercially usable (GraphCast Apache 2.0; AIFS CC-BY-4.0). For a $100 bot, this is a structural advantage retail Kalshi participants do not have.
- **For inflation and macro contracts, the best strategy is ensemble of public nowcasts + your own bridge/factor model.** Cleveland Fed nowcast + Atlanta Fed GDPNow + bespoke LightGBM using ALFRED vintages + ZORI lead on shelter + EIA gas on energy beats any single source by 15-25% in RMSE (Chinn-Meunier 2024).
- **Kalshi's API is actively migrating.** Integer-cent price fields are being eliminated (final cut: April 2, 2026). Fractional-contract trading (`count_fp`) is rolling out per-market. A new `user_orders` WS channel replaces order polling. A batch orderbook endpoint (up to 100 tickers/call) and first-party historical endpoints landed Q1 2026.
- **Modern calibration methods beat the textbooks.** Beta calibration (Kull 2017) dominates Platt scaling. **Inductive Venn-Abers** produces probability **intervals** with distribution-free validity — these intervals feed directly into credal-set Kelly sizing, so they're actionable, not just diagnostic.
- **Sizing should be multi-bet Kelly with Venn-Abers lower-bound probabilities, cluster caps, and CVaR-constrained**, not independent per-market Kelly. At $100 capital, use α = 0.25 fractional Kelly for the first 500 settled bets.
- **Sports markets are now live on Kalshi** and legally protected in the Third Circuit after *KalshiEX v. Flaherty* (April 2026). But they face sharp competition from sportsbooks and are not a profitable category for retail quants.

### 1.2 Data-source quality scorecard

Ratings combine: source accuracy, free-tier availability, API quality, latency, historical-data depth, and C++ accessibility.

| Source | Category | Quality | Cost | Latency | Verdict |
|---|---|---|---|---|---|
| ECMWF AIFS Open Data | Weather | **10** | Free | ~4h | Operational since Feb 2025. Best free probabilistic source. |
| NOAA NBM v4.2 | Weather | **10** | Free | ~90min | Official calibrated blended ensemble. Primary settlement-day source. |
| Open-Meteo (GraphCast + multi-model) | Weather | **9** | Free | ~1h | Frontier AI model aggregator, no key required. |
| NOAA GEFS Reforecast v12 | Weather (historical) | **10** | Free | N/A | 20 years of retrospective ensembles. Gold for backtesting. |
| ERA5 (ARCO-ERA5 on GCS) | Weather (truth) | **10** | Free | ~5-day lag | Ground truth for calibration training. |
| GHCN-Daily | Weather (obs) | **9** | Free | Daily | Definitive historical station records. |
| NOAA HAFS | Hurricanes | **10** | Free | 6h | 2023+ operational hurricane model. |
| NHC probabilistic wind-speed | Hurricanes | **10** | Free | 6h | Calibrated landfall-intensity product. |
| CSU Tropical Forecast | Hurricanes (seasonal) | **8** | Free | Monthly | Best seasonal outlook. |
| Cleveland Fed Inflation Nowcast | CPI | **10** | Free | Daily | Stock-Watson UCSV; best single CPI predictor. |
| Zillow ZORI | Rent/Shelter CPI | **9** | Free | Monthly | Leads CPI shelter by 12-16 months. |
| EIA gasoline/WTI | Energy CPI | **9** | Free | Weekly/Daily | Near-perfect correlation with CPI energy component. |
| Truflation | Alt-inflation | **7** | Free tier / paid | Daily | Orthogonal signal, not CPI-basket-comparable. |
| Manheim MUVVI | Used-car CPI | **7** | Free monthly / paid weekly | Monthly | Core goods leader. |
| Atlanta Fed GDPNow | GDP | **10** | Free | ~daily | Transparent, well-calibrated (MAE 0.6pp). |
| Dallas/NY Fed WEI | Weekly growth | **8** | Free | Weekly (Fri) | Dynamic factor model, macro regime indicator. |
| Philly Fed ADS | Daily growth | **8** | Free | Weekly | Daily-frequency activity factor. |
| ADP National Employment | NFP | **7** | Free | 2d pre-NFP | Methodology overhaul 2022; r = 0.6 with NFP post-2022. |
| DOL Initial Jobless Claims | Labor | **9** | Free | Weekly Thu | Strongest high-freq labor signal. |
| Indeed Hiring Lab | Labor | **8** | Free via FRED | Daily | 2-3 month NFP lead. |
| ALFRED (vintage FRED) | Backtest truth | **10** | Free | Real-time | Essential for honest backtesting. |
| FRED-MD / FRED-QD | Macro factors | **10** | Free | Monthly | Curated, stationarity-transformed for factor models. |
| CME Fed Funds futures | Rate decisions | **10** | Free delayed / paid real-time | Real-time | CME FedWatch reference. |
| Deribit options API | Crypto priors | **9** | Free | Real-time | Deepest crypto options, free L2. |
| SPX options (CBOE) | Equity priors | **10** | $29/mo (Polygon) | Delayed/real-time | Risk-neutral distribution source. |
| 538 historical CSV | Polling | **7** | Free | Static | Last-known-good for polling methodology replica. |
| PollBase (Economist) | Polling | **8** | Free | Monthly | Cleanest long-run polling dataset. |
| Kalshi Forecast Percentiles | Benchmark | **8** | Via API | Real-time | Crowd forecast distribution — calibration benchmark. |

### 1.3 Decision tree: which data + algorithm stack per category

```
Kalshi market category
├── Daily high temperature (6 cities) — HIGH priority
│    Data:   AIFS + NBM + GraphCast (via Open-Meteo) + HRRR last-mile
│    Algo:   Quantile Regression Forest post-processing on 10yr GEFS Reforecast + GHCN
│            → EMOS/NGR second-stage → Beta calibration → Venn-Abers intervals
│    Sizing: multi-bet Kelly with cluster cap; α=0.25 for first 100 bets
│
├── CPI brackets — MEDIUM priority
│    Data:   Cleveland Fed + ZORI + EIA + Manheim + ALFRED vintages
│    Algo:   LightGBM bridge regression + Cleveland Fed + consensus → stacked meta-model
│            → Beta calibration → Venn-Abers
│    Sizing: cluster-cap with NFP/Fed/GDP (macro cluster ≤ 35%)
│
├── NFP brackets — MEDIUM priority
│    Data:   ADP + jobless claims 4wk MA + Indeed postings + ISM Emp
│    Algo:   Bridge regression (Atlanta Fed style) + dynamic factor model
│    Sizing: same macro cluster as CPI
│
├── Fed rate decisions — LOW priority (efficient market)
│    Data:   CME FedWatch (from ZQ futures) + SOFR term structure + Timiraos sentiment
│    Algo:   Mechanical arbitrage vs FedWatch; >5pp divergence triggers trade
│    Sizing: capital-efficient only, max 10% portfolio
│
├── Hurricane count + landfall — HIGH priority JUN-NOV
│    Data:   HAFS + NHC probabilistic wind-speed + HURDAT2 + CSU seasonal + ENSO
│    Algo:   Same as temperature stack, substituting HAFS/GenCast for GFS/AIFS
│    Sizing: seasonal — ramp to 15% exposure Jul-Oct
│
├── Stock market levels / crypto — AVOID (near-efficient)
├── Sports — AVOID (sharp competition)
└── Entertainment — opportunistic manual only
```

---

## 2. Kalshi Platform — Current State (2026)

This section documents what has changed since the initial project research. It is an operational reference, not a strategy guide (for strategy, see `KALSHI_RESEARCH.md`).

### 2.1 Major API changes (Q4 2025 – Q2 2026)

**The fixed-point migration.** Over Jan-Apr 2026 Kalshi is replacing integer-cent price fields with dollar-denominated decimal strings:

- Prices now carry up to 4 decimals, e.g. `"0.6500"`.
- **Jan 6, 2026**: legacy integer fields removed from `GET /markets`, `GET /markets/{ticker}`, `GET /events`, `GET /events/{ticker}` (`yes_bid`, `no_ask`, `last_price`, `yes_price`, `previous_yes_bid`, `notional_value`, `response_price_units`, `liquidity`).
- **Feb 11, 2026**: `type` field removed from `POST /portfolio/orders` — limit-only from here out.
- **Feb 13, 2026**: `liquidity` and `liquidity_dollars` response fields now return 0 (dead fields).
- **Apr 2, 2026**: final cut of all remaining integer cent fields. Parsers expecting `yes_price: 65` will silently break after this date.

**Order placement now uses dollar strings:**

```json
POST /portfolio/orders
{
  "ticker": "KXHIGHNY-26APR20-T75",
  "side": "yes",
  "action": "buy",
  "count_fp": "10.00",
  "yes_price_dollars": "0.6500",
  "client_order_id": "bot-1744200000000-0001",
  "time_in_force": "fill_or_kill",
  "post_only": true
}
```

**Fractional-contract trading (`count_fp`)** rolled out March 9, 2026, per-market. `*_fp` fields are fixed-point string quantities (e.g. `"10.50"`). Markets carry `fractional_trading_enabled: true|false`. Integer `count` fields are deprecated.

For C++: switch order-size types from `uint32_t` to a `FixedPoint` struct or a `double` with strict string-serialization. Never round silently at the API boundary.

### 2.2 New endpoints worth wiring up

| Endpoint | Landed | Why it matters |
|---|---|---|
| `GET /trade-api/v2/markets/orderbooks` | Mar 30 2026 | Batch up to 100 orderbooks in one request — cold-start from minutes to seconds |
| `GET /historical/markets/{ticker}/candlesticks` | Feb 19 2026 | First-party historical candles — no more scraping |
| `GET /historical/fills` `GET /historical/orders` | Feb 19 2026 | Programmatic backtesting data |
| `GET /historical/trades` | Mar 6 2026 | Public historical trade tape |
| `GET /account/limits` | Jan 28 2026 | Read your RPS tier programmatically at startup |
| `user_orders` WS channel | Feb 3 2026 | Private ordered stream — replaces polling |
| `order_group_updates` WS channel | Jan 22 2026 | Bracket/OCO groups |
| `multivariate_market_lifecycle` WS channel | Feb 12 2026 | KXMVE- prefixed multivariate events |
| `communications` WS channel (sharded) | Jan 12 2026 | RFQ consumers at scale |
| `GET /forecast_percentiles_history` | Sep 11 2025 | Kalshi's crowd-aggregated forecast distribution — calibration benchmark |
| `GET /series/fee_changes` | Sep 21 2025 | Series-specific fee schedules — don't assume global formula |
| `min_updated_ts` filter on `GET /markets` | Jan 21 2026 | Incremental market catalog sync |
| `POST /portfolio/subaccounts` | Jan 9 2026 | Per-strategy capital isolation (N/A at $100) |

### 2.3 Authentication — C++ canonical signing

Canonical signing payload: `str(timestamp_ms) + method.upper() + path` — **no separators, no trailing newline, no query string**.

- **Algorithm**: RSA-PSS with SHA-256, MGF1(SHA-256), salt length = DIGEST_LENGTH (32 bytes).
- **Encoding**: standard Base64 (not Base64URL).
- **Headers**: `KALSHI-ACCESS-KEY` (key ID), `KALSHI-ACCESS-SIGNATURE` (base64), `KALSHI-ACCESS-TIMESTAMP` (ms-since-epoch string).

OpenSSL C++ pattern:

```cpp
EVP_MD_CTX* ctx = EVP_MD_CTX_new();
EVP_PKEY_CTX* pctx = nullptr;
EVP_DigestSignInit(ctx, &pctx, EVP_sha256(), nullptr, pkey);
EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING);
EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, 32);  // or -1 for digest length
EVP_PKEY_CTX_set_rsa_mgf1_md(pctx, EVP_sha256());
EVP_DigestSignUpdate(ctx, payload.data(), payload.size());
// ... finalize and base64-encode
```

**Six common auth bugs** (all observed in production):

1. Including query string in signed path (most frequent 401 cause).
2. Signing timestamp in seconds rather than milliseconds.
3. Appending `\n` to payload (`printf` footgun).
4. Using RSA-PKCS1 v1.5 padding instead of PSS (OpenSSL default is v1.5).
5. URL-encoding the path before signing.
6. Base64URL encoding (`-_`) instead of standard Base64 (`+/`).

There is no token to refresh — each request is signed with the private key independently. Server tolerates ~5s clock skew; keep NTP synced.

### 2.4 WebSocket operational best practices

**URLs**:
- Prod: `wss://trading-api.kalshi.com/trade-api/v2/ws`
- Demo: `wss://demo-api.kalshi.co/trade-api/v2/ws`

**Orderbook replay**: server sends `orderbook_snapshot` followed by monotonic-`seq` `orderbook_delta` stream. Quantity 0 = delete level.

**Gap recovery**: if `delta.seq != local_seq + 1`, discard local book and **resubscribe** — no incremental resync endpoint exists. This is the single most important invariant. Code should panic-resync on any gap.

**Reconnect policy**:
- Exponential backoff: 1s → 2s → 4s → 8s → 16s → 30s cap, with ±25% jitter.
- Server kill idle connections at ~60s; send ping frames every 20-30s if no server traffic.
- On reconnect: re-sign, re-auth, re-subscribe, discard local books, apply fresh snapshots.
- No permessage-deflate compression.
- Prefer `ts_ms` (ms timestamps, added Mar 12, 2026) over `ts` (seconds, deprecated Apr 15, 2026).

**Concurrent connections**: 200 per user at Basic, scaling with tier (Sep 18, 2025 change).

### 2.5 Order management gotchas

- **`client_order_id`** is your idempotency key. Unique per account per 30-day rolling window. On `POST /portfolio/orders` timeout, retry with the same `client_order_id` — Kalshi returns the existing order rather than duplicating.
- **Amend via `PUT /portfolio/orders/{order_id}/amend`** — cheaper and preserves queue position if only count decreases. Any price change or count increase re-queues.
- **Batch**: `POST /portfolio/orders/batched`, up to 20 orders, not atomic — parse per-order errors. `post only cross` error returned when post-only would cross (since Oct 24, 2025, instead of silent cancel).
- **`BatchCancelOrders`**: each cancel counts as **0.2 transactions** against your write RPS — cheapest path for a kill-switch mass-cancel.
- **Specific error codes** (Jan 26, 2026): `invalid_order_size`, `available_balance_too_low`, `order_id_and_client_order_id_mismatch`, `order_side_mismatch`, `order_ticker_mismatch`. Map to typed C++ exceptions.
- **Fills via `user_orders` WS channel**: strictly ordered per-order, includes `fee_cost` as dollar string since Jan 29, 2026.

### 2.6 Orderbook one-sided representation

Kalshi orderbooks return **bids only** — both YES bids and NO bids. **The YES ask book is derived from NO bids**:

```
ask_YES(p) = 1.00 − p_NO_bid
qty_ask_YES = qty_NO_bid
```

Any NO bid at price `q` is mechanically a YES offer at `1 − q`. This is the single most important microstructure invariant for quoting.

### 2.7 Fee structure (still accurate)

```
taker_fee_cents = ceil(100 × 0.07   × C × P × (1 − P))   // max 1.75¢ at P=0.50
maker_fee_cents = ceil(100 × 0.0175 × C × P × (1 − P))   // max 0.44¢ at P=0.50
```

No settlement fee. **Check `GET /series/fee_changes` at startup** — some series now have custom schedules.

### 2.8 Incentive programs (both run through Sep 1, 2026)

**Liquidity Incentive Program**: $10-$1,000/day per market for resting orders, sampled at random intra-second snapshots. Eligible to members except MM-agreement holders and FCM/IB customers. At $100 capital you qualify; realistic payout is meaningful only on low-volume books where you can consistently be top-of-book.

**Volume Incentive Program**: up to $0.005/contract traded — effectively a -0.5¢ rebate, stacks with LIP and regular fees.

**Market Maker Agreement** — separate program, quote/volume obligations, reduced fees, ineligible for LIP. Contact Kalshi directly; not relevant until $10K+ capital.

### 2.9 Regulatory landscape — April 2026

**Third Circuit, April 6, 2026 — *KalshiEX v. Flaherty***: first federal appellate ruling that the Commodity Exchange Act preempts state gambling laws for CFTC-DCM sports event contracts. Clears NJ and Third Circuit for Kalshi sports.

Still contested / varying:
- **Nevada**: pre-Flaherty district-court injunction on Kalshi sports; status post-Flaherty unresolved.
- **Massachusetts**: state-court injunction effective Mar 8, 2026; state-court venue so Third Circuit doesn't bind.
- **Maryland**: district court refused preemption relief.
- Six state AGs signal potential SCOTUS review.
- **CFTC (Feb 2026)**: affirmed "exclusive jurisdiction" over event contracts.

For automation: Kalshi server-side geofences (403 on disallowed states), but preflight-check `available_on_brokers` per event. The OpenAPI `category` field is deprecated (Mar 20, 2026) — filter on `series_ticker` prefix patterns.

Other: PATRIOT Act KYC (SSN+ID); 1099-B issued for realized gains; wash-sale rules do **not** apply to event contracts (consult CPA); standard CFTC Rule 40.11 prohibited events (assassination, terrorism, etc.).

### 2.10 Tooling

- **OpenAPI spec**: `https://docs.kalshi.com/openapi.yaml` — generate C++ types from this, don't hand-roll.
- **Changelog RSS**: `https://docs.kalshi.com/changelog/rss.xml` — poll on cron for breaking changes.
- **Python SDK**: `kalshi-python` on PyPI, generated from OpenAPI.
- **Third-party references** (study these for C++ patterns):
  - `github.com/ammario/kalshi` (Go, clean design)
  - `github.com/rmadev01/kalshi-rs` (Rust HFT)
  - `github.com/pbeets/kalshi-trade-rs` (Rust orderbook)
- **Discord**: `discord.gg/kalshi`, `#api-help` channel. Email `api@kalshi.com` for auth, `institutional@kalshi.com` for FIX/tier.

### 2.11 FIX 4.4 — institutional-gated

Still institutional-access-only. Email `institutional@kalshi.com`, pass review. Not relevant for a $100 bot; post-Prime-tier territory.

---

## 3. Data Sources: Weather & Tropical Cyclones

### 3.1 AI weather models — the 2024-2026 revolution

AI-native weather models are now state-of-the-art, routinely beating ECMWF HRES at a fraction of the compute cost. For a solo quant on Kalshi temperature contracts, this is the single biggest available edge.

#### GraphCast (Google DeepMind, 2023)
- Paper: [Science, Dec 2023](https://www.science.org/doi/10.1126/science.adi2336)
- Repo: <https://github.com/google-deepmind/graphcast> (Apache 2.0; weights released)
- **Skill**: beats HRES on 90%+ of 1380 variable/lead-time combos at 10-day horizon. ~5-10% RMSE reduction on 2m temperature, often more over CONUS.
- **Resolution**: 0.25° (~28 km), 6-hour steps to 10 days.
- Access:
  - Google Earth Engine: `projects/deepmind-graphcast/assets/operational` (4x/day).
  - WeatherBench2 precomputed: <https://weatherbench2.readthedocs.io>
  - Open-Meteo serves GraphCast: <https://open-meteo.com/en/docs/gfs-graphcast-api>
- Local run: ~32 GB RAM, ~1 TPUv4 for full 10-day run, or ~20 min on an A100.

#### GenCast (DeepMind, Dec 2024)
- Paper: [Nature, Dec 2024](https://www.nature.com/articles/s41586-024-08252-9)
- **Diffusion-based ensemble** — generates 50+ probabilistic trajectories. Beats ECMWF ENS on 97% of targets at 1-15 day lead.
- **Killer feature for Kalshi**: explicitly skillful on **tropical cyclone tracks** (up to 12 h additional lead over ENS); also excellent on temperature-distribution tails (critical for `P(T_max > threshold)`).
- Weights: `gs://dm_graphcast/gencast/params/`
- Precomputed ensembles via WeatherBench2; no free real-time public API yet. Run on A100 (~$1-2/day on Lambda/RunPod) or wait for Open-Meteo integration.

#### ECMWF AIFS — operational since Feb 2025
- Blog: <https://www.ecmwf.int/en/about/media-centre/aifs-blog>
- **Roughly on par with GraphCast**, slightly better on some surface variables.
- **Open Data**: <https://www.ecmwf.int/en/forecasts/datasets/open-data> — free CC-BY-4.0, 0.25° GRIB2, 4x/day.
- Python: `pip install ecmwf-opendata`
- S3 path: `s3://ecmwf-forecasts/YYYYMMDD/HHz/aifs-single/0p25/oper/`
- Latency: ~4 hours from cycle time.

#### Pangu-Weather (Huawei, 2023)
- Paper: [Nature, Jul 2023](https://www.nature.com/articles/s41586-023-06185-3)
- Repo: <https://github.com/198808xc/Pangu-Weather>
- First AI model to beat HRES on TC tracks. Runs in ~1.4s per forecast on a single V100. ONNX-friendly.
- Weights on Google Drive.

#### FourCastNet v2-SFNO (NVIDIA, 2024)
- In NVIDIA Modulus: <https://github.com/NVIDIA/modulus>
- Access via NVIDIA Earth-2 API (paid) or self-host.
- Specialized for extreme events and tropical cyclone genesis.

**Recommendation**: Consume AIFS (ECMWF open data) + GraphCast (via Open-Meteo) + NBM (NOAA blend) as three independent frontier models, average/stack them, and pull GenCast ensembles from WeatherBench2 for tail-risk pricing. All free.

### 3.2 NOAA operational probabilistic forecasts

#### NBM — National Blend of Models (critical)
- Docs: <https://vlab.noaa.gov/web/mdl/nbm>
- Current: v4.2 (2024-2025). Blended + bias-corrected + calibrated combination of GFS, ECMWF, NAM, HRRR, RRFS, etc.
- Resolution: 2.5 km CONUS, hourly to 36h, 3-hourly to 192h, 6-hourly to 264h.
- Variables: `TMAX`, `TMIN` are **probabilistic** — 1st/10th/25th/50th/75th/90th/99th percentiles available.
- Access:
  - NOMADS: `https://nomads.ncep.noaa.gov/pub/data/nccf/com/blend/prod/blend.YYYYMMDD/HH/core/`
  - AWS (low-latency, free, no auth): `s3://noaa-nbm-grib2-pds/` — <https://registry.opendata.aws/noaa-nbm/>
- Core blend cycles: 01, 07, 13, 19 UTC.
- **This is your primary settlement-day probability source for daily-high-temp contracts.**

#### NDFD — National Digital Forecast Database
- <https://digital.weather.gov/> — downstream of NBM + forecaster edits.
- REST: `https://digital.mdl.nws.noaa.gov/xml/` (DWML XML, legacy but current).
- Use NBM directly for raw probability signal; NDFD for "what's the official forecast".

#### NWS API (api.weather.gov)
- No auth (just User-Agent). Soft-unlimited rate.
- `GET /points/{lat},{lon}` → follow `properties.forecastGridData`.
- Note: **no probabilistic temperature in the public API**. Use NBM GRIB2 directly.

#### HREF — High-Resolution Ensemble Forecast
- <https://www.spc.noaa.gov/exper/href/> — 10-member ensemble (HRRR + NAM-3km + time-lagged).
- Best for 0-48h severe/convective probabilities. NBM dominates for daily-max at metro stations.

#### RAP / HRRR — nowcasting
- HRRR: 3 km CONUS, hourly updates, 18h forecast (48h at synoptic cycles).
- AWS: `s3://noaa-hrrr-bdp-pds/` — <https://registry.opendata.aws/noaa-hrrr-pds/>
- **Last-mile nowcasting on settlement day**: if KNYC contract settles at 8pm ET, HRRR at 18Z gives you the best 2-8h forecast of remaining daily-max potential.

### 3.3 Historical & reforecast data (for training)

#### GEFS Reforecast v12 — the backtest gold
- <https://registry.opendata.aws/noaa-gefs-reforecast/>
- S3: `s3://noaa-gefs-retrospective/`
- Coverage: 2000-01-01 to 2019-12-31, 11 members, daily 00Z, out to 35 days.
- Resolution: 0.25° days 1-10, 0.5° days 10-35.
- GRIB2. **Train EMOS/QRF calibration on this — your single most valuable dataset.**

#### ERA5 reanalysis
- CDS: <https://cds.climate.copernicus.eu/> (`pip install cdsapi`, free account)
- Fastest access: `s3://era5-pds/` and `gs://gcp-public-data-arco-era5/` (zarr-optimized).
- Coverage: 1940-present, 0.25°, hourly. Ground truth for calibration training + pseudo-observations.

#### GHCN-Daily — station records
- Bulk: `https://www.ncei.noaa.gov/pub/data/ghcn/daily/all/`
- By year: `https://www.ncei.noaa.gov/pub/data/ghcn/daily/by_year/`
- NCEI web service: <https://www.ncei.noaa.gov/cdo-web/webservices/v2> (token-auth, 1000 req/day free).

**Kalshi-relevant station IDs** (GHCN-D format):

| City | Primary | Alternate | Notes |
|---|---|---|---|
| NYC | `USW00094728` (Central Park) | `USW00014732` (LGA), `USW00094789` (JFK) | Per-contract — **read rulebook** |
| Chicago | `USW00094846` (ORD) | | |
| Miami | `USW00012839` (MIA) | | |
| Los Angeles | `USW00023174` (LAX) | | |
| Denver | `USW00003017` (DIA) | | |
| Austin | `USW00013904` (AUS) | | |

#### Iowa Environmental Mesonet — 1-minute ASOS
- <https://mesonet.agron.iastate.edu/request/asos/1min.phtml>
- Free, extremely useful for settlement verification and intraday nowcasting of day-max progress.

### 3.4 Hurricane / tropical data

#### HAFS — Hurricane Analysis and Forecast System
- <https://hafs.noaa.gov/>
- **Operational since June 2023, fully replaced HWRF/HMON in 2024.**
- Configurations: HFSA (Atlantic-optimized), HFSB (statistical bias correction).
- Resolution: 2 km moving nest, 6 km parent, 126h forecast, 6-hourly cycles during active TCs.
- Access: `s3://noaa-nws-hafs-pds/` and NOMADS `/com/hafs/prod/`.
- GRIB2.

#### NHC products
- <https://www.nhc.noaa.gov/>
- Feeds:
  - RSS/ATOM: <https://www.nhc.noaa.gov/index-at.xml>
  - GIS/KMZ: <https://www.nhc.noaa.gov/gis/>
  - Text archives: <https://www.nhc.noaa.gov/archive/>
  - Raw track FTP: <ftp://ftp.nhc.noaa.gov/atcf/> (A-decks = forecast, B-decks = best track)
- **Probabilistic wind-speed product**: <https://www.nhc.noaa.gov/aboutnhcprobs2.shtml> — P(34/50/64 kt winds) at every coastal point, 5-day lead. **Perfect for landfall-intensity Kalshi contracts.**
- **P-Surge 2.5**: <https://www.nhc.noaa.gov/surge/psurge.php> — probabilistic storm surge.

#### HURDAT2 — best-track historical
- <https://www.nhc.noaa.gov/data/hurdat/>
- Atlantic 1851-present, East Pacific 1949-present.
- For seasonal-count base rates and landfall climatology.

#### Seasonal forecasts
- **CSU Tropical Forecast** (Klotzbach et al.): <https://tropical.colostate.edu/> — twice-yearly + real-time updates; CSV downloadable.
- **NOAA CPC ENSO**: <https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso_advisory/> — weekly state; strong Atlantic-season predictor.
- **TSR (Tropical Storm Risk, UCL)**: <http://www.tropicalstormrisk.com/>

#### TIGGE multi-center ensemble tracks
- <https://confluence.ecmwf.int/display/TIGGE>
- Free multi-center (NHC, ECMWF, UKMO, JMA) TC tracks — research license, raw tracks public domain.

### 3.5 Station-level gotchas (critical for settlement)

#### KNYC (Central Park, `USW00094728`)
- **Urban heat island adds ~2-4°F** vs regional models on calm clear nights.
- NBM underestimates daily max on summer sunny days by ~1°F.
- Tree canopy at Belvedere Castle sensor affects morning warming.
- Sensor history: moved Belvedere Castle rooftop → ground 1995; ASOS upgrade HO-83 → HO-1088 in 2018. Pre-1995 and pre-2018 GHCN needs bias correction.

#### KORD (O'Hare, `USW00094846`)
- **Lake Michigan lake-breeze reversal**: east-wind summer days can be 5-10°F below HRRR guidance. HRRR + HREF resolve the lake breeze front; GEFS/NBM do not.
- S/SW flow = hot; E/NE lake-fetch = cool. Wind direction is the variable the model cares about most.
- ASOS relocated on airfield 2018 (small shift).

#### KMIA (Miami Intl, `USW00012839`)
- **Biscayne Bay sea breeze** caps summer daily max at 89-92°F; model warm bias on calm synoptic days.
- Afternoon thunderstorms abruptly cap temp; HRRR + HREF outperform globals.
- During active TCs, standard temp models fail — use HAFS inner nest.

#### KLAX (`USW00023174`)
- **Marine layer / June gloom** — GFS/ECMWF chronically 2-4°F warm on marine-layer days. NBM handles this better via HRRR input.

#### KDEN (`USW00003017`)
- **Chinook downslope warming** can spike max 20+°F above guidance. HRRR/RAP strength.

#### KAUS (`USW00013904`)
- Dryline passages, Gulf return flow. Texas heat-dome days well-forecast; transitional days are not.

#### Kalshi settlement rule — verify per contract
Most daily-high-temperature contracts settle on the **maximum reported temperature between midnight and 11:59 PM local time at the designated NWS ASOS station**, as reported in the **daily CF6 climate summary** (issued next morning). The **CF6 is authoritative**, not real-time METAR — it sometimes corrects sensor glitches. Pull historical CF6 at:
```
https://forecast.weather.gov/product.php?site=NWS&issuedby={STATION}&product=CF6
```

### 3.6 C++ integration notes

- **GRIB2**: `eccodes` (ECMWF) — mature, well-documented.
- **NetCDF**: `netcdf-cxx4`.
- **HTTP**: `cpr` (already in project idioms).
- **JSON**: `nlohmann/json` (already in project).
- **zarr**: **no good C++ client in 2026**. Easiest path: Python sidecar (`xarray` + `zarr` + `cfgrib`) writes station-specific CSVs to shared directory, consumed by C++. Don't try to read zarr natively in C++.

---

## 4. Data Sources: Macroeconomics

### 4.1 Inflation / CPI

#### Cleveland Fed Inflation Nowcast (primary)
- <https://www.clevelandfed.org/indicators-and-data/inflation-nowcasting>
- Stock-Watson UCSV extension — multivariate regression on daily oil (WTI), weekly gasoline (EIA), monthly CPI/PCE lags.
- **Historical CSV**: `https://www.clevelandfed.org/-/media/files/webcharts/inflationnowcasting/inflation-nowcasting.csv` — scrape hourly.
- RMSE ~0.10-0.12 pp on headline MoM CPI — materially better than naive AR.
- Free.

#### Zillow ZORI (Observed Rent Index)
- <https://www.zillow.com/research/data/>
- FRED: `USARENTQQE`; direct CSV: `https://files.zillowstatic.com/research/public_csvs/zori/`
- **Leads CPI Shelter by 12-16 months** because BLS uses 6-month panel rotation. Shelter is ~34% of CPI — **the biggest single edge for CPI shelter nowcasting**.
- Model: `CPI_shelter_mom(t) = α + Σ β_i · ZORI_mom(t−i), i ∈ [12, 18]`.

#### EIA Energy
- <https://www.eia.gov/opendata/> (free API key)
- Weekly gasoline: `PET.EMM_EPM0_PTE_NUS_DPG.W` (Mon 5pm ET)
- Daily WTI: `PET.RWTC.D`; Brent: `PET.RBRTE.D`
- `https://api.eia.gov/v2/seriesid/PET.EMM_EPM0_PTE_NUS_DPG.W?api_key=XXX`
- Energy ≈ 6-7% of CPI. Gasoline CPI: `gas_cpi_mom ≈ 0.95 × eia_retail_gas_mom` (near-perfect correlation).

#### Manheim MUVVI (Used Vehicle Value Index)
- <https://publish.manheim.com/en/services/consulting/used-vehicle-value-index.html>
- Monthly mid-month + **weekly "MUVVI flash" Tuesdays**.
- Used cars ≈ 2.5% of CPI but very volatile.
- Free historical; paid real-time via Cox Automotive.

#### Truflation (daily alt-inflation)
- <https://truflation.com>, API docs: <https://docs.truflation.com>
- Aggregates Numerator receipt data + Zillow + Realtor.com + government sources.
- Runs 30-150 bps below official CPI (different basket, no OER) — use for direction, not level. **Orthogonal signal, not CPI-comparable.**
- Free tier: daily index; granular categories $500-2000/mo.

#### Other inflation sources
- **USDA Food Price Outlook** (Food 13% of CPI): <https://www.ers.usda.gov/data-products/food-price-outlook/>
- **ISM Prices Paid** (first business day, pre-CPI): FRED `NAPMPRI`, `NAPMNMP`
- **NY Fed Underlying Inflation Gauge**: FRED `UIGFULL`, `UIGPRIC`
- **Atlanta Fed Sticky vs Flexible Price CPI**: FRED `STICKCPIM157SFRBATL`, `CORESTICKM159SFRBATL`
- **Case-Shiller / CoreLogic HPI** (OER inputs, 24-36 month horizon): FRED `CSUSHPISA`
- **Adobe Digital Price Index** (online-only inflation, free PDF): <https://business.adobe.com/resources/digital-price-index.html>

### 4.2 Employment / NFP

#### Jobless Claims (strongest high-freq labor signal)
- DOL: <https://www.dol.gov/ui/data.pdf>
- FRED: `ICSA` (initial), `CCSA` (continuing). Thu 8:30am ET.
- Use 4-week MA. Rule of thumb: +10k on 4wk avg → ~20-40k negative NFP impact.

#### ADP National Employment (2d pre-NFP)
- <https://adpemploymentreport.com>
- **Methodology overhauled 2022 with Mark Zandi** — now uses real ADP payroll processing on 25M+ employees. Correlation with NFP: ~0.6 post-2022 (lower than pre-2020).
- FRED: `ADPMNUSNERSA`. Free.

#### Indeed Hiring Lab (daily postings)
- <https://www.hiringlab.org/indeed-data/>
- FRED: `IHLIDXUSTPHYTOTL` (daily, 7-day MA, ~3-day lag).
- GitHub: <https://github.com/hiring-lab/data>
- **Leading indicator**: JOLTS openings (~1 month lead), NFP (~2-3 month lead).

#### Other labor sources
- **Homebase Employment** (SMB payroll, weekly): <https://joinhomebase.com/data/>
- **LinkUp Job Market Tracker** ($2-10k/mo paid, higher quality): <https://www.linkup.com/data/>
- **Revelio Labs** (LinkedIn-derived, $25k+/yr enterprise, workforce flow leads NFP ~2 weeks)
- **Challenger Gray & Christmas** layoffs (monthly, Thu pre-NFP)
- **JOLTS**: FRED `JTSJOL`, `JTSQUR` (quits rate = wage pressure)

### 4.3 GDP

#### Atlanta Fed GDPNow (primary)
- <https://www.atlantafed.org/cqer/research/gdpnow>
- Bridge equations — 13 GDP subcomponents each nowcast from monthly indicators, aggregated with BEA weights.
- Data: `https://www.atlantafed.org/-/media/documents/cqer/researchcq/gdpnow/gdpnow-forecast-evolution.xlsx` (full history).
- MAE 0.6pp vs advance BEA release since 2011.
- **Known "GDPNow overshoot"**: early-quarter estimates have MAE ~1.5pp, tightening to ~0.6pp by quarter-end. **Mean-revert first-month estimate by ~40% toward consensus.**

#### Dallas Fed & NY Fed Weekly Economic Indices
- Dallas Fed WEI (Lewis-Mertens-Stock): <https://www.dallasfed.org/research/wei>
- NY Fed WEI: <https://www.newyorkfed.org/research/policy/weekly-economic-index> (FRED `WEI`)
- Both are dynamic factor models over 10 weekly series; Friday evening updates.

#### Philly Fed ADS Index (Aruoba-Diebold-Scotti)
- <https://www.philadelphiafed.org/research-and-data/real-time-center/business-conditions-index>
- Daily-frequency dynamic factor model. Negative = below trend. FRED: update Fri evenings.

#### NY Fed Multivariate Core Trend (MCT)
- New 2023 inflation model: <https://www.newyorkfed.org/research/policy/multivariate-core-trend>
- Free.

### 4.4 Fed rate decisions

#### CME FedWatch — methodology
Fed funds futures (ZQ contracts) settle on monthly average EFFR. For a meeting on day `m` of `N`:
```
ZQ_settle = (m/N) · EFFR_pre + ((N − m)/N) · EFFR_post
```
Solve for `EFFR_post`, convert to hike/cut probability assuming 25bp moves:
```
P(hike to bucket X) = (EFFR_post − rate_if_no_action) / (rate_if_action − rate_if_no_action)
```
- Tool: <https://www.cmegroup.com/markets/interest-rates/cme-fedwatch-tool.html>
- Raw data: CME DataMine (paid) or unofficial JSON `https://www.cmegroup.com/CmeWS/mvc/Quotes/Future/305/G`.
- Free delayed: Yahoo `ZQ=F`.

#### SOFR futures vs fed funds
Post-LIBOR-transition (2023+), SR1 (1-month SOFR) and SR3 (3-month) are more liquid than fed funds futures. **Use fed funds for front contract (next meeting), SOFR for 6+ months out.**

#### NY Fed Primary Dealer Survey
- <https://www.newyorkfed.org/markets/primarydealer_survey_questions.html>
- ~2 weeks pre-FOMC, PDF only — parse with `pdfplumber`.

#### Nick Timiraos / WSJ Fed signaling
During blackout (10 days pre-FOMC), Timiraos articles can move rate probabilities 5-20pp intraday. **Kalshi Fed contracts reprice within 2-5 min of publication.** Operational ingest:
- RSS filter on byline "Nick Timiraos" + keywords ("Fed officials," "rate cut/hike," "likely").
- Classify via Claude/GPT API.
- Compare to pre-article OIS-implied rate. Edge narrow but real.

### 4.5 High-frequency / vintage data (for honest backtesting)

#### ALFRED — Archival FRED
- <https://alfred.stlouisfed.org>
- Every vintage of every FRED series. **Essential for avoiding forward-looking bias** — using current (revised) NFP to backtest 2022 strategy is wrong.
- API: same as FRED + `&vintage_dates=2024-03-08`.
- Python: `fredapi.get_series_as_of_date()`.

#### FRED-MD / FRED-QD (Michael McCracken)
- <https://research.stlouisfed.org/econ/mccracken/fred-databases/>
- Curated monthly (128 series) / quarterly (246 series) datasets, already stationarity-transformed.
- **Standard input for dynamic factor nowcasters.** Free CSV, monthly updates.

### 4.6 C++ integration notes

All FRED/EIA/Zillow/Cleveland Fed data is either CSV or JSON. Use `cpr` for HTTP, `nlohmann/json` for JSON, write a minimal CSV parser (`std::getline` + `std::stod`, 30 lines). Cache everything to SQLite — data is small (MBs) and refresh latency matters.

---

## 5. Data Sources: Market-Implied Priors

External reference markets (options, futures) often give the single best estimate of the "true" probability for a Kalshi contract. Extracting these is a high-leverage data-engineering win.

### 5.1 Breeden-Litzenberger — risk-neutral density from options

Foundational result (Breeden-Litzenberger 1978):
```
f(K) = e^(rT) · ∂²C(K,T)/∂K²
F(K) = 1 + e^(rT) · ∂C/∂K         ← digital put value, directly a Kalshi yes/no payoff
```

**Numerical recipe** (smooth in IV space, not price space):

```python
from scipy.interpolate import UnivariateSpline
import numpy as np

iv_spline = UnivariateSpline(K, iv_mid, k=4, s=len(K)*1e-6)
K_fine = np.linspace(K[0], K[-1], 2000)
C_fine = bs_call(S, K_fine, r, T, iv_spline(K_fine))
pdf = np.exp(r*T) * np.gradient(np.gradient(C_fine, K_fine), K_fine)
cdf = np.cumsum(pdf) * (K_fine[1] - K_fine[0])
```

Smoothing in IV regularizes the second-derivative noise; smoothing in price space amplifies bid-ask noise. Use SVI or SABR for strike extrapolation — never extrapolate splines.

**Q vs P measure**: Breeden-Litzenberger gives the risk-neutral (Q) density. Short-dated (<3 mo) equity indices have Q ≈ P for non-tail outcomes. Deep-tail long-dated needs Girsanov correction or Ross (2015) Recovery Theorem / Stutzer minimum-entropy.

References: Figlewski 2008 ([SSRN](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1256783)) is the best practical treatise; Jackwerth 2004 monograph covers the full literature.

### 5.2 Volatility surface parametrization

- **SVI (Gatheral 2004)**: `w(k) = a + b{ρ(k−m) + √((k−m)² + σ²)}`. 5 parameters per expiry, asymptotically linear wings (matches Lee moment formula). Check Lee/butterfly arbitrage constraints.
- **SSVI (Gatheral-Jacquier 2014)**: extends to term structure.
- **SABR (Hagan et al. 2002)**: `dF = σ F^β dW`, `dσ = νσ dZ`, `ρ = corr(dW,dZ)`. Best for rates options; Hagan formula breaks at low rates — use shifted SABR or ZABR.
- **Heston**: stochastic vol with mean reversion; calibrate via FFT pricing (Carr-Madan). Heavier but natural term structure.
- **Local polynomial / kernel smoothing** (Ait-Sahalia-Lo 1998): nonparametric, interior only.

**For Kalshi**: SVI per expiry, smooth the 5 parameters across expiries. `py_vollib` for BS inversion, implement SVI in ~30 LOC.

### 5.3 Digital = Kalshi contract (synthetic replication)

A Kalshi "yes at K" literally *is* a digital option. Synthesize:
```
digital_call(K) ≈ (C(K − ε) − C(K + ε)) / (2ε)
```
Tight call spread. Arbitrage bounds for Kalshi yes price at K:
```
[(C(K) − C(K − ε))/ε, (C(K + ε) − C(K))/ε · e^(−rT)]
```
Violations are **statically hedgeable** — short Kalshi, long call spread. Carr-Madan (1998) replication: any European payoff = cash + stock + ∫call payoffs weighted by second derivative of payoff. Use ε = 0.5-1% of spot for stable numerics.

### 5.4 Reference markets by Kalshi category

| Kalshi contract | Reference market | Data source |
|---|---|---|
| FOMC rate decision | CME ZQ (fed funds) + SOFR futures | CME FedWatch methodology (above) |
| S&P 500 close above X | SPX options | Polygon ($29/mo), Tradier, IBKR, CBOE |
| VIX above X | VIX futures + VIX options (VVIX) | CBOE, Polygon |
| BTC/ETH above X | Deribit options | <https://www.deribit.com/api/v2/public/get_book_summary_by_currency> (free) |
| 10Y yield above X | CBOT Treasury options / OIS swaption | IBKR, CME, BrokerTec indicative |

### 5.5 Kalshi as a reference market for itself

`GET /forecast_percentiles_history` (landed Sep 11, 2025) returns Kalshi's own crowd-aggregated forecast distribution. **Use as a calibration benchmark** — compare your model to the aggregate, not just the price. Markets occasionally price differently than the aggregate forecast, signaling either a micro-structural mispricing or a wrong forecast.

---

## 6. Data Sources: Polling, Sentiment, Miscellaneous

### 6.1 Political / election polling

**538 shutdown (2024-2025)**: the quintessential polling aggregator closed. Data legacy:
- Historical CSV dumps: <https://github.com/fivethirtyeight/data>
- **Silver Bulletin** (Nate Silver's Substack, closed-source).
- Open-source replicas worth studying:
  - *The Economist*'s state-space model in Stan: <https://github.com/TheEconomist/us-potus-model-2020>
  - Simpler: <https://github.com/bandrews568/race-to-wh>
- **PollBase** (*Economist*): cleanest long-run polling dataset.

**538 methodology (replicate for your own aggregation)**:
- Pollster house-effects regression
- √n Bayesian shrinkage on sample size
- Recency decay, half-life 2-3 weeks (`exp(−days_ago / 14)`)
- Trend-line correction via national polling shifts
- Correlated-t distributions over demographic states for national→state

**Simple starting recipe**: Bayesian Beta prior with `α₀ + β₀ ≈ 10`, recency-weight polls, √n pooling. State-level: MRP (multilevel regression with poststratification) — Gelman's blog has worked examples.

**Not relevant until 2027 US election cycle heats up**, per `KALSHI_RESEARCH.md` §11.8.

### 6.2 Sports (avoid, but the data is here)

- **Pinnacle closing lines** — sharpest consensus globally. Scrape or via Pinnacle API (paid).
- **Betfair exchange** — market prices.
- **Odds API**: <https://the-odds-api.com/> — free tier, aggregated odds.
- **ESPN** / sports-reference.com — historical stats, Elo.

### 6.3 Entertainment / awards (opportunistic only)

- **Box Office Mojo**: daily box office.
- **Netflix Top 10**: <https://www.netflix.com/tudum/top10> (Tue).
- **Gold Derby** (prediction aggregator, expert + fan).
- **Metacritic, Rotten Tomatoes**.
- **Google Trends** — free search-volume signals.

### 6.4 Natural disasters / miscellaneous

- **USGS earthquakes**: <https://earthquake.usgs.gov/fdsnws/event/1/> (real-time).
- **NASA EONET events**: <https://eonet.gsfc.nasa.gov/api/v3> (volcanoes, wildfires).
- **CDC FluView / COVID-19 tracker**: for health markets.
- **BEA corporate profits / BLS productivity** for occasional macro markets.

---

## 7. Algorithms: Probability Forecasting

This section covers the model layer — going from raw data to a raw probability. §8 covers the calibration layer on top.

### 7.1 Weather ensemble → probability

**Step 1: raw ensemble probability.** Count ensemble members exceeding threshold. For GEFS 31-member at `P(T_max > 75°F)`:
```
p_raw = |{members where T_max_i > 75}| / 31
```

**Step 2: post-processing (EMOS / NGR).** Ensemble variance is typically 20-40% too narrow. Gneiting et al. 2005:
```
Y | ensemble ~ Normal(μ, σ²)
μ     = a + b · mean(ensemble)       (or a + Σ b_i · member_i for per-member weights)
σ²    = c + d · var(ensemble)
```
Fit by **CRPS minimization** (not MLE — CRPS gives better tail calibration). Closed-form gradient exists.

**Step 3: derive probability from calibrated Gaussian.**
```
P(T_max > threshold) = 1 − Φ((threshold − μ) / σ)
```

**Alternative: Quantile Regression Forest (Taillardat 2016)**. Random forest predicting conditional quantiles of the observation from ensemble summary stats + station features + calendar features. Advantages over EMOS: handles non-Gaussian tails, captures heteroskedasticity. `quantile-forest` Python package. **ECMWF uses this in production for AIFS-ENS post-processing.**

**Recommendation for this bot**: QRF as primary (needs >500 training days per station), EMOS as fallback for thin training data.

### 7.2 Economic nowcasting — bridge regression

**Bridge equations** (Atlanta Fed GDPNow style): separate OLS per GDP subcomponent on its monthly indicators, aggregate with BEA weights. Simple, transparent, doesn't handle ragged edges elegantly.

**For NFP specifically**:
```
NFP_t = α + β_ADP · ADP_surprise_t
          + β_JC  · Δ(4wk jobless claims MA)_t
          + β_ISM · (ISM_employment_t − 50)
          + β_JOLT · JOLTS_openings_t
          + β_Indeed · Indeed_postings_t
          + ε_t
```
Published bridges achieve MAE ~50-70k vs actual consensus MAE ~60-80k. R² 0.25-0.40. ADP alone R² ~0.15.

### 7.3 Dynamic factor models (DFM)

Extract latent factors from many macro series via Kalman filter with state-space:
```
Y_t = Λ F_t + e_t         (observation equation)
F_t = A F_{t-1} + u_t     (state equation)
```

Handles **mixed frequencies** (weekly + monthly + quarterly) and **ragged edges** (different release dates) — the real-world data pattern. What NY Fed's discontinued nowcast used (Giannone-Reichlin-Small 2008).

Python: `statsmodels.tsa.statespace.DynamicFactorMQ`.

### 7.4 MIDAS (Mixed Data Sampling)

Ghysels-Santa-Clara-Valkanov: regress low-frequency target on weighted lags of high-frequency predictors using parameterized weight functions (beta polynomial, exponential Almon):
```
Y_t = α + β · Σ w(θ, k) · X_{t-k/m} + ε_t
```
Concentrates information near release dates. Better than simple averaging. Python: `midaspy`.

### 7.5 LightGBM / gradient boosting

Chinn-Meunier 2024 ("Machine Learning Nowcasting"): **LightGBM beats DFM by 10-20% RMSE on GDP** with 20+ years of data and 50+ features.

```python
import lightgbm as lgb
m = lgb.LGBMRegressor(
    objective='quantile', alpha=0.5,   # median; use 0.1/0.9 for intervals
    n_estimators=500, learning_rate=0.02,
    num_leaves=31, min_data_in_leaf=20,
    feature_fraction=0.8, bagging_fraction=0.8, bagging_freq=5
)
m.fit(X_train, y_train)
```

**Gotcha**: heavy feature-leakage risk — **must** respect release dates via ALFRED vintages. Use SHAP for attribution (boosted trees are opaque).

### 7.6 LSTM / transformer — **usually not worth it for macro**

State of the art on M4/M5 benchmarks (Temporal Fusion Transformer, N-BEATS) but **underperform LightGBM and DFM on macro nowcasting** because macro sample sizes are tiny (300-600 monthly obs). Transformers need 10k+ sequences.

Use transformers only if pooling many countries or series. **2025 consensus: LightGBM + DFM ensemble > transformer for macro.** Transformers win on high-freq intraday data (10k+ obs).

### 7.7 Options-implied probability (reference-market prior)

Already covered §5. Pipeline:
1. Pull option chain mid IVs.
2. Fit SVI per expiry.
3. Breeden-Litzenberger to extract risk-neutral density.
4. Integrate to get `P(outcome < K)` = digital put value.
5. Use as prior; combine with your fundamental model (see §11).

### 7.8 Feature engineering

- **Lead-lag**: ADP 2d lead on NFP; Zillow 12-16mo lead on shelter CPI; PPI 1-3mo lead on CPI goods; ISM 1-quarter lead on GDP.
- **Interactions**: `temp × season_sin`, `CPI × energy_share`, `unemp × labor_force`.
- **Calendar**: FOMC day, days-to-release, day-of-week, post-holiday, pre/post-8:30am.
- **Regime flags**: Sahm rule (3mo unemp > 12mo min + 0.5%), VIX regime (>20), yield-curve inversion.

---

## 8. Algorithms: Calibration

Calibration maps raw model probabilities to empirically-accurate probabilities. This is the single highest-leverage layer in the stack because a miscalibrated model with positive EV in expectation will systematically lose money on bet sizing.

### 8.1 Methods catalog

| Method | N min | N comfy | Output | When to use |
|---|---|---|---|---|
| Temperature scaling (Guo 2017) | 30 | 100 | Point | Baseline only; 1 parameter is too simple for this bot |
| Platt scaling (1999) | 50 | 200 | Point | Sigmoid-shaped miscalibration, small N |
| **Beta calibration (Kull 2017)** | 100 | 300 | Point | **Default** — almost always matches/beats Platt + isotonic |
| Histogram / adaptive binning | 200 | 1000 | Point | Interpretability, baseline |
| Spline (Gupta 2021) | 300 | 1000 | Point | Smooth alternative to isotonic |
| Isotonic (PAVA) | 500 | 2000 | Point | Large N, arbitrary monotonic shape |
| **Inductive Venn-Abers (Vovk 2012)** | 50 | 300 | **Interval** | **Keep running in parallel** — distribution-free intervals |
| Conformal (split / cross) | 100 | 300 | Interval / set | Distribution-free coverage guarantee |
| Dirichlet (Kull 2019) | 300 | 1000 | Multi-class point | Bracket contracts (K-way) |
| EMOS / NGR (Gneiting 2005) | 200 | 1000 | Distribution | Weather ensemble post-processing (pre-binarization) |

### 8.2 Beta calibration (recommended default)

```
μ(s; a, b, c) = 1 / (1 + exp(−(a log s − b log(1 − s) + c)))
```
Three parameters fit via logistic regression on features `[log(s), −log(1−s)]`.

- **Correctly models identity** (perfectly-calibrated inputs pass through — Platt cannot).
- Handles asymmetric miscalibration.
- Works with ~100 samples.

Library: `betacal` (Python, `pip install betacal`). C++: ~50 lines with Eigen + LBFGS.

Paper: [Kull, Silva Filho, Flach 2017](https://arxiv.org/abs/1707.01889).

### 8.3 Inductive Venn-Abers calibration (intervals — critical)

Given calibration set, for a test prediction `s` run isotonic regression twice:
1. On calibration set ∪ `(s, 0)` → probability `p₀`.
2. On calibration set ∪ `(s, 1)` → probability `p₁`.

Output: interval `[p₀, p₁]` (or collapsed point `p₁ / (1 − p₀ + p₁)`).

**Why this is powerful for trading**: the interval width scales with calibration-data thinness at that prediction level. This **automatically widens bet-thresholds when calibration data is sparse at that probability level**. Feed the lower bound into Kelly sizing — trade only if `p₀ > market_ask + edge_threshold`.

**Distribution-free validity guarantee** under exchangeability (Vovk 2012).

Library: `venn-abers` Python (`pip install venn-abers`, author Ivan Petej). C++: implement PAVA once (~40 lines), then two incremental queries per prediction.

### 8.4 Conformal prediction for binary outcomes

**Split conformal** — hold out calibration set of size n, compute nonconformity scores `s_i = |y_i − p̂_i|`, output prediction set `{y : s(x,y) ≤ q_α}` where `q_α = ⌈(n+1)(1−α)⌉/n` empirical quantile. Guarantees `P(y ∈ set) ≥ 1 − α`.

**CQR (Conformalized Quantile Regression)** (Romano-Patterson-Candès 2019) — for weather ensemble temperature predictions, predict quantiles then conformalize. Get prediction intervals with coverage guarantee.

**Cross-conformal** — K-fold residuals, sample-efficient but loses strict validity. Critical for small calibration data (<200 obs).

Key 2023-2026 papers:
- Angelopoulos & Bates 2023, "Conformal Prediction: A Gentle Introduction": <https://arxiv.org/abs/2107.07511>
- Gibbs & Candès 2024, "Conformal Inference for Online Prediction with Arbitrary Distribution Shifts"
- Angelopoulos-Bates-Candès 2024, "Conformal Risk Control": <https://arxiv.org/abs/2208.02814>

Library: `mapie` (best maintained), `crepes`, `nonconformist`.

### 8.5 EMOS for weather (pre-calibration step)

Detailed in §7.1. Apply EMOS/NGR to raw ensemble temperature first, then Beta+IVAP on the resulting `P(T > threshold)`.

### 8.6 Diagnostic tooling

**Reliability diagram**: plot predicted prob (x) vs empirical freq (y) per bin. Diagonal = calibrated. Add 95% Wilson CI bars.

**Adaptive-binning ECE**: `Σ_b (|B_b|/n) · |acc(B_b) − conf(B_b)|`. Standard equal-width ECE is known-biased (Kumar 2019) — **use equal-frequency bins**.

**Brier decomposition (Murphy 1973)**:
```
BS = Reliability − Resolution + Uncertainty
```
Reliability → 0 = calibrated. Resolution ≈ Uncertainty = useless model.

**CRPS** for continuous predictions (weather):
```
CRPS(F, y) = ∫ (F(x) − 1{x ≥ y})² dx
```
Strictly proper, reduces to Brier for binary.

**Spiegelhalter z-statistic** (tests Brier significance):
```
Z = Σ (o_i − p_i)(1 − 2p_i) / √(Σ p_i(1−p_i)(1−2p_i)²)
```
|Z| > 1.96 → miscalibrated.

**Smooth ECE** (Błasiok & Nakkiran 2023, ICLR 2024): kernel-smoothed consistent estimator, replaces binned ECE. <https://arxiv.org/abs/2309.12236>

Libraries: `netcal` (best), `properscoring`, `scoringrules` (JAX-accelerated), `uncertainty-toolbox`.

### 8.7 Online / streaming calibration

- **Online Platt**: SGD on logistic, learning rate 0.01. Trivial.
- **Online isotonic** (Kotłowski-Koolen 2016): maintain sorted structure, merge on violation, O(log n) per update.
- **Pragmatic choice**: refit on sliding window every K new outcomes.

**Kalshi strategy**: exponentially-weighted 90-day window + refit weekly or per 50 new observations. Keep both "all-time" and "recent" calibrators; use recent unless sample <100.

### 8.8 Drift detection

Monitor:
- **KS test** on recent predicted probs vs training. p < 0.01 = input drift.
- **PSI**: `Σ (p_new − p_old) · log(p_new/p_old)`. PSI > 0.25 = significant shift.
- **Rolling ECE** (last 30 days). Alert if > 2× all-time ECE.
- **CUSUM on Brier residuals**.

On drift: refit on recent window, widen IVAP intervals, reduce position sizes.

### 8.9 Decision tree for this bot

```
Per market-category, with N = calibration samples:

N < 50:      hierarchical pool across related markets + Venn-Abers (wide intervals)
N < 100:     Temperature scaling OR Venn-Abers
N < 300:     Beta calibration (default) + Venn-Abers for intervals
N < 1000:    Beta calibration; add Spline if resolution plateau
N ≥ 1000:    Isotonic OR Beta — pick via 5-fold CV log-loss
             Always keep Venn-Abers running in parallel for intervals

Weather contracts: apply EMOS/NGR FIRST (to raw ensemble), THEN Beta+IVAP on top.

Always: adaptive-ECE + Brier decomposition + Spiegelhalter-Z,
        refit weekly on 90-day exponentially weighted window,
        monitor PSI + rolling ECE for drift.
```

### 8.10 C++ implementation plan

1. **Beta calibration**: 50 lines with Eigen + LBFGS (`dlib` or `ceres-solver` for the optimizer).
2. **IVAP**: implement PAVA once (~40 lines), two incremental queries per prediction. Free interval output.
3. **EMOS**: linear regression + CRPS optimization via LBFGS.
4. **SQLite calibration DB** (already in project at `data/calibration.db`): store `(model_id, timestamp, predicted_p, outcome, market_category)`. Query windowed for retraining.
5. **Separate calibrators per market category** — weather, CPI, NFP, Fed all have different miscalibration shapes.
6. **Optimization scoring rule = log loss**; **monitoring = Brier + ECE**; **weather = CRPS**.

---

## 9. Algorithms: Position Sizing (Kelly & Extensions)

Current state-of-the-art for Kalshi-style binary contracts is **multi-bet Kelly with Venn-Abers lower bounds, cluster caps, and CVaR constraints**, solved as a convex program. The scalar-per-market approach is strictly dominated.

### 9.1 Kelly fundamentals

**Binary Kelly**: win probability `p`, net odds `b` (profit per unit staked), loss of stake on loss:
```
f* = (bp − q) / b     where q = 1 − p
```

For Kalshi (pay price `c` for YES paying $1): `b = (1−c)/c`, so:
```
f* = (p − c) / (1 − c)
```
**Edge-over-payoff-per-dollar form** — intuitive.

Assumptions violated in practice: known `p`, infinite divisibility, log utility, no transaction costs, independent repeated bets.

### 9.2 Fractional Kelly — the quadratic utility-loss argument

Thorp (2006), MacLean-Thorp-Ziemba (2010): growth rate `g(f)` is ≈ quadratic near `f*`:
```
g(f) ≈ g(f*) − 0.5 · σ² · (f − f*)²
```
Betting at `α · f*` retains `1 − (1−α)²` of the growth rate while reducing volatility by `(1−α)`. **Half-Kelly keeps 75% of growth for 50% of volatility.** Asymmetric trade-off that makes fractional Kelly robust to estimation error.

### 9.3 Robust Kelly (probability uncertainty)

Baker-McHale (2013): if `p̂` has `Var = σ_p²`, shrink:
```
f_robust = f* · max(0, 1 − σ_p² / (p(1−p)))
```

Rujeerapaiboon-Kuhn-Wiesemann 2018, "Robust Growth-Optimal Portfolios" ([Management Science](https://pubsonline.informs.org/doi/10.1287/mnsc.2016.2664)): Wasserstein-DRO formulation:
```
max_f  min_{P ∈ U}  E_P[log(1 + f · X)]
```
`U` = Wasserstein or moment-based ambiguity set. Closed-form under Gaussian ambiguity.

### 9.4 Bayesian Kelly

Treat `p` as posterior `π(p|D)`. Maximize `E_π[log(1 + f · X(p))]`. For `Beta(α, β)` posterior:

```python
from scipy.optimize import minimize_scalar
from scipy.stats import beta

def bayesian_kelly(alpha, beta_param, price, n_samples=10000):
    p_samples = beta.rvs(alpha, beta_param, size=n_samples)
    def neg_growth(f):
        return -np.mean(np.log(1 + f * (p_samples*(1-price)/price - (1-p_samples))))
    res = minimize_scalar(neg_growth, bounds=(0, 0.99), method='bounded')
    return res.x
```

Naturally produces fractional-Kelly-like shrinkage when posterior is wide.

Browne-Whitt 1996 is the original analysis; Chapman 2023 extends to Dirichlet priors (multi-outcome brackets).

### 9.5 Credal-set / interval Kelly (this bot's recommendation)

Feed **Venn-Abers lower bound** `p_L` into Kelly:
```
f = max(0, f*(p_L))
```
If even `p_L` has positive edge, bet that `p_L`'s Kelly. Conservative, honest, automatically handles calibration uncertainty.

**No-go rule**: skip the bet if `p_U − p_L > 2 · (p̂ − price)` — the uncertainty band exceeds the edge. This is a minimum edge-to-uncertainty ratio (Sharpe-like); 1.5-2.0 is standard.

### 9.6 Drawdown-constrained Kelly (Grossman-Zhou 1993)

```
max  E[log W_T]   s.t.  P(max drawdown > D) ≤ α
```
Closed-form under GBM:
```
f_DD = f* · (1 − D_current / D_max)
```
Full-Kelly at new high, zero at max allowed drawdown. [Grossman-Zhou 1993](https://onlinelibrary.wiley.com/doi/10.1111/j.1467-9965.1993.tb00044.x).

### 9.7 Kelly with fees

Kalshi taker fee at P=0.50 is 1.75¢ — 3.5% round-trip on a 50¢ contract. Adjust:
```
b_eff = (1 − c − fee) / c         (net payoff per dollar risked)
Net_EV = p · (1 − c − fee) − (1 − p) · c
```
**Hard rule**: drop any bet where `edge < 2 · fee`. Morton-Pliska 1995 creates a "no-trade region" — only trade when `f_current` deviates from `f*` by more than a threshold proportional to cost. **Over-rebalancing is the single biggest cause of real-world Kelly underperformance.**

### 9.8 Multi-bet Kelly (simultaneous)

For `n` simultaneous bets:
```
max_f  E[log(1 + fᵀ · X)]    s.t.  Σf ≤ 1,  f_i ∈ [0, cap_i]
```
**Concave program** — solve with CVXPY:

```python
import cvxpy as cp

def multi_kelly(scenarios, probs, caps, total_cap=0.80):
    # scenarios: (S, n) matrix of payoffs per dollar in each of S scenarios
    n = scenarios.shape[1]
    f = cp.Variable(n, nonneg=True)
    wealth = 1 + scenarios @ f
    obj = cp.Maximize(probs @ cp.log(wealth))
    cons = [f <= caps, cp.sum(f) <= total_cap]
    cp.Problem(obj, cons).solve()
    return f.value
```

Independent bets: Smoczynski-Tomkins 2010 give separable analytic solution. Correlated bets: enumerate joint scenarios — 5 bets = 2⁵ = 32 states, tractable. Estimate joint via Gaussian copula on estimated correlation.

For C++: `ECOS` or `SCS` solvers via `Eigen`, or just shell out to a Python sidecar.

### 9.9 Worked example — 5 simultaneous Kalshi bets

```
Bet    Price  Model p  Edge   Indep f*
CPI    0.55   0.62     0.07   0.156
NFP    0.40   0.48     0.08   0.133
Fed    0.70   0.75     0.05   0.167
NYC°F  0.30   0.36     0.06   0.086
BTC$   0.50   0.55     0.05   0.100
```
Naive sum of f*: 0.64 (impossible — exceeds bankroll).
With correlation (CPI-NFP-Fed cluster ρ = 0.5), joint optimizer shrinks each macro bet by ~30% → total ~0.45. Apply α = 0.25 fractional Kelly → 0.11 portfolio fraction deployed. At $100 bankroll: $11 total across 5 bets, weighted by the `f` vector.

### 9.10 Bounded-loss advantage

Max loss per Kalshi contract = price paid. No margin calls, no gap risk. Simplifies risk control: size directly by "max dollars at risk per market."

### 9.11 Sizing on bid/ask (not midpoint)

Always size on the price you actually pay (ask for long YES). Using midpoint overstates edge by half the spread — **systematic overbetting in wide books**. Common mistake.

### 9.12 Exploration vs exploitation

- **Thompson sampling** — sample `p ~ posterior`, size Kelly on the sample. Naturally explores high-uncertainty markets.
- **UCB** — `score = edge + √(2 log t / n_category)`. Directs probing to under-sampled categories.
- **ε-greedy probes** — with ε = 5%, place 1-contract micro-bets in a new category to gather calibration data before sizing up. Cost: ε · bankroll · fee = negligible.
- **Phase-aware Kelly fraction**: first 100 bets in a category at α = 0.1; graduate to 0.25 once Brier stabilizes; 0.5 only after 500+ bets and out-of-sample calibration evidence.

### 9.13 Recent literature (2022-2026)

- Lopez de Prado & Lewis 2023, "Detection of False Investment Strategies Using Unsupervised Learning" — deflated-Sharpe shrinkage for multi-model Kelly: <https://papers.ssrn.com/sol3/papers.cfm?abstract_id=3484020>
- Hsieh & Barmish 2024, "On the Robustness of the Kelly Criterion" — elliptical ambiguity, closed-form shrinkage.
- Busseti, Ryu, Boyd 2016 (updated 2022), "Risk-Constrained Kelly Gambling" — CVXPY formulation with VaR/CVaR: <https://arxiv.org/abs/1603.06183>
- Vovk, Petej, Nouretdinov 2023, "Venn-Abers Calibration" — probability intervals → credal Kelly.

---

## 10. Algorithms: Portfolio Construction & Risk

### 10.1 Why MVO fails for binary bets

Bernoulli payoffs are bimodal at 0 and 1 — variance doesn't summarize risk when skew is extreme. A 90%/10% bet looks "low-variance" under MVO but has catastrophic left-tail. Don't use mean-variance for Kalshi.

### 10.2 CVaR optimization (Rockafellar-Uryasev 2000)

Minimize expected loss in worst α% of scenarios. LP formulation handles binary payoffs directly:
```
min  CVaR_α(loss(f))   s.t.  E[return] ≥ target,  f ≥ 0
```
CVXPY-native. Standard choice: α = 5% or 1%.

### 10.3 Hierarchical risk parity (López de Prado 2016)

Cluster assets by correlation, allocate top-down. For Kalshi: cluster by category (weather events / macro / sports), allocate within clusters. Robust to ill-conditioned correlation matrices that plague 50+ bets with short histories. [HRP paper](https://jpm.pm-research.com/content/42/4/59).

### 10.4 Portfolio-level risk limits (hard gates)

Stack these before sizing:

| Limit | Value at $100 bankroll |
|---|---|
| Total exposure cap | 80% ($80) |
| Per-category cap | weather ≤ 50% ($50), macro ≤ 40% ($40), entertainment ≤ 15% ($15) |
| Cluster cap (correlation-aware) | {CPI, NFP, Fed, GDP} combined ≤ 35% ($35) |
| Single-bet cap | 25% ($25) regardless of Kelly |
| Daily loss kill switch | −10% of SoD bankroll |
| CVaR-95% | ≤ 15% of bankroll |
| CVaR-99% | ≤ 25% of bankroll |
| Reserve | 20% cash always |

Cluster-cap enforcement is already in the codebase (`src/risk/cluster_limiter.cpp`).

### 10.5 Risk of ruin

Thorp: full-Kelly `P(bankroll halves before doubling) = 1/2`. Half-Kelly drops this to ~0.06.

Fractional Kelly ruin approximation:
```
P(reach aW before W/b) ≈ (1 − b^(−2/α)) / (1 − (ab)^(−2/α))
```
where α is the Kelly fraction.

**Chernoff bound** for small samples: after `n` bets with edge `ε` and variance `σ²`,
```
P(loss > k) ≤ exp(−nε² / 2σ²)
```
Useful for sizing exploration bets.

### 10.6 Dynamic bankroll scaling

- **Proportional** (re-bet full bankroll) — fastest asymptotic growth; worst drawdown.
- **Fixed-dollar** — linear growth, tiny drawdown.
- **Ratchet** — at `k · W₀` crossing, raise floor to `(k−1) · W₀`. Vince 1992 "Leverage Space Trading Model".
- **Kelly cap** (current code): `effective_bankroll = min(current, starting × 1.5)`. For $100 start, cap sizing-bankroll at $150 until strong out-of-sample calibration. Already in `src/strategy/kalshi/adaptive_sizer.cpp`.

### 10.7 Settlement-lag capital

Capital locked in unsettled winners is unavailable. If 80% of bankroll is in positions awaiting settlement 12h out, effective free capital is 20%. Model as time-varying cap.

---

## 11. Algorithms: Forecast Combination

### 11.1 Opinion pools

- **Linear pool**: `P̄ = Σ w_i P_i`. Simple, proper scoring rule. **Over-disperses**.
- **Logarithmic pool** (geometric of log-odds): `P̄ ∝ Π P_i^{w_i}`. KL-minimizing. **Under-disperses when forecasts agree**.

Satopää et al. 2014 "Combining Multiple Probability Predictions" — geometric pooling generally preferred for independent experts; when forecasts share info, shrink toward marginal. <https://arxiv.org/abs/1405.0798>

### 11.2 Bayesian Model Averaging

Weight by posterior model probability: `P(M | data) ∝ P(data | M) · P(M)`. BIC approximation:
```
w_i ∝ exp(−BIC_i / 2)
```
Needs reasonable prior or collapses to one model.

### 11.3 Stacking (recommended for production)

Breiman 1996. Train base models on CV folds, fit meta-model (logistic regression with non-negative weights, or isotonic) on out-of-fold predictions. **This wins Kaggle probability contests.**

`sklearn.ensemble.StackingClassifier`. ~5-10% RMSE improvement over inverse-MSE weighting in published macro nowcasting benchmarks.

### 11.4 Extremization (Baron et al. 2014)

When independent forecasters agree, extremize toward 0 or 1:
```
P' = P^a / (P^a + (1−P)^a),   a ≈ 2-3 empirically
```
Good Judgment Project found this materially boosts Brier scores.

### 11.5 Dynamic weighting

- **Bates-Granger 1969**: inverse-variance `w_i ∝ 1/σ_i²`. Equivalent to inverse-MSE when forecasts are unbiased.
- **Exponentially-weighted error variance** (`λ = 0.95`):
  ```
  σ²_t = λ σ²_{t−1} + (1 − λ) e²_t
  ```
- **Granger-Ramanathan 1984**: unrestricted OLS on forecast matrix, often better than constrained `Σw = 1` if forecasts are unbiased.
- **Regret-minimizing** (Hedge / Multiplicative Weights):
  ```
  w_{t+1, i} ∝ w_{t, i} · exp(−η · loss_{t, i})
  ```
  For non-stationary or adversarial settings.

**For this bot**: start with Bates-Granger after 100-event burn-in; switch to stacking after 500 events.

### 11.6 Bias corrections

- **Favorite-longshot bias** (Ottaviani-Sørensen 2008): bettors overpay longshots. Isotonic remap of historical Kalshi-close vs realized directly calibrates. <https://academic.oup.com/restud/article-abstract/75/2/485/1583373>
- **Overround removal**: for markets where `ΣP > 1`, normalize: `P_i' = P_i / Σ P_j`. For Kalshi YES/NO, `P(yes) + P(no) ≈ 1 − 2·fee`.
- **Smart-money premium** (Rothschild 2009 on Intrade): large-size orders 5-10% better-calibrated than small; weight toward size-weighted midpoint.

---

## 12. Algorithms: Backtesting Discipline

### 12.1 Vintage data — ALFRED is mandatory

Using current (revised) macro numbers to backtest a strategy is forward-looking bias. ALFRED gives "what was known as of date X". Always use vintages for macro features.

```python
import fredapi
f = fredapi.Fred(api_key=...)
nfp_vintage = f.get_series_as_of_date('PAYEMS', vintage_date='2024-03-08')
```

### 12.2 Walk-forward

- **Expanding window** for structural/slow-drift processes (macro).
- **Rolling 252-day** for fast-drift regimes (volatility).

### 12.3 Purged + embargoed CV (López de Prado 2018)

"Advances in Financial Machine Learning" — for overlapping-window labels:
- **Purge** training samples whose label window overlaps test window.
- **Embargo** N days after test to prevent serial-correlation leakage.

Critical for any event window that spans multiple days (e.g., CPI "bracket" contract opens 2 weeks before resolution).

### 12.4 Metrics

- **Brier score**: `(p − y)²`. Default for binary.
- **Log loss**: `−[y log p + (1−y) log(1−p)]`. Penalizes tail errors more; better for optimization.
- **CRPS**: continuous predictions.
- **Reliability + Resolution**: decompose Brier to diagnose model failures (calibrated but useless vs uncalibrated).
- **Sharpe / Sortino on simulated P&L**: ultimate metric.

### 12.5 Avoiding p-hacking

- **Deflated Sharpe ratio** (Bailey & López de Prado 2014): correct for multiple-testing bias when selecting among many strategies.
- **Walk-forward with strict time-ordering** — no peeking.
- **Commit to preregistered test set** before running.
- **Purged k-fold CV** for hyperparameter selection.
- **Paper-trade on demo for 48-72 hours** before live (already in the project workflow).

---

## 13. Recommended Implementation Roadmap

This maps the research above onto the existing codebase. File paths relative to `C:\Users\alexc\Documents\Projects\Trader\`.

### Phase 0 — Platform hygiene (April 2026 — DO FIRST)

1. **Fixed-point migration.** Read `src/exchange/kalshi/` — if any prices are `int64_t` cents, migrate to `FixedPoint` or `std::string`. **Deadline: April 2, 2026.** After that, integer-cent fields are gone.
2. **Wire `user_orders` WS channel** — replaces order polling, gives ordered fills with `fee_cost` as dollar string.
3. **Wire `GET /account/limits`** on startup to log RPS tier.
4. **Pull `GET /series/fee_changes`** on startup — don't assume global 0.07/0.0175 formula.
5. **Add `GET /trade-api/v2/markets/orderbooks` batch** for cold-start — 100 tickers in one request.
6. **Subscribe to changelog RSS** (`https://docs.kalshi.com/changelog/rss.xml`) on a cron.

### Phase 1 — Data layer upgrades

7. **Weather sources**: add ECMWF AIFS (`pip install ecmwf-opendata` sidecar), NOAA NBM (via AWS S3 `s3://noaa-nbm-grib2-pds/`), GraphCast via Open-Meteo. Replace or augment current feed.
8. **Historical training data**: download GEFS Reforecast v12 (`s3://noaa-gefs-retrospective/`, 2000-2019) + ERA5 (`gs://gcp-public-data-arco-era5/`) + GHCN-Daily for six Kalshi stations.
9. **CPI inputs**: Cleveland Fed Nowcast CSV scraper, ZORI CSV (12-16 month shelter lead), EIA weekly gasoline + daily WTI, Manheim monthly.
10. **NFP inputs**: DOL jobless claims (weekly), ADP (monthly), Indeed Hiring Lab daily via FRED (`IHLIDXUSTPHYTOTL`).
11. **ALFRED vintages**: add `fredapi.get_series_as_of_date()` usage to all macro backtests.
12. **Reference-market priors**: CME ZQ fed funds for Fed contracts (derive FedWatch probabilities).

### Phase 2 — Algorithm upgrades

13. **Weather post-processing**: implement Quantile Regression Forest in Python sidecar (`quantile-forest`), trained on GEFS Reforecast + ERA5 + GHCN. Export per-station percentile forecasts as JSON consumed by C++.
14. **EMOS fallback** for new stations with <500 training days.
15. **Beta calibration** layer (`src/strategy/kalshi/probability_calibrator.cpp`): ~50 lines with Eigen + LBFGS.
16. **Inductive Venn-Abers** in parallel: PAVA implementation + two incremental queries. Feed lower bound into sizer.
17. **Multi-bet Kelly** in `src/strategy/kalshi/adaptive_sizer.cpp`: CVXPY sidecar or ECOS/SCS C++ solver. Enforce cluster caps from `src/risk/cluster_limiter.cpp`.
18. **Stacking meta-model** for macro: Cleveland Fed + GDPNow + own LightGBM → logistic meta-regression.
19. **Drift monitoring**: PSI + rolling ECE + Spiegelhalter-Z as cron check; alert on threshold breach.

### Phase 3 — Monitoring & observability

20. **Kalshi Forecast Percentiles** benchmark: pull `GET /forecast_percentiles_history`, log model-vs-crowd divergence.
21. **Per-source Brier tracking**: extend `data/calibration.db` schema to include `source_id` so you can Brier-score Cleveland Fed vs your own model vs GDPNow, etc.
22. **CF6 settlement reconciliation**: after each weather contract settles, pull CF6 from NWS (`https://forecast.weather.gov/product.php?...&product=CF6`), reconcile Kalshi settlement vs our logged prediction.

### Phase 4 — Deferred (post-demo graduation)

23. Sports markets — **skip**, per `KALSHI_RESEARCH.md` §11.11 rating 3/10.
24. Hurricane category activation (Jun-Nov seasonal) — reuse QRF stack with HAFS + NHC probabilistic wind-speed inputs.
25. FIX 4.4 — only after Prime tier (7.5% monthly volume). Not for $100 capital.

### Phase 5 — Phase 2 preparation

`RESEARCH.md` covers Avellaneda-Stoikov for dYdX v4 perp market making. Phase 2 shares ~88% of core infra with Phase 1 and is out of scope for this document.

---

## Appendix A: Formula Cheat Sheet

### Kalshi fees
```
taker_fee_cents = ceil(100 × 0.07   × C × P × (1 − P))
maker_fee_cents = ceil(100 × 0.0175 × C × P × (1 − P))
```

### Kelly (binary Kalshi)
```
b     = (1 − c) / c
f*    = (p − c) / (1 − c)             ← edge-over-payoff form
f_α   = α · f*                         ← fractional, α ∈ [0, 1]
```

### Robust (interval) Kelly
```
f_robust = max(0, f*(p_L))             ← p_L = Venn-Abers lower bound
no-go if p_U − p_L > 2 · (p̂ − c)
```

### Drawdown-constrained Kelly
```
f_DD = f* · (1 − D_current / D_max)
```

### Multi-bet Kelly
```
max_f  E[log(1 + fᵀ · X)]
s.t.   Σ f_i ≤ total_cap,   f_i ∈ [0, cap_i]
```

### Brier / CRPS / ECE
```
BS   = (1/N) Σ (p_i − y_i)²
BS   = Reliability − Resolution + Uncertainty
CRPS = ∫ (F(x) − 1{x ≥ y})² dx
ECE  = Σ_b (|B_b|/n) · |acc(B_b) − conf(B_b)|    (equal-frequency bins)
```

### Spiegelhalter Z
```
Z = Σ (o_i − p_i)(1 − 2p_i) / √(Σ p_i(1 − p_i)(1 − 2p_i)²)
```

### Beta calibration
```
μ(s; a, b, c) = 1 / (1 + exp(−(a log s − b log(1 − s) + c)))
```

### EMOS / NGR
```
Y | ens ~ Normal(μ, σ²)
μ   = a + b · mean(ens)
σ²  = c + d · var(ens)
fit by CRPS minimization
```

### Breeden-Litzenberger
```
f(K) = e^(rT) · ∂²C/∂K²
F(K) = 1 + e^(rT) · ∂C/∂K    ← digital put value, = Kalshi yes price
```

### SVI (Gatheral)
```
w(k) = a + b · {ρ(k − m) + √((k − m)² + σ²)}
```

### CME FedWatch
```
ZQ_settle = (m/N) · EFFR_pre + ((N − m)/N) · EFFR_post
P(hike to X) = (EFFR_post − rate_no_action) / (rate_action − rate_no_action)
```

### Orderbook invariant
```
ask_YES(p) = 1.00 − p_NO_bid
qty_ask_YES = qty_NO_bid
```

### Arbitrage condition (cross-platform)
```
profit = $1.00 − (price_A_yes + price_B_no) − fees_A − fees_B − slippage
```

### Bates-Granger / inverse-MSE
```
w_i ∝ 1 / σ_i²     (error variance over rolling window)
```

### Extremization (Baron)
```
P' = P^a / (P^a + (1 − P)^a),   a ∈ [2, 3]
```

### PSI (drift detection)
```
PSI = Σ_bins (p_new − p_old) · log(p_new / p_old)
PSI > 0.25 → significant shift
```

---

## Appendix B: Complete URL Index

### Kalshi platform
- Main docs: <https://docs.kalshi.com>
- OpenAPI spec: <https://docs.kalshi.com/openapi.yaml>
- Changelog: <https://docs.kalshi.com/changelog>
- Changelog RSS: <https://docs.kalshi.com/changelog/rss.xml>
- API keys / auth: <https://docs.kalshi.com/getting_started/api_keys>
- Rate limits: <https://docs.kalshi.com/getting_started/rate_limits>
- Demo env: <https://docs.kalshi.com/getting_started/demo_env>
- FIX overview: <https://docs.kalshi.com/fix>
- Orderbook WS: <https://docs.kalshi.com/websockets/orderbook-updates>
- Liquidity Incentive Program: <https://help.kalshi.com/en/articles/13823851-liquidity-incentive-program>
- Market Maker Program: <https://help.kalshi.com/en/articles/13823819-how-to-become-a-market-maker-on-kalshi>
- Fee schedule: <https://kalshi.com/docs/kalshi-fee-schedule.pdf>
- Rulebook: <https://kalshi.com/rulebook>
- News / product announcements: <https://news.kalshi.com>

### Weather — AI models
- GraphCast: <https://github.com/google-deepmind/graphcast>
- GraphCast paper (Science 2023): <https://www.science.org/doi/10.1126/science.adi2336>
- GenCast paper (Nature 2024): <https://www.nature.com/articles/s41586-024-08252-9>
- Pangu-Weather paper (Nature 2023): <https://www.nature.com/articles/s41586-023-06185-3>
- Pangu-Weather repo: <https://github.com/198808xc/Pangu-Weather>
- ECMWF AIFS blog: <https://www.ecmwf.int/en/about/media-centre/aifs-blog>
- ECMWF Open Data: <https://www.ecmwf.int/en/forecasts/datasets/open-data>
- NVIDIA FourCastNet (Modulus): <https://github.com/NVIDIA/modulus>
- WeatherBench2: <https://weatherbench2.readthedocs.io>
- Open-Meteo GraphCast: <https://open-meteo.com/en/docs/gfs-graphcast-api>

### Weather — NOAA operational
- NBM: <https://vlab.noaa.gov/web/mdl/nbm>
- NBM on AWS: <https://registry.opendata.aws/noaa-nbm/>
- NBM NOMADS: <https://nomads.ncep.noaa.gov/pub/data/nccf/com/blend/prod/>
- NDFD: <https://digital.weather.gov/>
- NWS API: <https://api.weather.gov>
- HREF: <https://www.spc.noaa.gov/exper/href/>
- HRRR on AWS: <https://registry.opendata.aws/noaa-hrrr-pds/>
- GEFS Reforecast v12: <https://registry.opendata.aws/noaa-gefs-reforecast/>
- ERA5 (Copernicus CDS): <https://cds.climate.copernicus.eu/>
- ARCO-ERA5 (GCS): <https://cloud.google.com/storage/docs/public-datasets/era5>
- GHCN-Daily (bulk): <https://www.ncei.noaa.gov/pub/data/ghcn/daily/>
- NCEI web service: <https://www.ncei.noaa.gov/cdo-web/webservices/v2>
- Iowa Environmental Mesonet 1-min ASOS: <https://mesonet.agron.iastate.edu/request/asos/1min.phtml>

### Hurricanes
- HAFS: <https://hafs.noaa.gov/>
- NHC main: <https://www.nhc.noaa.gov/>
- NHC ATOM/RSS: <https://www.nhc.noaa.gov/index-at.xml>
- NHC ATCF FTP: <ftp://ftp.nhc.noaa.gov/atcf/>
- NHC probabilistic wind-speed: <https://www.nhc.noaa.gov/aboutnhcprobs2.shtml>
- P-Surge storm surge: <https://www.nhc.noaa.gov/surge/psurge.php>
- HURDAT2: <https://www.nhc.noaa.gov/data/hurdat/>
- Colorado State Tropical Forecast: <https://tropical.colostate.edu/>
- NOAA CPC ENSO: <https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso_advisory/>
- TSR (UCL): <http://www.tropicalstormrisk.com/>
- TIGGE archive: <https://confluence.ecmwf.int/display/TIGGE>

### Macroeconomics — inflation
- Cleveland Fed Inflation Nowcast: <https://www.clevelandfed.org/indicators-and-data/inflation-nowcasting>
- Cleveland Fed nowcast CSV: <https://www.clevelandfed.org/-/media/files/webcharts/inflationnowcasting/inflation-nowcasting.csv>
- Truflation: <https://truflation.com>
- Truflation API docs: <https://docs.truflation.com>
- Adobe Digital Price Index: <https://business.adobe.com/resources/digital-price-index.html>
- Zillow research data: <https://www.zillow.com/research/data/>
- Zillow CSV: <https://files.zillowstatic.com/research/public_csvs/zori/>
- Manheim UVVI: <https://publish.manheim.com/en/services/consulting/used-vehicle-value-index.html>
- EIA Open Data: <https://www.eia.gov/opendata/>
- USDA Food Price Outlook: <https://www.ers.usda.gov/data-products/food-price-outlook/>
- ISM: <https://www.ismworld.org/supply-management-news-and-reports/reports/ism-report-on-business/>
- NY Fed UIG: <https://www.newyorkfed.org/research/policy/underlying-inflation-gauge>
- Atlanta Fed Sticky Price: <https://www.atlantafed.org/research/inflationproject/stickyprice>

### Macroeconomics — employment
- BLS Employment Situation: <https://www.bls.gov/news.release/empsit.htm>
- DOL jobless claims: <https://www.dol.gov/ui/data.pdf>
- ADP Employment Report: <https://adpemploymentreport.com>
- Indeed Hiring Lab: <https://www.hiringlab.org/indeed-data/>
- Indeed GitHub data: <https://github.com/hiring-lab/data>
- LinkUp: <https://www.linkup.com/data/>
- Homebase: <https://joinhomebase.com/data/>
- Revelio Labs: <https://www.reveliolabs.com>
- JOLTS: <https://www.bls.gov/jlt/>

### Macroeconomics — GDP
- Atlanta Fed GDPNow: <https://www.atlantafed.org/cqer/research/gdpnow>
- GDPNow history XLSX: <https://www.atlantafed.org/-/media/documents/cqer/researchcq/gdpnow/gdpnow-forecast-evolution.xlsx>
- NY Fed WEI: <https://www.newyorkfed.org/research/policy/weekly-economic-index>
- NY Fed MCT: <https://www.newyorkfed.org/research/policy/multivariate-core-trend>
- Dallas Fed WEI: <https://www.dallasfed.org/research/wei>
- Philly Fed ADS: <https://www.philadelphiafed.org/research-and-data/real-time-center/business-conditions-index>

### Macroeconomics — Fed / rates
- CME FedWatch: <https://www.cmegroup.com/markets/interest-rates/cme-fedwatch-tool.html>
- NY Fed Primary Dealer Survey: <https://www.newyorkfed.org/markets/primarydealer_survey_questions.html>
- FOMC calendar: <https://www.federalreserve.gov/monetarypolicy/fomccalendars.htm>

### Vintage / curated macro
- ALFRED: <https://alfred.stlouisfed.org>
- FRED API: <https://fred.stlouisfed.org/docs/api/fred/>
- FRED-MD / FRED-QD: <https://research.stlouisfed.org/econ/mccracken/fred-databases/>

### Options / reference markets
- CBOE quotes: <https://www.cboe.com/delayed_quotes/>
- Deribit public API: <https://www.deribit.com/api/v2/public/get_book_summary_by_currency>
- Polygon.io: <https://polygon.io>

### Polling
- FiveThirtyEight data: <https://github.com/fivethirtyeight/data>
- *Economist* POTUS model: <https://github.com/TheEconomist/us-potus-model-2020>

### Papers / seminal references
- Kull et al. 2017 Beta calibration: <https://arxiv.org/abs/1707.01889>
- Vovk-Petej 2012 Venn-Abers: <https://arxiv.org/abs/1211.0025>
- Angelopoulos-Bates 2023 Conformal intro: <https://arxiv.org/abs/2107.07511>
- Conformal Risk Control 2024: <https://arxiv.org/abs/2208.02814>
- Smooth ECE (Błasiok-Nakkiran 2023): <https://arxiv.org/abs/2309.12236>
- Busseti-Ryu-Boyd Risk-Constrained Kelly: <https://arxiv.org/abs/1603.06183>
- Rujeerapaiboon et al. Robust Growth-Optimal: <https://pubsonline.informs.org/doi/10.1287/mnsc.2016.2664>
- Figlewski 2008 Implied RND: <https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1256783>
- Satopää et al. 2014 combining predictions: <https://arxiv.org/abs/1405.0798>
- Deflated Sharpe (Bailey-López de Prado): <https://papers.ssrn.com/sol3/papers.cfm?abstract_id=3484020>
- HRP (López de Prado 2016): <https://jpm.pm-research.com/content/42/4/59>
- Grossman-Zhou drawdown Kelly 1993: <https://onlinelibrary.wiley.com/doi/10.1111/j.1467-9965.1993.tb00044.x>
- Ottaviani-Sørensen 2008 favorite-longshot: <https://academic.oup.com/restud/article-abstract/75/2/485/1583373>
- Gatheral SVI slides: <https://mfe.baruch.cuny.edu/wp-content/uploads/2013/01/OsakaSVI2012.pdf>
- Hagan SABR: <https://www.researchgate.net/publication/235622441_Managing_Smile_Risk>
- Carr-Madan static replication: <https://www.researchgate.net/publication/2821098_Towards_a_Theory_of_Volatility_Trading>
- Manokhin Awesome Conformal Prediction: <https://github.com/valeman/awesome-conformal-prediction>

### Kalshi regulatory
- KalshiEX v. Flaherty (Paul Weiss writeup): <https://www.paulweiss.com/insights/client-memos/a-divided-third-circuit-holds-that-the-cftc-has-exclusive-jurisdiction-over-sports-related-event-contracts>
- Holland & Knight Feb 2026 analysis: <https://www.hklaw.com/en/insights/publications/2026/02/prediction-markets-at-a-crossroads-the-continued-jurisdictional-battle>
- Nevada ruling (Nevada Independent): <https://thenevadaindependent.com/article/federal-judge-rules-that-kalshi-must-stop-offering-prediction-contracts-in-nevada>
- CFTC LIP filing Feb 2026: <https://www.cftc.gov/sites/default/files/filings/orgrules/26/02/rules02112639183.pdf>

### Third-party Kalshi clients (study for C++ patterns)
- ammario/kalshi (Go): <https://github.com/ammario/kalshi>
- rmadev01/kalshi-rs (Rust HFT): <https://github.com/rmadev01/kalshi-rs>
- pbeets/kalshi-trade-rs (Rust orderbook): <https://github.com/pbeets/kalshi-trade-rs>
- ArshKA/pykalshi (Python, async, Pydantic, pandas integration): <https://github.com/ArshKA/pykalshi> — primarily useful for **understanding API semantics and local-orderbook-from-WS-delta patterns**; Pythonic idioms (async/await, context managers, Pydantic) don't port cleanly to C++, but the delta-application logic is worth reading. Also a clean scaffold if a Python sidecar is spun up for zarr / AI-weather-model integration.
- Community post on WS delta application: <https://amiable.dev/blog/arbiter-bot/2026-01-21-kalshi-websocket-deltas/>

---

*Document version: 1.0 — April 2026. Companion to `KALSHI_RESEARCH.md` (strategy), `RESEARCH.md` (Phase 2 crypto), `VENUE_ANALYSIS.md` (venue comparison). Maintenance: poll Kalshi changelog RSS weekly; re-check AI weather model state quarterly (rapid progress); re-check calibration-algorithm literature yearly.*
