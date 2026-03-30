# 04 — Probability Engine

**Status**: Not Started

---

## Scope

Build the quantitative models that convert public data into event probability estimates. Weather ensemble model is the primary engine (highest edge, daily resolution). Economic models are secondary.

**This spec covers**:
- Bayesian probability framework (prior + evidence → posterior)
- Weather ensemble model: calibrated GEFS/ECMWF ensemble → probability (PRIMARY)
- Economic models: CPI bottom-up, NFP regression, Fed rate via FedWatch (SECONDARY)
- Cross-market consistency checks
- Backtesting harness using GEFS reforecast archive

**Out of scope**:
- Data fetching → `03-public-data-feeds.md`
- Trade execution decisions → `05-trading-strategy.md`
- Model calibration tracking → `06-risk-and-calibration.md`

---

## What's Done

| Item | Status |
|------|--------|
| Bayesian framework | Not started |
| Weather ensemble model | Not started |
| Economic models + consistency | Not started |

---

## Technical Details

### Probability Model Interface

```cpp
class IProbabilityModel {
public:
    virtual ~IProbabilityModel() = default;

    // Given current data, what's the probability of the event?
    virtual Probability estimate(const MarketEvent& event,
                                 const std::vector<DataSignal>& signals) = 0;

    // Confidence in the estimate (0-1). Low confidence = don't trade.
    virtual double confidence() const = 0;

    // Human-readable explanation for logging
    virtual std::string rationale() const = 0;
};
```

### Weather Ensemble Model (Primary — 9/10 category rating)

**This is the most important model in the system.** It produces the highest-edge, highest-frequency signals.

**Approach**: Calibrated ensemble counting with bias correction.

**Step 1: Raw ensemble probability**
```
raw_prob(temp > threshold) = count(members where high > threshold) / total_members
```
Example: 28 of 31 GFS members forecast NYC high > 75°F → raw_prob = 0.903

**Step 2: Bias correction**
GEFS has documented biases (warm in winter, cold in summer for specific stations). Apply rolling correction:
```
bias = mean(forecast_high - actual_high) over last 30-60 days for this station
corrected_member_high = member_high - bias
```
Recompute raw_prob with corrected member values.

**Step 3: Ensemble calibration (EMOS — Ensemble Model Output Statistics)**
Raw ensemble probabilities are often overconfident (too many members agree). Apply Non-homogeneous Gaussian Regression:
```
calibrated_forecast ~ Normal(a + b * ensemble_mean, c + d * ensemble_spread)
```
Where a, b, c, d are calibrated from GEFS reforecast archive (20+ years of data).

Simpler alternative (start here): Quantile mapping — map raw ensemble probabilities to observed frequencies from historical data.

**Step 4: Forecast horizon scaling**
Ensemble spread increases with forecast horizon. Calibrate σ per horizon:
| Forecast Horizon | Typical Temp Error (°F) | Notes |
|-----------------|------------------------|-------|
| 1 day | ±2°F | High confidence |
| 2 days | ±3°F | Good confidence |
| 3 days | ±4°F | Moderate confidence |
| 5 days | ±5°F | Lower confidence |
| 7 days | ±6°F | Trade only with large edge |

**Step 5: Multi-model blending**
When both GFS (31 member) and ECMWF (51 member) are available:
```
blended_prob = w_gfs * prob_gfs + w_ecmwf * prob_ecmwf
```
ECMWF is generally more accurate (weight 0.6) but GFS updates more frequently. Calibrate weights from reforecast data.

**Known mispricing patterns:**
- Markets anchor to climatological averages rather than updating to latest model runs
- Overnight/weekend staleness: NOAA runs update at ~4am and ~4pm ET; markets often lag
- Shoulder seasons (spring/fall) have widest ensemble spread = most opportunity
- Local phenomena (urban heat island, lake-effect, inversions) cause systematic bias that station-specific correction captures
- One documented weather bot using GFS ensemble achieved $1.8K profit

### Economic Models (Secondary)

#### CPI Model (7/10 category rating)

**Approach**: Bottom-up CPI estimation + Cleveland Fed Nowcast.

CPI components with BLS weights:
- **Shelter** (33%): Largest driver. Use Zillow rent index (lagged 6-12 months). Most predictable component.
- **Food** (13.5%): USDA Food Price Outlook.
- **Energy** (7%): Most volatile. Use EIA weekly gasoline prices (from FRED series GASREGW).
- **Core goods** (~20%): PPI and import prices.
- **Core services ex-shelter** (~25%): Wage growth data.

