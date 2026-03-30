# 03 — Public Data Feeds

**Status**: Not Started

---

## Scope

Build data feed clients for free public data sources that power the probability models. Weather data is the primary feed (daily resolution, highest edge). Economic data is secondary (monthly resolution).

**This spec covers**:
- Open-Meteo ensemble API client (GFS 31-member + ECMWF 51-member) — **primary data source**
- NWS API client (official forecasts, station observations) — supplementary
- BLS API client (CPI, NFP) — secondary
- FRED API client (Fed funds rate, GDP, economic indicators) — secondary
- Common data normalization layer
- GEFS reforecast archive downloader for backtesting

**Out of scope**:
- Probability calculations → `04-probability-engine.md`
- Trading decisions → `05-trading-strategy.md`

---

## What's Done

| Item | Status |
|------|--------|
| Open-Meteo + NWS weather feeds | Not started |
| BLS + FRED economic feeds | Not started |
| Feed manager + scheduling | Not started |

---

## Technical Details

### Common Signal Format

All data feeds produce normalized signals:

```cpp
struct DataSignal {
    std::string source;        // "open_meteo", "nws", "bls", "fred"
    std::string series_id;     // e.g., "ensemble_KNYC", "CUUR0000SA0"
    std::string label;         // "GFS Ensemble NYC", "CPI YoY"
    double value;              // The data point
    double previous_value;     // Prior period (for econ data)
    Timestamp observation_date;
    Timestamp fetch_time;
    nlohmann::json metadata;   // Source-specific extras
};

// Weather-specific: ensemble forecast data
struct EnsembleForecast {
    std::string station;       // "KNYC", "KORD", etc.
    std::string model;         // "gfs", "ecmwf"
    int num_members;           // 31 for GFS, 51 for ECMWF
    Timestamp forecast_time;   // When the forecast was made
    Timestamp target_date;     // Date being forecast
    std::vector<double> member_highs;  // High temp from each ensemble member
    std::vector<double> member_lows;   // Low temp from each ensemble member
};
```

### Open-Meteo Ensemble API (Primary Weather Source)

**Why Open-Meteo over raw NOAA**: Clean JSON API, no API key, both GFS and ECMWF ensembles, no GRIB2 parsing needed.

- **Ensemble endpoint**: `https://ensemble-api.open-meteo.com/v1/ensemble`
- **Auth**: None required (free, no key)
- **Rate limits**: Fair use — recommended max 10,000 requests/day
- **Key parameters**:
  ```
  ?latitude={lat}&longitude={lon}
  &models=gfs_seamless,ecmwf_ifs025
  &hourly=temperature_2m
  &forecast_days=7
  &temperature_unit=fahrenheit
  ```
- **Response**: JSON with `hourly.temperature_2m_member01` through `temperature_2m_member31` (GFS) or `member51` (ECMWF)
- **Station coordinates** (for Kalshi settlement stations):
  | Station | Lat | Lon |
  |---------|-----|-----|
  | KNYC (NYC) | 40.7128 | -74.0060 |
  | KORD (Chicago) | 41.9742 | -87.9073 |
  | KMIA (Miami) | 25.7959 | -80.2870 |
  | KLAX (LA) | 33.9425 | -118.4081 |
  | KDEN (Denver) | 39.8561 | -104.6737 |
  | KAUS (Austin) | 30.1975 | -97.6664 |

- **What to extract**: For each station and forecast day:
  - All member high temperatures → count above/below Kalshi thresholds
  - Ensemble spread (disagreement = uncertainty = wider market spreads = more opportunity)

### NWS API (Supplementary Weather)

- **Base URL**: `https://api.weather.gov/`
- **Auth**: None required (include User-Agent header per NWS requirements)
- **Key endpoints**:
  - `GET /points/{lat},{lon}` → Resolve forecast office + grid
  - `GET /gridpoints/{office}/{x},{y}/forecast` → 7-day forecast (high/low)
  - `GET /stations/{stationId}/observations/latest` → Latest observation