```
CPI_estimate = Σ(component_weight × component_forecast)
σ_forecast = calibrated from last 24 months of predictions vs actuals
P(CPI > threshold) = 1 - Φ((threshold - CPI_estimate) / σ_forecast)
```

**Cleveland Fed Nowcast**: Daily updated CPI estimate at 10 AM ET. Historically more accurate than Bloomberg consensus. Use as strong prior; update with latest energy prices.

**Known edge**: Kalshi CPI forecasts have 40% lower average error than Bloomberg consensus. When Kalshi diverges from consensus by >0.1pp one week before release, actual CPI shows significant deviation 80% of the time.

#### NFP Model (6/10 category rating)

**Approach**: Leading indicators regression.

```
NFP_estimate = β₀ + β₁×ADP + β₂×claims_trend + β₃×ISM_emp + β₄×prior_NFP
P(NFP > threshold) = Φ((NFP_estimate - threshold) / σ_NFP)
```

Calibrate β and σ from FRED historical data. ADP report (2 days before NFP) is the strongest single predictor.

#### Fed Rate Model (5/10 category rating)

**Approach**: CME FedWatch probability arbitrage.

CME FedWatch derives FOMC probabilities from deeply liquid Fed Funds futures ($B+ open interest). These are publicly available. Kalshi prices have diverged by up to **54 percentage points** from FedWatch.

```
P_model(action) = CME_FedWatch_probability
edge = P_model - Kalshi_market_price
```

**Caveat**: Per UCD study, Fed/finance is the **most efficient category on Kalshi**. Typical divergences are 3-8 cents. The 54-point case is extreme and rare. Trade only when divergence is substantial.

### Cross-Market Consistency Checks

Related markets must satisfy logical constraints:
```
P(temp > 80°F) ≤ P(temp > 75°F)   // higher threshold must be lower prob
P(CPI > 3.5%) ≤ P(CPI > 3.0%)     // same logic
P(Fed cuts 50bps) ≤ P(Fed cuts)     // subset must be ≤ superset
```

When market prices violate these constraints, guaranteed arb exists (buy cheap side, sell expensive side).

```cpp
struct ConsistencyCheck {
    std::string market_a;    // Should have higher probability
    std::string market_b;    // Should have lower probability
    double price_a;          // Market A price
    double price_b;          // Market B price
    double violation;        // price_b - price_a (positive = arb exists)
};
```

### Backtesting with GEFS Reforecast

Before deploying with real money, validate weather model against 20+ years of data:

1. Download GEFS reforecast archive for target stations
2. For each historical day: compute model probability from ensemble
3. Compare to actual observed temperature (from GHCN/IEM)
4. Compute Brier score across full history
5. **Must achieve Brier < 0.20 on historical data before live trading**

This is the single most important validation step. It's free and tells you exactly how good your model is.

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 10 | Probability framework + weather ensemble model | IProbabilityModel interface defined. WeatherEnsembleModel implements it: takes EnsembleForecast data, counts members above threshold, applies bias correction (rolling 30-60 day residuals), applies forecast horizon scaling. Multi-model blending (GFS + ECMWF). Unit tests verify: raw counting produces correct probability, bias correction shifts result, horizon scaling widens uncertainty. Backtest against at least 30 days of historical data with Brier score computation. |
| 11 | Economic models: CPI + NFP + Fed rate | CPI bottom-up model uses component weights + Cleveland Fed Nowcast. NFP regression on ADP + claims. Fed rate model uses FedWatch probabilities. All implement IProbabilityModel. Unit tests verify each against at least 3 historical events with known outcomes. Fee-aware edge calculation using actual Kalshi fee formula: `ceil(0.0175 * C * P * (1-P))`. |
| 12 | Cross-market consistency + reforecast backtester | ConsistencyChecker detects logical violations across related markets (monotonicity, subset). Reforecast backtester loads historical GEFS data + GHCN observations, runs weather model against full history, outputs Brier score + calibration curve. Must achieve Brier < 0.20 on historical weather data. Unit tests verify consistency detection with synthetic prices. Integration test runs backtester on at least 1 year of historical data. |