- **Use**: Cross-reference Open-Meteo ensemble with NWS point forecast. NWS point forecast represents the "official" consensus. If ensemble disagrees with NWS, that's additional signal.

### BLS API (Economic Data — Secondary)

- **Base URL**: `https://api.bls.gov/publicAPI/v2/timeseries/data/`
- **Auth**: Registration key (free, 500 req/day)
- **Key series**:
  - `CUUR0000SA0` — CPI-U (All Items, US City Average)
  - `CES0000000001` — Total Non-Farm Payrolls
  - `LNS14000000` — Unemployment Rate
- **Update frequency**: Monthly
- **What to fetch**: Latest value, prior period, YoY/MoM change

### FRED API (Federal Reserve Economic Data — Secondary)

- **Base URL**: `https://api.stlouisfed.org/fred/`
- **Auth**: API key (free registration)
- **Key series**:
  - `FEDFUNDS` — Effective Federal Funds Rate
  - `GDPNOW` — Atlanta Fed GDPNow (real-time GDP tracking)
  - `T10Y2Y` — 10-Year minus 2-Year Treasury (yield curve)
  - `GASREGW` — Weekly US regular gasoline price (CPI energy input)
- **Cleveland Fed Inflation Nowcast**: Available via Cleveland Fed website, daily at 10 AM ET. More accurate than professional forecaster surveys for CPI.

### GEFS Reforecast Archive (Backtesting)

- **Source**: NOAA GEFS Reforecast Dataset (20+ years of retrospective ensemble forecasts)
- **Purpose**: Validate weather model BEFORE risking real money. Simulate exact model against 20 years of historical data.
- **Access**: NOAA NCEI archive (bulk download)
- **Alternative**: Iowa Environmental Mesonet for historical station observations + Open-Meteo historical API for past ensemble data

### Fetch Schedule

| Source | Frequency | When | Priority |
|--------|-----------|------|----------|
| Open-Meteo ensemble | Every 6 hours | Aligned with model runs (00Z/06Z/12Z/18Z) | Primary |
| NWS forecast | Every 6 hours | After ensemble refresh | Supplementary |
| Kalshi markets | Every 5 minutes | Continuous | Primary |
| BLS CPI | Monthly | Day of release (8:30 AM ET) | Secondary |
| BLS NFP | Monthly | First Friday (8:30 AM ET) | Secondary |
| FRED rates | Daily | After market close | Secondary |
| Cleveland Fed Nowcast | Daily | After 10 AM ET update | Secondary |

### Caching

All fetched data cached locally (JSON files in `data/cache/`) with TTL:
- Open-Meteo ensemble: 6 hours (matches model run cycle)
- NWS forecast: 6 hours
- BLS: Until next release date
- FRED: 24 hours

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 7 | Open-Meteo ensemble client + NWS client | Fetches GFS 31-member + ECMWF 51-member ensemble data for all 6 Kalshi station coordinates via Open-Meteo API. Parses into EnsembleForecast structs (member highs per forecast day). Fetches NWS point forecasts for same stations. Caches with 6h TTL. Unit tests verify parsing against saved sample API responses. Integration test fetches real ensemble data and verifies member count. |
| 8 | BLS + FRED API clients with caching | Fetches CPI (CUUR0000SA0), NFP (CES0000000001), Fed funds rate (FEDFUNDS), GDPNow, gasoline prices from BLS and FRED. Parses into DataSignal structs. Caches with appropriate TTLs. Unit tests verify parsing against sample responses. Integration test fetches real data from both APIs. |
| 9 | Data feed manager + scheduling + reforecast downloader | Coordinates all feeds on configurable schedule (ensemble every 6h, econ on release days). Publishes DataSignal/EnsembleForecast events to SPSC bus. Handles fetch failures gracefully (use cached data, log warning). Includes GEFS reforecast downloader for backtesting historical ensemble data. Unit test verifies scheduling and event publishing. |
