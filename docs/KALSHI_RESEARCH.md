# Kalshi Prediction Market: Comprehensive Trading Research

## Table of Contents
1. [Market Structure and Mechanics](#1-market-structure-and-mechanics)
2. [Proven Trading Strategies](#2-proven-trading-strategies)
3. [Probability Modeling for Event Markets](#3-probability-modeling-for-event-markets)
4. [Edge Detection and Sizing](#4-edge-detection-and-sizing)
5. [Kalshi-Specific Tips and Tricks](#5-kalshi-specific-tips-and-tricks)
6. [Risk Management for Prediction Markets](#6-risk-management-for-prediction-markets)
7. [Data Sources and Predictive Power](#7-data-sources-and-predictive-power)
8. [Real-World Performance and Expectations](#8-real-world-performance-and-expectations)
9. [Market Making on Kalshi](#9-market-making-on-kalshi)
10. [Cross-Platform Arbitrage](#10-cross-platform-arbitrage)
11. [Market Category Deep Analysis & Ratings](#11-market-category-deep-analysis--ratings)
    - 11.1 Weather / Temperature (9/10)
    - 11.2 Economics: CPI (7/10)
    - 11.3 Economics: NFP (6/10)
    - 11.4 Economics: GDP (5/10)
    - 11.5 Federal Reserve Rate Decisions (5/10)
    - 11.6 Crypto Price Targets (3/10)
    - 11.7 Stock Market Levels (4/10)
    - 11.8 Politics / Elections (6/10 peak, 2/10 off-cycle)
    - 11.9 Natural Disasters / Hurricanes (7/10 seasonal)
    - 11.10 Entertainment / Awards (6/10)
    - 11.11 Sports (3/10)
    - 11.12 Company / Tech Events (4/10)
    - 11.13 Summary: Category Rankings & Capital Allocation

---

## 1. Market Structure and Mechanics

### Regulatory Status
- **CFTC-regulated** Designated Contract Market (DCM) -- the only CFTC-regulated prediction market exchange in the US
- All contracts are legally event contracts under US commodity law
- Clearing partners include Webull Financial LLC and Robinhood Derivatives LLC (as FCMs)

### Contract Structure
- **Binary contracts**: Every contract resolves to exactly **$1.00 (YES wins)** or **$0.00 (NO wins)**
- Prices range from **$0.01 to $0.99**, representing implied probability (e.g., $0.65 = 65% implied probability)
- **YES + NO always = $1.00** -- they are paired. When you buy YES at $0.40, someone else buys NO at $0.60
- Contracts are **cash-settled** in USD
- Maximum loss is always limited to the cost of the contract (bounded loss)

### Market Categories
- **Economics**: CPI, Fed rate decisions, GDP, nonfarm payrolls, unemployment
- **Weather/Climate**: Daily high temperatures (NYC, Chicago, Miami, Austin, LA, Denver), rainfall, snowfall, hurricanes, tornadoes
- **Crypto**: BTC, ETH, DOGE, SHIBA price movements
- **Sports**: NBA, NHL, NFL, MLB, golf, tennis, soccer
- **Politics/Policy**: Elections, government actions
- **Financial Indices**: KXINX, KXNASDAQ100

### Fee Structure (as of 2026)

**Taker Fee Formula:**
```
fee = round_up(0.07 * C * P * (1 - P))
```
Where C = number of contracts, P = price in dollars.

**Maker Fee Formula:**
```
fee = round_up(0.0175 * C * P * (1 - P))
```

**Key fee characteristics:**
- Maximum taker fee: **1.75 cents per contract** (occurs at 50-cent price point)
- Fees are **lowest** at extreme prices (near $0.01 or $0.99)
- Fees are **highest** at mid-range prices (40-60 cents)
- **No settlement fees** -- winning contracts pay out $1.00 with no deduction
- **No deposit fees** for ACH or wire; **2% fee** for debit card deposits/withdrawals
- **No withdrawal fees** for ACH or wire

**Fee examples at different price points:**
| Contract Price | Taker Fee/Contract | Fee as % of Price |
|---|---|---|
| $0.05 | ~0.33 cents | 6.7% |
| $0.10 | ~0.63 cents | 6.3% |
| $0.25 | ~1.31 cents | 5.3% |
| $0.50 | ~1.75 cents | 3.5% |
| $0.75 | ~1.31 cents | 1.7% |
| $0.90 | ~0.63 cents | 0.7% |
| $0.95 | ~0.33 cents | 0.3% |

### Order Types
- **Limit Orders**: Specify exact price; no guarantee of fill. Can be maker or taker depending on whether they rest on the book or fill immediately
- **Quick Orders (Market Orders)**: Execute immediately at best available price. Always taker orders
- **Time-in-force options**:
  - **EOD** (End of Day)
  - **IOC** (Immediate or Cancel) -- unfilled portion cancels immediately
  - **GTC** (Good Till Cancel / Valid until market expiration)
  - **Custom time** -- user-specified expiration
- **Post-Only**: Available via API; ensures order rests on book (maker) or is rejected
- **All orders are limit orders via API** -- market orders have been removed from the API

### Market Lifecycle
1. **Market Creation**: Suggested by members or Kalshi team; undergoes CFTC regulatory review
2. **Market Open**: Trading begins; YES and NO contracts available
3. **Active Trading**: Prices fluctuate based on trading activity; you can enter/exit at any time
4. **Market Close**: Trading halts at predetermined expiration time
5. **Determination**: Outcome verified against predetermined data source (e.g., NWS for weather, BLS for CPI). Takes **1-12+ hours** after close depending on when source data is available
6. **Settlement**: Winners receive $1.00 per contract. Usually completes within **~3 hours** after determination. Funds available for withdrawal

### Critical Gotcha: Void Rules
Markets that cannot be determined do NOT always void. Kalshi may resolve at the **"last traded fair price"** instead of voiding. This has caused significant losses (documented case of $30,000 loss). Key risk in sports markets where player participation is uncertain. This differs from traditional sportsbooks which typically void on non-participation.

---

## 2. Proven Trading Strategies

### Strategy 1: Economic Data Release Trading (CPI, NFP, GDP)

**The Edge**: Kalshi's CPI forecasts have shown **40% lower average error** than Bloomberg consensus estimates over a 25-month study period (Feb 2023 - mid 2025). When Kalshi's estimate diverges from consensus by >0.1 percentage points one week before release, the actual CPI shows a significant deviation **80% of the time** (vs 40% baseline).

**How to trade CPI:**
1. Build a bottom-up CPI estimate using component data (shelter, food, energy, core goods)
2. Compare your estimate to Kalshi market prices AND Bloomberg consensus
3. Use Cleveland Fed Inflation Nowcast as an additional signal (updates daily at 10 AM ET)
4. Trade when your model diverges from market-implied probability by >5%
5. Position size using fractional Kelly (see Section 4)

**NFP (Nonfarm Payrolls) approach:**
- Released first Friday of each month at 8:30 AM ET
- **ADP report** (released 2 days before NFP) serves as a leading indicator
- Use JOLTS data, ISM employment sub-indexes, and initial jobless claims as additional inputs
- Kalshi offers bracket contracts (e.g., "Will NFP be between 150K-200K?")

**GDP approach:**
- Atlanta Fed GDPNow provides real-time nowcast, updated 6-7 times per month
- Kalshi traders have found GDPNow sometimes overshoots -- the "model lag" trade involves positioning against extreme GDPNow readings
- In late 2025, when GDPNow showed 5.3% growth, Kalshi markets only priced 45-52% probability of a high-growth outcome

### Strategy 2: Weather Market Trading

**The Edge**: Weather forecasts have known biases and limitations. Human traders who deeply understand local microclimates and forecast model tendencies can find systematic mispricings.

**Approach:**
1. **Use multiple forecast models**: NWS, GFS (31-member ensemble), ECMWF, NAM
2. **Check station-specific forecasts** -- Kalshi settles on specific NWS stations:
   - NYC: **KNYC**
   - Chicago: **KORD**
   - Miami, Austin, LA, Denver: respective NWS stations
3. **Use ensemble probability**: Count fraction of GFS ensemble members above/below threshold. Example: 28/31 members above 70F = 90% model probability
4. **Compare model probability to market price**: If market says 70% but your model says 90%, you have a 20-point edge
5. **Watch for local anomalies**: Wildfires, urban heat island effects, inversion layers -- these cause forecast models to systematically err

**Key resources:**
- **Wethr.net**: Station-specific forecasts and historical data
- **Ventusky.com**: All weather models in one interface
- **Open-Meteo API**: Free access to GFS 31-member ensemble data
- **NWS API**: Official forecast data (api.weather.gov)

**Contract types**: Daily high temperature brackets, rainfall yes/no, snowfall, monthly hurricane/tornado counts

**Settlement**: Based on NWS final climate report, typically released the following morning

### Strategy 3: FOMC/Fed Rate Decision Trading

**The Edge**: Kalshi has maintained a **perfect forecast record** on the day before every FOMC meeting since 2022 (correctly predicting the modal outcome).

**Approach:**
1. Monitor CME FedWatch tool probabilities vs Kalshi prices for divergences
2. In late 2025, documented divergences of up to **54 percentage points** between CME FedWatch and Kalshi (e.g., CME 90% hold vs Kalshi 64% cut)
3. **Combo contracts**: Kalshi offers "Fed Combo" markets combining rate decision + number of dissents
4. Watch for positioning shifts in the 48 hours before announcements
5. Trade press conference keywords -- Kalshi has markets on specific terms Powell will emphasize

**Cross-market signals:**
- CME FedWatch tool (derived from Fed funds futures)
- Treasury yield curve movements
- Eurodollar/SOFR futures
- Kalshi's own multiple Fed-related markets (rate level, decision type, dissents)

### Strategy 4: Fade Overreactions
- Markets frequently overreact to rumors, partial data, or misreported news
- Buy during dips or sell during spikes when sentiment is temporarily distorted
- Requires real-time news monitoring and quick assessment of whether the reaction is proportional

### Strategy 5: Buy Early / Sell Before Settlement
- News moves markets before final outcomes are known
- Profit on price movement rather than waiting for settlement
- Lower risk since you can exit if thesis changes
- Requires sufficient market liquidity for clean exits

### Strategy 6: Correlated Event Combos
- When two events logically move together, bundle positions
- Example: Cold weather correlates with lower NFL passing yards and higher rushing attempts
- Trade weather + sports markets together for correlated edge

---

## 3. Probability Modeling for Event Markets

### Bayesian Probability Estimation

**Base rate approach:**
1. Start with historical base rates (e.g., CPI MoM has been 0.2-0.4% in 80% of months over last 2 years)
2. Update with current data as it arrives
3. Weight recent data more heavily than distant data
4. Output: probability distribution over possible outcomes

**For CPI specifically:**
1. Gather component-level data:
   - **Shelter** (33% of CPI weight): Use Zillow rent index, Case-Shiller, new lease data
   - **Food** (13.5%): USDA Food Price Outlook, commodity prices
   - **Energy** (7%): Daily oil prices, weekly gasoline prices (EIA data)
   - **Core goods** (~20%): Import prices, PPI
   - **Core services ex-shelter** (~25%): PCE services data, wage growth
2. Weight components by CPI basket weights (available from BLS)
3. Sum weighted component estimates for headline number
4. Compare to market-implied distribution on Kalshi

### Ensemble Forecasting Techniques

**For weather:**
- NOAA's Global Ensemble Forecast System (GEFS): **21 members**, produced 4x daily, 16-day horizon
- GFS ensemble via Open-Meteo: **31 members**
- Method: Count fraction of ensemble members in each bracket = raw probability
- Calibrate using historical forecast verification data
- Higher ensemble agreement = higher confidence signal

**For economic data:**
- Use multiple nowcast models (Cleveland Fed, Atlanta Fed GDPNow, NY Fed)
- Weight by historical accuracy
- Bayesian Model Averaging: combine models weighted by their posterior probability

### Measuring Model Accuracy: Brier Score

**Formula:**
```
Brier Score = (1/N) * sum((forecast_probability - actual_outcome)^2)
```
Where actual_outcome is 0 or 1.

**Interpretation:**
- **0.0** = perfect calibration
- **0.25** = random guessing (for 50/50 events)
- **1.0** = always wrong
- Polymarket achieves **0.187** across 2,847 markets (strong calibration)

**Decomposition:**
```
Brier Score = Reliability - Resolution + Uncertainty
```
- **Reliability**: Measures calibration (are your 70% predictions right 70% of the time?)
- **Resolution**: Measures discrimination (do you assign different probabilities to events that resolve differently?)
- **Uncertainty**: Irreducible randomness in outcomes

**How to calibrate your model:**
1. Track every prediction with probability and outcome
2. Bin predictions (e.g., 0-10%, 10-20%, ..., 90-100%)
3. Plot actual win rate vs predicted probability
4. Perfect calibration = points on the diagonal
5. Adjust model if systematically overconfident or underconfident

---

## 4. Edge Detection and Sizing

### Calculating Edge

**Expected Value per contract:**
```
EV = P(true) * $1.00 - Market_Price
```

**Example:** Your model says 75% chance of event occurring. Market price is $0.60.
```
EV = 0.75 * $1.00 - $0.60 = $0.15 per contract (25% ROI)
```

**Fee-aware edge calculation:**
```
Net_EV = P(true) * $1.00 - Market_Price - Fee
```
Using the taker fee formula: fee = 0.07 * P * (1-P)
```
Net_EV = 0.75 * $1.00 - $0.60 - 0.07 * 0.60 * 0.40
Net_EV = 0.75 - 0.60 - 0.0168 = $0.1332 per contract
```

**Minimum edge to overcome fees:**
- At 50-cent contracts: need >1.75 cents edge (>3.5% probability edge)
- At 10-cent contracts: need >0.63 cents edge (>6.3% probability edge)
- At 90-cent contracts: need >0.63 cents edge (>0.7% probability edge)

### Kelly Criterion for Binary Outcomes

**Standard Kelly formula:**
```
f* = (b * p - q) / b
```
Where:
- f* = fraction of bankroll to bet
- p = your estimated true probability
- q = 1 - p
- b = net odds = (1 - Market_Price) / Market_Price

**Worked example:**
- Market price: $0.60 (implied 60%)
- Your estimate: 75% true probability
- b = (1 - 0.60) / 0.60 = 0.667
- f* = (0.667 * 0.75 - 0.25) / 0.667 = **0.375 (37.5% of bankroll)**

**Full Kelly is too aggressive for real trading.** Use fractional Kelly:

| Fraction | Risk Level | Notes |
|---|---|---|
| 0.25x Kelly | Conservative | Recommended for beginners and small bankrolls |
| 0.50x Kelly | Moderate | Good balance of growth and safety |
| 1.0x Kelly | Aggressive | 33% chance of halving bankroll before doubling |

**For the example above at 0.25x Kelly:**
- Bet size = 0.25 * 0.375 = **9.4% of bankroll**
- With $500 bankroll: bet $47

### Position Sizing for Small Capital ($100-$1,000)

**Hard rules:**
- Never bet more than **25% of bankroll** on a single position (regardless of Kelly)
- Never bet more than **5% of bankroll** per trade for correlated events
- With $500 bankroll, max single position = $125
- Minimum practical trade size on Kalshi: $1 (1 contract at $0.01)

**Practical sizing table ($500 bankroll, 0.25x Kelly):**
| Edge (prob points) | Market Price | Kelly Full | 0.25x Kelly | Dollar Bet |
|---|---|---|---|---|
| 5% | $0.50 | 10% | 2.5% | $12.50 |
| 10% | $0.50 | 20% | 5.0% | $25.00 |
| 15% | $0.50 | 30% | 7.5% | $37.50 |
| 20% | $0.50 | 40% | 10.0% | $50.00 |
| 5% | $0.20 | 6.3% | 1.6% | $7.81 |
| 10% | $0.80 | 50% | 12.5% | $62.50 |

### Academic Kelly Result for Prediction Markets

For bounded contracts (price between 0 and 1), the optimal Kelly fraction is:
```
f* = (Q - P) / (1 + Q)
```
Where P = p/(1-p) and Q = q/(1-q) are odds ratios (p = market price, q = your probability).

Key insight: **Deviations from optimal f* cause quadratic (not linear) degradation in utility** -- meaning being slightly off on sizing is much less costly than being way off. This provides robustness against probability estimation errors.

---

## 5. Kalshi-Specific Tips and Tricks

### API Technical Details

**Authentication**: RSA-PSS signed requests with three headers:
- `KALSHI-ACCESS-KEY`: Your API key ID
- `KALSHI-ACCESS-SIGNATURE`: Base64-encoded RSA-PSS signature of `timestamp + method + path`
- `KALSHI-ACCESS-TIMESTAMP`: Unix timestamp in **milliseconds** (not seconds -- common gotcha)
- Signing uses SHA256 with PSS padding, `DIGEST_LENGTH` salt

**Base URLs:**
| Environment | REST | WebSocket |
|---|---|---|
| Production | `https://api.elections.kalshi.com/trade-api/v2` | `wss://api.elections.kalshi.com/trade-api/ws/v2` |
| Demo | `https://demo-api.kalshi.co/trade-api/v2` | `wss://demo-api.kalshi.co/trade-api/ws/v2` |

**Rate Limits by Tier:**
| Tier | Read/sec | Write/sec | Qualification |
|---|---|---|---|
| Basic | 20 | 10 | Account signup |
| Advanced | 30 | 30 | Qualification form |
| Premier | 100 | 100 | 3.75% monthly volume + tech competency |
| Prime | 400 | 400 | 7.5% monthly volume + tech competency |

**Write-limited operations** (count against write limits):
- CreateOrder, CancelOrder, AmendOrder, DecreaseOrder
- BatchCreateOrders, BatchCancelOrders
- Batch items count as 1 each, except BatchCancelOrders where each cancel = 0.2 transactions

**WebSocket Channels:**
- **Public**: `orderbook_delta`, `ticker`, `trade`, `market_lifecycle_v2`
- **Private**: `fill`, `user_orders`, `market_positions`, `order_group_updates`
- Note: Even public channels require authentication headers

**Key REST Endpoints:**
| Endpoint | Method | Purpose |
|---|---|---|
| `/markets` | GET | List all markets with optional filters |
| `/markets/{ticker}` | GET | Single market details |
| `/markets/{ticker}/orderbook` | GET | Order book with bid/ask levels |
| `/portfolio/orders` | POST | Place limit orders |
| `/portfolio/orders` | GET | List user orders by status |
| `/portfolio/orders/{id}` | DELETE | Cancel order |
| `/portfolio/positions` | GET | Current holdings |
| `/portfolio/balance` | GET | Available cash balance |
| `/portfolio/settlements` | GET | Settlement history |
| `/account/limits` | GET | Query rate limit tier |

**Order Placement Parameters (API):**
```json
{
  "ticker": "KXCPI-26MAR",
  "side": "yes",
  "action": "buy",
  "count_fp": "10.00",
  "yes_price_dollars": "0.6500",
  "client_order_id": "<uuid>",
  "time_in_force": "fill_or_kill",
  "post_only": true
}
```

### Key API Gotchas
1. **Demo requires separate API keys** from production
2. **Timestamps must be in milliseconds**, not seconds
3. **All API orders are limit orders** -- no market orders via API
4. **Order book returns bids only** -- no explicit asks; ascending order with best bid last
5. **Prices are now strings with 4 decimal places** (e.g., "0.6500") as of March 2026. Legacy integer cent fields deprecated
6. **Quantities use `count_fp` strings** (e.g., "10.00")
7. **Historical data partitioned**: Live data ~3 months; older data via `/historical/*` endpoints
8. **30-minute token refresh** requirement for long-running sessions
9. **FIX 4.4 protocol** available for institutional use (contact Kalshi directly)

### Demo Environment for Paper Trading
- Fully functional mirror of production
- Same API endpoints, just different base URL
- Free to use -- no real money at risk
- All tools transfer seamlessly to production with URL change
- **Highly recommended**: Test all strategies here first

### Common Mistakes to Avoid
1. **Chasing momentum** -- entering after price has already moved significantly
2. **Ignoring fees** -- mid-priced contracts have highest fees; longshots have highest fee-to-price ratio
3. **Not using limit orders** -- quick/market orders always pay taker fees; limit orders can avoid fees entirely
4. **Slippage on thin books** -- the displayed price may not have enough depth for your order size
5. **Misunderstanding void rules** -- markets may settle at "last traded fair price" rather than voiding (documented $30K loss case)
6. **Trading too many markets** -- spreading capital thin reduces the impact of any edge
7. **Not checking settlement sources** -- always verify which data source resolves the contract
8. **Ignoring adverse selection on limit orders** -- limit orders that fill tend to fill when the price is moving against you

---

## 6. Risk Management for Prediction Markets

### Bounded Loss Advantage
- Maximum loss per contract = cost of the contract
- No margin calls, no leverage blow-ups
- A $0.10 contract can only lose $0.10 -- you cannot lose more
- This makes prediction markets fundamentally different from leveraged trading

### Position-Level Risk Rules
1. **Max single position**: 25% of bankroll (hard cap regardless of Kelly)
2. **Max per-trade risk**: 5% of bankroll for individual trades
3. **Correlated positions**: Reduce to 2-3% each when holding multiple correlated bets
4. **Total exposure**: Never have >70% of bankroll deployed at once (keep 30% cash reserve)

### Diversification Across Markets
- Trade across **uncorrelated** market categories (weather + economics + sports)
- Within economics: CPI and NFP are partially correlated (both reflect labor market); diversify with GDP, housing
- Weather markets across different cities have low correlation
- **Target 5-15 simultaneous positions** for adequate diversification with small capital

### Correlation Awareness
- Fed rate markets correlate with: CPI markets, GDP markets, employment markets
- Weather markets: geographically proximate cities may correlate (NYC and Chicago winter storms)
- Sports markets: same-game props are highly correlated
- **Rule**: If two positions would both lose under the same scenario, treat them as one position for sizing

### When NOT to Trade
1. **Edge < fee + slippage**: If your model's edge doesn't exceed transaction costs
2. **Low liquidity**: If you can't exit at a reasonable price
3. **Approaching settlement with thin edge**: Risk/reward deteriorates as binary resolution approaches
4. **After major news shock**: Prices may be stale or spreads very wide
5. **When model inputs are uncertain**: Don't trust your CPI estimate if key component data is missing
6. **Markets you don't understand**: Stick to areas of expertise

### Bankroll Management
- Start with money you can afford to lose entirely
- Use separate "trading bankroll" from personal finances
- Track P&L rigorously from day one
- Consider 3-month evaluation periods before scaling up
- **Recommended starting bankroll**: $200-$500 for learning

---

## 7. Data Sources and Predictive Power

### CPI (Consumer Price Index)

**Primary Sources:**
| Source | URL | Update Frequency | Use |
|---|---|---|---|
| BLS CPI Data | https://www.bls.gov/cpi/ | Monthly (2nd or 3rd week) | Official release; settlement source |
| BLS Component Tables | https://www.bls.gov/news.release/cpi.t01.htm | Monthly | 81 component breakdown for bottom-up estimates |
| Cleveland Fed Inflation Nowcast | https://www.clevelandfed.org/indicators-and-data/inflation-nowcasting | Daily at 10 AM ET | Real-time CPI and PCE nowcast |
| BLS CPI Charts | https://www.bls.gov/charts/consumer-price-index/ | Monthly | Visual component analysis |

**Cleveland Fed Nowcast methodology:**
- Uses daily oil prices, weekly gasoline prices, monthly CPI/PCE readings
- Reports month-over-month (non-annualized) and year-over-year rates
- Historically **more accurate than professional forecaster surveys** and comparable to the Fed's internal Greenbook
- Available in real-time (unlike Greenbook)

**Bottom-up CPI Estimation:**
- CPI has 81 components, 36 of which are not seasonally adjusted
- **Shelter** (33% weight): Largest single driver. Rose 0.2% in Feb 2026
- **Food** (13.5%): Food at home vs food away from home (use USDA Food Price Outlook)
- **Energy** (7%): Most volatile component. Use daily oil + weekly gas prices
- **Core goods**: Use PPI and import price data
- Weight each component estimate by its BLS basket weight and sum

### GDP

| Source | URL | Update Frequency | Use |
|---|---|---|---|
| Atlanta Fed GDPNow | https://www.atlantafed.org/research-and-data/data/gdpnow | 6-7x per month | Real-time GDP growth nowcast |
| GDPNow on FRED | https://fred.stlouisfed.org/series/GDPNOW | Same as above | Programmatic access |
| BEA GDP Release | https://www.bea.gov/data/gdp | Quarterly (advance, 2nd, 3rd) | Official settlement data |

**GDPNow accuracy note**: Sometimes overshoots dramatically. In Q4 2025, showed 5.3% while Kalshi only priced 45-52% for high growth -- traders exploit this "model lag."

### Fed Rate Decisions

| Source | URL | Use |
|---|---|---|
| CME FedWatch Tool | https://www.cmegroup.com/markets/interest-rates/cme-fedwatch-tool.html | Implied probabilities from Fed Funds futures |
| FRED Fed Funds Rate | https://fred.stlouisfed.org/series/FEDFUNDS | Historical rate data |
| FOMC Schedule | https://www.federalreserve.gov/monetarypolicy/fomccalendars.htm | Meeting dates |

**FedWatch vs Kalshi divergences**: Up to 54 percentage points observed (CME 90% hold vs Kalshi 64% cut in early 2026). These divergences reflect different trader populations and methodologies.

### Employment / NFP

| Source | URL | Timing | Use |
|---|---|---|---|
| BLS Employment Report | https://www.bls.gov/news.release/empsit.htm | 1st Friday, 8:30 AM ET | Official NFP data |
| ADP National Employment | https://adpemploymentreport.com | 2 days before NFP | Leading indicator for NFP |
| JOLTS | https://www.bls.gov/jlt/ | Monthly | Job openings, quits (leading) |
| Initial Jobless Claims | https://www.dol.gov/ui/data.pdf | Weekly (Thursdays) | High-frequency labor signal |
| ISM Employment Sub-Index | https://www.ismworld.org | Monthly | Manufacturing + services employment |

### Weather

| Source | URL | Use |
|---|---|---|
| NWS Forecasts | https://forecast.weather.gov | Official forecasts, station-specific |
| NWS API | https://api.weather.gov | Programmatic access to forecasts |
| Open-Meteo | https://open-meteo.com | Free GFS 31-member ensemble data |
| Wethr.net | https://wethr.net | Station-specific forecasts + historical |
| Ventusky | https://ventusky.com | Multi-model visualization |
| NOAA GEFS | https://www.ncei.noaa.gov/products/weather-climate-models/global-ensemble-forecast | 21-member ensemble, 4x daily |
| NOAA Climate Prediction Center | https://www.cpc.ncep.noaa.gov | Extended outlooks |

### General Economic Data (FRED API)

| Detail | Info |
|---|---|
| URL | https://fred.stlouisfed.org/docs/api/fred/ |
| Coverage | 840,000+ time series from 118 sources |
| Cost | Free (requires free API key) |
| Python library | `fredapi` (pip install fredapi) |
| Key series | CPIAUCSL (CPI), UNRATE (unemployment), GDP, FEDFUNDS, T10Y2Y (yield curve) |

---

## 8. Real-World Performance and Expectations

### Academic Study Results (300,000+ contracts analyzed)

Source: Karl Whelan, University College Dublin -- "Makers and Takers: The Economics of the Kalshi Prediction Market"

**Overall market statistics:**
- Average pre-fee return across all contracts: **-20%**
- **Makers** average loss: ~10%
- **Takers** average loss: ~32%

**Favorite-Longshot Bias (quantified):**
| Contract Price | Expected Win Rate | Actual Win Rate | Return |
|---|---|---|---|
| $0.05 | 5% | ~2% | -60% |
| $0.10 | 10% | <10% | Significantly negative |
| $0.50 | 50% | ~50% | Near zero (minus fees) |
| $0.90 | 90% | ~90% | Small positive |
| $0.95 | 95% | ~98% | Small positive |

**Key insight**: Low-price (longshot) contracts systematically underperform. High-price (favorite) contracts slightly outperform. This bias is **much stronger for takers than makers**.

### Kalshi's Forecasting Accuracy (Federal Reserve / NBER Study)
- Kalshi's modal forecast correctly predicted the federal funds rate on the **day before every FOMC meeting since 2022**
- CPI forecasts: 40% lower average error than Bloomberg consensus
- When Kalshi diverges from consensus by >0.1pp one week before CPI release, actual CPI shows significant deviation 80% of the time
- Performance edge most visible during periods of heightened economic volatility

### Realistic Edge Sizes
- **3-5% edge** over market consensus is a strong, tradeable edge
- **10%+ edge** is exceptional and rare -- found primarily in niche markets or immediately after relevant data releases
- Even 3-5% edge compounds significantly via Kelly criterion over many trades

### Realistic Returns by Capital Level
| Capital | Approach | Monthly Return | Notes |
|---|---|---|---|
| $100-500 | Learning, few markets | -10% to +5% | Expect to lose initially while learning |
| $500-5,000 | Single market focus | $50-200 | After 3+ months of learning |
| $5,000-25,000 | Multi-market, semi-automated | $200-1,000 | Requires systematic approach |
| $25,000-100,000 | Full automation, diversified | $1,000-5,000 | Professional-grade systems needed |

### How Many Markets to Trade Simultaneously
- **Beginner (learning)**: 1-3 markets in your area of expertise
- **Intermediate**: 5-10 markets across 2-3 categories
- **Advanced/Automated**: 15-50+ markets with systematic approach

### Expected Win Rates
- A well-calibrated model should have Brier score **< 0.20** (vs 0.25 for random)
- Target **55-65% win rate** on balanced-odds trades (around 50-cent contracts)
- Higher win rates on favorites (80%+ contracts) but smaller per-contract profit
- **Lower win rates are fine** if edge per trade is large (longshot hunting)

### Key Performance Insight
The structural advantage lies in being a **maker, not a taker**:
- Makers lose ~10% on average (vs takers at ~32%)
- Maker fees are 4x lower than taker fees
- The path to profitability on Kalshi almost certainly involves limit orders and patience
- Adverse selection risk on limit orders is real but manageable with proper strategy

---

## 9. Market Making on Kalshi

### How Market Making Works on Kalshi
1. Post a **bid** (buy) order at price X
2. Post an **ask** (sell) order at price X + spread
3. When both sides fill, you earn the spread
4. Example: Bid $0.48, Ask $0.52 -> **$0.04 profit per round-trip**

### Spread Requirements for Profitability
- Minimum spread must exceed **maker fees on both sides + expected adverse selection cost**
- At 50-cent midpoint: maker fee = ~0.44 cents per side -> minimum spread = ~2 cents after fees
- In practice, **4-8 cent spreads** are typical for profitable MM on Kalshi
- Wider spreads needed near events (news risk) and for illiquid markets
- Narrower spreads possible on stable, high-volume markets

### Inventory Management
- **Goal**: Stay as close to neutral (zero inventory) as possible
- **Quote skewing**: When holding excess YES contracts, lower your ask price and raise your bid price to encourage sells
- **Linear skewing**: Adjust spread proportionally to inventory size
- **Hard position limits**: Cap maximum inventory per market (e.g., 100-200 contracts)
- **Time-based reduction**: Aggressively reduce inventory as settlement approaches (binary settlement = total loss on wrong side)

### Reservation Price Model (Avellaneda-Stoikov adapted for binary)
```
reservation_price = mid_price - inventory * risk_aversion * volatility^2
optimal_spread = volatility^2 * risk_aversion + (2/risk_aversion) * ln(1 + risk_aversion/inventory_penalty)
```
For binary contracts, volatility = sqrt(P * (1-P)) where P is the current mid-price probability.

### Risk Controls for Market Making
1. **Cancel all orders on rapid price movement** (>5 cent move in <1 minute)
2. **Widen spreads before scheduled events** (FOMC, CPI release, game start)
3. **Pause quoting during major news** -- integrate news feeds
4. **Position limits per market**: Never hold more than X% of bankroll in any single market
5. **Daily loss limit**: Stop trading if daily loss exceeds Y% of bankroll

### Event Risk
- News can move prices **40-50 points instantly** (e.g., surprise CPI print)
- If you're making a market on CPI and holding inventory when the number drops, you can lose your entire position
- **Before any scheduled data release, FLATTEN INVENTORY or widen spreads dramatically**

### Kalshi Market Maker Program
- Formal program for designated market makers
- Requirements: Financial resources, trading experience, business reputation, ability to maintain liquidity
- Benefits: Reduced fees, adjusted position limits
- Obligation: **98% quoting availability** per 1-hour increment
- Products: 90+ products across all categories
- Application: Through Kalshi directly after demonstrating capabilities

### Liquidity Incentive Program
- Separate from formal MM program
- Rewards placing resting orders that improve market liquidity
- Earn payments for maintaining orders on the books
- Good way to start market making without formal MM status

### Practical Infrastructure
- **Manual market making is impractical** -- requires automation
- Target **sub-10ms total latency** for strategy components
- Use VPS with 99.9%+ uptime (not home computer)
- Python is preferred with official client libraries
- Start on demo environment, then transition to production

---

## 10. Cross-Platform Arbitrage

### Fundamental Mechanics
**Same-event arbitrage**: Buy YES on Platform A + Buy NO on Platform B. If total cost < $1.00, guaranteed profit regardless of outcome.

**Example:**
- Platform A (Kalshi): YES at $0.42
- Platform B (Polymarket): NO at $0.55 (equivalent to YES at $0.45)
- Total cost: $0.42 + $0.55 = $0.97
- Guaranteed payout: $1.00
- Profit: $0.03 per pair (3.1% return)

**Scaling:**
- 100 pairs: $97 cost -> $3 profit
- 1,000 pairs: $970 cost -> $30 profit
- 10,000 pairs: $9,700 cost -> $300 profit

### Fee Impact on Arbitrage
- **Kalshi taker fee**: ~1.75 cents max at midpoint
- **Polymarket taker fee**: ~0.01% on trades (essentially zero)
- **USDC/USD conversion friction**: 0.1-0.3%
- **Slippage**: 0.5-1% on each side
- **Minimum viable gross spread**: **3-5%** to cover all costs

### Where Opportunities Exist

**Most reliable divergences:**
1. **Fed rate decision contracts**: Different trader populations on each platform
2. **Macro event markets** (CPI, GDP, NFP): Regulatory differences create systematic divergence
3. **Political markets**: Different information environments (US vs global)

**Frequency**: Gross spreads >5% occur approximately **15-20% of the time** across matched markets. Net edge after fees is **1-3%**.

**Timing**: The **48-hour window** around scheduled announcements produces the widest and most reliable spreads.

### Platform Comparison

| Feature | Kalshi | Polymarket |
|---|---|---|
| Regulation | CFTC-regulated (US) | Crypto-native (non-US / gray area US) |
| Settlement | USD | USDC (crypto) |
| Maker Fees | ~0.44 cents/contract at midpoint | Zero |
| Taker Fees | ~1.75 cents/contract max | 0.01% |
| API Latency | Sub-50ms (WebSocket) | Sub-50ms (WebSocket) |
| Open Interest | ~$450M | Higher on major markets |
| Authentication | RSA-PSS signed | L2 private key |
| Legal for US | Yes | Uncertain (Polymarket US charges 0.01%) |

### Kalshi vs CME FedWatch Arbitrage
- Not direct arbitrage (different instruments) but **cross-market signal**
- When Kalshi diverges significantly from CME FedWatch, institutional desks:
  - Buy "No Cut" on Kalshi + Go long interest rate futures on CME
  - Or vice versa
- Requires CME futures account + Kalshi account
- More suitable for institutional capital ($100K+)

### Risks of Cross-Platform Arbitrage
1. **Execution risk**: Prices change between placing orders on different platforms
2. **Settlement risk**: Platforms may define/resolve the same event differently (different data sources, different criteria)
3. **Liquidity risk**: Thin markets can prevent fair exits
4. **Capital lockup**: Funds tied up until event resolution (could be weeks/months)
5. **Regulatory risk**: US traders face legal uncertainty using Polymarket
6. **Smart contract risk**: Polymarket runs on blockchain -- contract bugs possible
7. **Speed**: Opportunities last **seconds, not minutes**. Bots capture most arb. Manual execution is extremely difficult

### Arbitrage Tools
- **Dune Analytics dashboard**: https://dune.com/the_liolik/99c (Polymarket/Kalshi scanner)
- **DeFi Rate calculators**: https://defirate.com/prediction-markets/calculators/
- **Custom bots**: See GitHub repos for reference implementations

### Realistic Expectations for Arbitrage
- **Monthly returns**: Claimed 12-20% for sophisticated automated systems (aggressive estimate)
- **Annual returns**: 10-20% more realistic for well-executed systematic approach
- **Capital requirement**: Meaningful only with $5,000+ deployed across platforms
- **Key bottleneck**: Speed of execution -- this is primarily a bot game, not a manual trading strategy

---

## 11. Market Category Deep Analysis & Ratings

This section evaluates every Kalshi market category for a solo quantitative trader with $100 starting capital, optimizing for profit. Each category is rated 1-10 across six dimensions, then given an overall confidence rating.

**Rating Dimensions:**
1. **Data Quality** — Are free, quantitative, machine-readable data sources available? How calibrated are they?
2. **Back-testability** — Can you validate your model against historical data before risking capital?
3. **Contract Frequency** — How often do contracts resolve? Higher frequency = faster capital turnover = more compounding.
4. **Resolution Objectivity** — Is the settlement criteria unambiguous? Lower dispute risk = lower operational risk.
5. **Competition Weakness** — How unsophisticated is the typical participant? Weaker competition = wider mispricing.
6. **Fee-Adjusted Edge** — After Kalshi's fee structure, does sufficient edge remain?

---

### 11.1 Weather / Temperature

**What's Available**: Daily high temperature contracts for 6 cities (NYC, Chicago, Miami, LA, Denver, Austin). Bracket contracts ("Will NYC high exceed 75°F?") resolving daily against NWS station readings. Also: rainfall, snowfall, monthly hurricane/tornado counts.

**Data Sources (Free, Quantitative, API-Accessible)**:

| Source | What It Provides | Update Frequency | API Quality |
|---|---|---|---|
| NOAA GEFS (via NOMADS) | 31-member ensemble forecasts in GRIB2 | 4x daily (00Z/06Z/12Z/18Z) | Raw but comprehensive |
| Open-Meteo API | GFS 31-member + ECMWF 51-member ensemble, JSON | 4x daily | Excellent — free, no key, clean JSON |
| National Blend of Models (NBM) | Pre-calibrated probabilistic forecasts | 4x daily | Best single source for calibrated probabilities |
| NWS API (api.weather.gov) | Official point forecasts, station observations | Hourly+ | Good — free, no key required |
| Iowa Environmental Mesonet | Aggregated ASOS/AWOS station data, historical | Continuous | Excellent for historical verification |
| GHCN (NOAA NCEI) | 100+ years of daily temperature observations | Daily | Definitive historical record |
| NOAA GEFS Reforecast Archive | 20+ years of retrospective ensemble forecasts | Static archive | Gold standard for backtesting |

**Modeling Approach**:
1. Pull GEFS/ECMWF ensemble for target city's NWS station grid point
2. Count ensemble members exceeding the contract threshold → raw probability
3. Apply bias correction using rolling 30-60 day GEFS-vs-actual residuals (GEFS has documented warm bias in winter, cold bias in summer for specific stations)
4. Apply ensemble calibration via Non-homogeneous Gaussian Regression (EMOS) or simpler quantile mapping
5. Compare calibrated probability to Kalshi market price
6. Trade when divergence exceeds fee threshold (~3-5 cents depending on contract price)

**Known Mispricings**:
- Markets anchor to climatological averages rather than updating to latest model runs
- Overnight/weekend staleness: NOAA runs update at ~4am and ~4pm ET; markets often don't adjust until trading activity picks up
- Shoulder seasons (spring/fall) have widest ensemble spread = most opportunity for mispricing
- Local phenomena (urban heat island, lake-effect, inversion layers) cause systematic forecast bias that ensemble models partially miss but station-specific correction captures
- One documented weather bot using GFS ensemble data achieved $1.8K in profits

**Back-testability**: **Exceptional.** GEFS reforecast archive provides 20+ years of retrospective ensemble forecasts. GHCN provides 100+ years of station observations. You can simulate your exact model against decades of historical data before risking a cent.

**Resolution**: NWS final climate report for the relevant station, typically released the following morning. Fully objective — a thermometer reading.

**Competition**: Low sophistication. Most participants trade on intuition ("feels warm today") or anchor to weather app point forecasts. A calibrated ensemble model is a structural, repeatable advantage.

**Fee Math**: Temperature contracts often misprice by 5-15 cents vs ensemble probability. At a 50-cent contract, maker fee is ~0.44 cents. At a 20-cent contract, maker fee is ~0.28 cents. Net edge after fees is strongly positive when mispricing exceeds 3-5 cents.

**At $100 Capital**: Daily resolution means capital turns over every day. Spread across 3-5 city/threshold combinations at $3-10 per position. Rapid feedback loop for model calibration.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 10 | Institutional-grade ensemble data, free, multiple redundant sources |
| Back-testability | 10 | 20+ years of reforecast data + 100+ years of observations |
| Contract Frequency | 10 | Daily resolution across 6 cities = up to 6+ trades/day |
| Resolution Objectivity | 10 | NWS station thermometer reading — zero ambiguity |
| Competition Weakness | 9 | Mostly intuition-based traders; few quantitative participants |
| Fee-Adjusted Edge | 8 | 5-15 cent mispricings vs ~0.3-0.5 cent maker fees |

**Overall Rating: 9/10** — Best category for systematic, data-driven trading at any capital level. Start here.

---

### 11.2 Economics: CPI (Consumer Price Index)

**What's Available**: Monthly contracts on CPI month-over-month change, year-over-year level, and bracket contracts (e.g., "Will CPI MoM be between 0.2% and 0.3%?"). Resolves against BLS release, typically 2nd or 3rd week of each month.

**Data Sources**:

| Source | What It Provides | Update Frequency | Predictive Power |
|---|---|---|---|
| Cleveland Fed Inflation Nowcast | Daily CPI/PCE point estimate + distribution | Daily at 10 AM ET | ~0.05% MAE on MoM CPI — best single predictor |
| BLS Component Tables | 81 CPI sub-components with weights | Monthly | Enables bottom-up estimation |
| EIA Gasoline Prices | Weekly retail gasoline prices | Weekly (Mondays) | Energy component (~7% CPI weight) |
| Manheim Used Vehicle Index | Wholesale used car prices | Monthly (mid-month) | Core goods driver, leads CPI by ~2 months |
| USDA Food Price Outlook | Food price projections | Monthly | Food component (~13.5% CPI weight) |
| FRED API | 840K+ economic time series | Varies | Context for all macro modeling |
| BLS Release Calendar | Exact release dates, 1 year ahead | Annual | Timing for position entry/exit |

**Modeling Approach**:
1. Build bottom-up CPI estimate from components (shelter 33%, food 13.5%, energy 7%, core goods 20%, core services ex-shelter 25%)
2. Cross-reference with Cleveland Fed Nowcast daily updates
3. Monitor high-frequency inputs (daily gasoline prices, weekly jobless claims) for directional signals
4. Compare model distribution to Kalshi bracket contract prices
5. Trade when model probability diverges from market by >5 percentage points

**Known Edge**: Kalshi CPI forecasts have shown 40% lower average error than Bloomberg consensus. When Kalshi diverges from consensus by >0.1pp one week before release, the actual CPI shows significant deviation 80% of the time (vs 40% baseline). The Cleveland Fed Nowcast shifts before Kalshi markets update — trading this lag is a documented edge.

**Back-testability**: Strong. BLS historical data goes back decades. Cleveland Fed Nowcast methodology is published. Philadelphia Fed Real-Time Data Center provides vintage data (what was known at each point in time), enabling proper out-of-sample backtesting.

**Resolution**: BLS official release — fully objective.

**Competition**: Moderate. Some institutional participants and economics-focused traders. But Kalshi prices still anchor to consensus estimates and update sluggishly after component data releases.

**Fee Math**: CPI bracket contracts trade at various prices. A 5-10 cent edge on a 40-cent contract yields ~4-9 cents after ~0.44 cent maker fee. Viable.

**At $100 Capital**: Monthly resolution limits capital turnover. You deploy $10-30 on CPI brackets, wait 2-4 weeks for resolution. Maybe 12-15 CPI trading opportunities per year. Solid but slow compared to weather.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 9 | Cleveland Fed Nowcast is gold standard; BLS components enable bottom-up |
| Back-testability | 8 | Decades of data; Philly Fed vintages enable proper backtesting |
| Contract Frequency | 4 | Monthly — only 12 resolution events per year |
| Resolution Objectivity | 10 | BLS official statistic — zero ambiguity |
| Competition Weakness | 6 | More informed than weather traders, but still anchoring biases |
| Fee-Adjusted Edge | 7 | 5-10 cent mispricings documented, viable after fees |

**Overall Rating: 7/10** — Strong edge per trade, but monthly frequency limits compounding at $100. Best as a secondary category alongside weather.

---

### 11.3 Economics: Nonfarm Payrolls (NFP)

**What's Available**: Monthly bracket contracts on total nonfarm payroll change (e.g., "Will NFP be between 150K-200K?"). Resolves on BLS Employment Situation release, first Friday of each month at 8:30 AM ET.

**Data Sources**:

| Source | What It Provides | Lead Time | Predictive Power |
|---|---|---|---|
| ADP National Employment Report | Private payroll estimate | 2 days before NFP | Moderate — directional signal, not precise |
| Weekly Jobless Claims | New unemployment filings | Weekly (Thursdays) | Trend signal for labor market health |
| JOLTS (Job Openings) | Job openings, quits, hires | Monthly (~5 week lag) | Leading indicator of labor demand |
| ISM Employment Sub-Indices | Manufacturing + services hiring | Monthly | Sector-level signal |
| Indeed Job Postings | Online job listing volume | Daily (free via FRED) | High-frequency leading indicator |

**Modeling Approach**:
1. Simple regression: NFP surprise ~ ADP surprise + Δ(jobless claims trend) + ISM employment
2. Even a basic model beats consensus ~55% of the time
3. Trade Kalshi brackets when model distribution disagrees with market-implied distribution

**Known Edge**: ADP report 2 days before NFP provides a significant directional signal. Markets often under-react to extreme ADP surprises. NFP has high revision rates (initial estimates revised by 50K+ frequently), creating recurring mispricing patterns.

**Back-testability**: Good. All input data freely available via FRED/BLS with decades of history.

**At $100 Capital**: Same monthly frequency limitation as CPI. Can combine with CPI for ~2 macro events per month.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 7 | Multiple signals but NFP is inherently noisy; ADP correlation is moderate |
| Back-testability | 8 | Full historical data available via FRED/BLS |
| Contract Frequency | 4 | Monthly — 12 events per year |
| Resolution Objectivity | 10 | BLS official release |
| Competition Weakness | 6 | Similar to CPI — informed but anchored participants |
| Fee-Adjusted Edge | 6 | Noisier than CPI; edge per trade is smaller |

**Overall Rating: 6/10** — Decent complementary play to CPI, but noisier signal and same frequency constraint.

---

### 11.4 Economics: GDP

**What's Available**: Quarterly GDP growth rate contracts. Bracket contracts on advance, second, and third estimates. Resolves against BEA release.

**Data Sources**:

| Source | What It Provides | Update Frequency |
|---|---|---|
| Atlanta Fed GDPNow | Real-time GDP tracking estimate | 6-7x per month after data releases |
| NY Fed Staff Nowcast | GDP nowcast for current + next quarter | Weekly (Fridays) |
| BEA Advance/Second/Third Estimates | Official GDP data | Quarterly (staggered) |

**Known Edge**: GDPNow sometimes overshoots dramatically. Documented case: Q4 2025 GDPNow showed 5.3% growth while Kalshi priced 45-52% probability of high growth — the "model lag" trade. Traders who recognized GDPNow's overshoot tendency profited.

**Back-testability**: Good. Atlanta Fed publishes full GDPNow history back to 2011.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 8 | GDPNow is transparent and well-calibrated; NY Fed adds second opinion |
| Back-testability | 7 | GDPNow history from 2011; BEA data from 1947 |
| Contract Frequency | 2 | Quarterly — only 4 primary events per year (more with revision estimates) |
| Resolution Objectivity | 10 | BEA official statistic |
| Competition Weakness | 5 | GDP markets attract more macro-savvy traders |
| Fee-Adjusted Edge | 6 | Edge exists on overshoot/undershoot trades but infrequent |

**Overall Rating: 5/10** — Too infrequent for $100 capital. Useful as an occasional opportunistic add-on, not a primary focus.

---

### 11.5 Federal Reserve Rate Decisions

**What's Available**: Binary contracts on FOMC rate decisions (hold, cut 25bps, cut 50bps, hike). Also "Fed Combo" contracts combining decision + number of dissents. 8 FOMC meetings per year.

**Data Sources**:

| Source | What It Provides | Predictive Power |
|---|---|---|
| CME FedWatch Tool | Probabilities derived from deeply liquid Fed Funds futures ($B+ OI) | Near-definitive for rate decisions |
| FRED Fed Funds data | Historical rate decisions | Context |
| Treasury yield curve | Market expectations embedded in bond prices | Directional signal |
| FOMC dot plots | Individual member projections (released quarterly) | Forward guidance |
| Fed speeches | Chair/Governor commentary | Qualitative signal for timing |

**Known Edge**: Kalshi prices have diverged from CME FedWatch by up to 54 percentage points (CME 90% hold vs Kalshi 64% cut). These divergences reflect Kalshi's less sophisticated participant base relative to institutional futures markets.

**The Problem**: Per the UCD study of 72.1M Kalshi trades, finance/Fed markets are the **most efficient category on Kalshi**. The 54-point divergence case is extreme and rare. Typical divergences are 3-8 cents — which after fees leaves razor-thin margins. Kalshi has maintained a perfect record predicting FOMC outcomes the day before every meeting since 2022, meaning the market is well-informed by settlement time.

**At $100 Capital**: 8 FOMC meetings per year. Capital locked per event. Even with edge, frequency is too low to compound meaningfully.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 10 | CME FedWatch is derived from the deepest derivatives market on Earth |
| Back-testability | 8 | Historical Fed Funds futures data available |
| Contract Frequency | 2 | 8 meetings per year — extremely low |
| Resolution Objectivity | 10 | FOMC press release — zero ambiguity |
| Competition Weakness | 3 | Most efficient category on Kalshi; attracts macro professionals |
| Fee-Adjusted Edge | 4 | 3-8 cent typical divergences vs ~0.4 cent fees — thin but positive |

**Overall Rating: 5/10** — Edge exists via FedWatch arbitrage but frequency is too low and competition too strong for $100 capital. Monitor for extreme divergences only.

---

### 11.6 Crypto Price Targets

**What's Available**: Binary contracts on whether BTC, ETH, DOGE, SHIBA will be above/below specific price levels by specific dates. Both short-term (daily/weekly) and longer-dated contracts.

**Data Sources**:

| Source | What It Provides | Use |
|---|---|---|
| Deribit Options API | Most liquid crypto options chain | Extract implied probability distributions via Breeden-Litzenberger |
| CoinGecko/CoinMarketCap | Free real-time and historical crypto prices | Price feed |
| Funding rates (Binance, Hyperliquid) | Sentiment signal | Directional bias indicator |
| On-chain metrics (Glassnode free tier) | Exchange flows, whale activity | Behavioral signal |

**The Problem**: The people trading crypto price contracts on Kalshi are crypto-native. They follow these markets obsessively. Your options-implied probability might be technically correct, but:
- Crypto price is a random walk at short horizons — no model reliably predicts next-day BTC price
- 15-minute crypto contracts are documented as near-coin-flip with fees. The UCD study confirms this.
- Longer-dated contracts lock up capital with uncertain edge
- Crypto prices are already efficiently priced across dozens of liquid venues

**At $100 Capital**: High fee-drag on frequent short-term trades. Longshot bets ("Will BTC hit $200K this month?") are systematically overpriced (favorite-longshot bias), but fee structure at extreme prices means fees consume 100% of contract cost for sub-7-cent contracts.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 7 | Options-implied distributions are accurate but crypto is a near-random walk |
| Back-testability | 5 | Historical options data available but limited; crypto regime-shifts make backtests unreliable |
| Contract Frequency | 7 | Daily/weekly contracts available |
| Resolution Objectivity | 10 | Price at timestamp — unambiguous |
| Competition Weakness | 3 | Crypto-native participants with strong market opinions |
| Fee-Adjusted Edge | 3 | 15-min contracts are negative EV; longer-dated have uncertain edge |

**Overall Rating: 3/10** — Near-efficient pricing, crypto-native competition, random walk dynamics. Avoid.

---

### 11.7 Stock Market Levels (S&P 500, NASDAQ)

**What's Available**: Contracts on whether S&P 500 / NASDAQ 100 will close above/below specific levels on specific dates. Both daily and weekly brackets.

**Data Sources**:

| Source | What It Provides | Use |
|---|---|---|
| SPX Options Chain (CBOE) | Deepest options market on Earth | Breeden-Litzenberger implied distributions |
| Yahoo Finance / Polygon.io | Free delayed options data | Compute implied probabilities |
| VIX | Implied volatility of S&P 500 | Volatility input for probability models |

**The Problem**: SPX options are priced by the most sophisticated participants in global markets — hedge funds, prop firms, market makers with PhDs in stochastic calculus. The implied probability distribution from SPX options is the single best estimate of future price distribution available anywhere. If Kalshi prices deviate from these, the deviation is small and corrects rapidly.

This is the hardest category to find edge in. You are literally competing against the global derivatives market.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 10 | SPX options = best-calibrated probability source in existence |
| Back-testability | 9 | Decades of options data |
| Contract Frequency | 7 | Daily/weekly contracts |
| Resolution Objectivity | 10 | Closing price — unambiguous |
| Competition Weakness | 1 | Competing against the entire global options market |
| Fee-Adjusted Edge | 2 | Near-zero mispricing; fees consume any residual edge |

**Overall Rating: 4/10** — Theoretically clean but practically impossible to find edge. The "true price" is already known from SPX options. Skip.

---

### 11.8 Politics / Elections

**What's Available**: Binary contracts on election outcomes, policy decisions, government actions, legislative votes. Volume spikes enormously during election cycles.

**Data Sources**:

| Source | What It Provides | Use |
|---|---|---|
| Polling aggregators (538-style) | Aggregated polling averages + models | Baseline probability estimates |
| RealClearPolitics | Polling averages, less modeling | Simple signal |
| State-level polling | Granular electoral college data | Electoral prediction models |
| PredictIt / Polymarket | Cross-platform price signals | Comparative pricing |
| Fundamentals (economy, approval ratings) | Historical correlates of election outcomes | Structural models |

**Edge Pattern**: Markets exhibit recency bias (overreacting to individual polls), anchoring (slow to update on structural shifts), and partisanship (traders bet with their preferred outcome). A disciplined Bayesian updater that weights polls by methodology quality and sample size outperforms the crowd.

**The Problem**: Volume is massive during major election cycles but nearly dead between them. Next US federal election cycle heats up mid-2027. Right now (March 2026), there are some midterm-related markets and policy markets, but liquidity is thin. Also, institutional firms (Susquehanna, Jump) were active on Polymarket elections, and similar sophistication may apply to Kalshi during high-profile races.

**At $100 Capital**: Contracts can take months to resolve (election date is fixed). Capital is locked for the duration. With $100, tying up $20-50 in political contracts for 6 months is deeply inefficient.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 7 | Polling data is good but noisy; models exist but calibration is uncertain |
| Back-testability | 6 | Limited historical prediction market data; each election is somewhat unique |
| Contract Frequency | 2 | Clustered around election cycles; dormant otherwise |
| Resolution Objectivity | 9 | Election results are objective; some policy markets have interpretation risk |
| Competition Weakness | 5 | Mixed — retail sentiment traders + institutional sophistication |
| Fee-Adjusted Edge | 6 | 5-15 cent mispricings on mid-tier races, viable after fees |

**Overall Rating: 6/10 during election cycles, 2/10 off-cycle** — Strong when active, worthless when dormant. Re-evaluate in 2027. For now, only trade opportunistically on high-conviction policy events.

---

### 11.9 Natural Disasters / Hurricanes

**What's Available**: Monthly hurricane count contracts (Atlantic basin), landfall probability contracts during active storms, tornado count contracts. Primarily available June-November (hurricane season).

**Data Sources**:

| Source | What It Provides | Update Frequency | Predictive Power |
|---|---|---|---|
| NOAA National Hurricane Center (NHC) | Probabilistic cone forecasts, ensemble tracks, intensity forecasts | Every 6 hours during active storms | Best-in-class; well-calibrated |
| NOAA Storm Prediction Center | Tornado/severe weather outlooks | Daily | Categorical probabilities for severe weather |
| Colorado State Univ. Tropical Forecast | Seasonal hurricane count predictions | Updated monthly during season | Good baseline; well-cited |
| ECMWF Ensemble | 51-member tropical cyclone tracks | 2x daily | Often superior to GFS for track |
| NOAA Historical Hurricane Tracks (HURDAT2) | Complete Atlantic hurricane history since 1851 | Static archive | Base rate computation |

**Edge Pattern**: The public systematically overestimates hurricane risk during active storms (fear premium) and underestimates during quiet periods. NHC cone forecasts are well-calibrated but widely misinterpreted — the cone shows the probable track of the center, not the extent of damaging winds. Markets price in fear, not probabilities.

**Modeling Approach**:
1. Extract NHC/ECMWF ensemble track probabilities for landfall location
2. Compare ensemble-derived probability to Kalshi market price
3. For monthly counts: use Colorado State seasonal forecast + ENSO state + sea surface temperature anomalies as inputs

**Back-testability**: Strong. HURDAT2 provides complete Atlantic hurricane data since 1851. NHC forecast verification statistics are published annually. Decades of ensemble forecast archives exist.

**The Catch**: Purely seasonal. Zero contracts December-May. During season, opportunities can be sporadic (only during active tropical development).

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 9 | NHC ensemble tracks are institutional-grade; freely available |
| Back-testability | 8 | 170+ years of hurricane records; decades of forecast verification |
| Contract Frequency | 3 | Only June-November; sporadic within season |
| Resolution Objectivity | 9 | NHC official advisory data; some interpretation on "landfall" boundaries |
| Competition Weakness | 8 | Fear-driven pricing; public misinterprets NHC cone |
| Fee-Adjusted Edge | 7 | Fear premium creates 10-20 cent mispricings during active storms |

**Overall Rating: 7/10 during hurricane season, 0/10 off-season** — Excellent edge when available, using the exact same NOAA modeling skills as temperature trading. Treat as a seasonal amplifier for weather expertise.

---

### 11.10 Entertainment / Awards

**What's Available**: Oscar/Emmy/Grammy predictions, box office performance, Netflix weekly charts, TV ratings, and similar cultural events.

**Data Sources**:

| Source | What It Provides | Availability |
|---|---|---|
| Box Office Mojo | Box office tracking, historical comps | Free, updated daily |
| Netflix Top 10 | Weekly global/country-level viewing hours | Free, published Tuesdays |
| Gold Derby | Awards prediction aggregator (expert + fan polls) | Free |
| Metacritic / Rotten Tomatoes | Critical reception scores | Free |
| Social media metrics | Trending topics, search volume | Google Trends (free) |
| Betting odds (PredictIt, offshore) | Cross-platform probability signals | Varies |

**Edge Pattern**: Per the UCD 72.1M trade analysis, entertainment markets are the **least efficient category on Kalshi**. Participants trade on fandom, personal preference, and recency bias rather than systematic analysis. This creates the largest calibration errors of any category.

**The Problem**: Events are infrequent and often one-off. Hard to build systematic models with limited history (each Oscar ceremony is unique). Requires domain research per event rather than a single reusable model. Netflix chart contracts are more systematic but volume is thin.

**Back-testability**: Weak. Each event is somewhat unique. Historical awards data exists but sample sizes are small and category definitions change.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 5 | Qualitative data; no calibrated ensemble equivalent for entertainment |
| Back-testability | 3 | Small samples, unique events, changing categories |
| Contract Frequency | 4 | Clustered around awards seasons; Netflix charts are weekly |
| Resolution Objectivity | 8 | Award winners are objective; some interpretation on streaming metrics |
| Competition Weakness | 10 | Least efficient category on Kalshi per 72.1M trade study |
| Fee-Adjusted Edge | 7 | Large mispricings (10-30 cents) due to sentiment-driven pricing |

**Overall Rating: 6/10** — Highest mispricing rates of any category, but hard to systematize and automate. Best for occasional high-conviction manual bets when you spot obvious crowd errors during awards season.

---

### 11.11 Sports

**What's Available**: NBA, NFL, MLB, NHL, golf, tennis, soccer game outcomes and props. High frequency during seasons.

**Data Sources**:

| Source | What It Provides | Use |
|---|---|---|
| Pinnacle/Betfair | Sharpest betting odds in the world | Reference probability |
| ESPN/sports-reference.com | Historical stats, Elo ratings | Model inputs |
| Odds API | Aggregated odds across sportsbooks | Free tier available |

**The Problem**: Sports betting is the most mature prediction market in existence. Sportsbooks have been pricing games for decades with massive data science teams. Pinnacle's closing lines are among the most efficient probability estimates known. If Kalshi deviates from Pinnacle, the deviation is:
- Small (1-3 cents typically)
- Consumed by Kalshi's fees (which are higher than sportsbook vig for most bets)
- Quickly corrected by cross-market arbitrageurs

You are not going to out-model the sportsbook industry. The best you can do is arb Kalshi vs sportsbooks when Kalshi is stale, but this requires accounts on both platforms and fast execution.

**Void Rule Risk**: Documented $30K loss case where a player didn't participate and the market settled at "last traded fair price" instead of voiding. This is a unique Kalshi risk that doesn't exist at traditional sportsbooks.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 9 | Pinnacle odds are extremely well-calibrated |
| Back-testability | 9 | Decades of sports data and betting market history |
| Contract Frequency | 9 | Multiple games daily during season |
| Resolution Objectivity | 7 | Game outcomes are objective; void rules create operational risk |
| Competition Weakness | 2 | Competing against the entire sportsbook industry |
| Fee-Adjusted Edge | 2 | Kalshi fees exceed edge vs sharp sportsbook lines |

**Overall Rating: 3/10** — Mature competition, adverse fee comparison vs sportsbooks, void rule risk. The sports betting industry solved this problem decades ago. Don't compete with them on Kalshi.

---

### 11.12 Company / Tech Events

**What's Available**: Earnings beat/miss contracts, product launch timing, CEO departures, regulatory approvals. Sporadic and event-driven.

**Data Sources**:

| Source | What It Provides | Use |
|---|---|---|
| Yahoo Finance | Earnings estimates, analyst consensus | Baseline expectations |
| SEC EDGAR filings | Regulatory filings, insider transactions | Leading indicators |
| OptionMetrics / free options data | Implied move from options straddle | Expected earnings move magnitude |

**The Problem**: Information asymmetry. Employees, analysts, industry insiders, and institutional investors know more about specific companies than any public data model can capture. A model using public consensus estimates provides no edge — the edge on company events comes from knowing something the market doesn't, and public data rarely provides that.

Earnings surprises are also partially priced into options-implied moves, so the "correct" probability is often already known from the options market.

| Dimension | Rating | Notes |
|---|---|---|
| Data Quality | 6 | Consensus estimates available but edge requires non-public insight |
| Back-testability | 5 | Historical earnings data exists but each company/quarter is unique |
| Contract Frequency | 4 | Quarterly earnings; sporadic product launches |
| Resolution Objectivity | 8 | Earnings numbers are objective; product launch timing can be ambiguous |
| Competition Weakness | 3 | Industry analysts and institutional investors are better informed |
| Fee-Adjusted Edge | 4 | Public-data models provide marginal edge at best |

**Overall Rating: 4/10** — Information disadvantage vs insiders. Not systematizable from public data alone. Skip unless you have genuine domain expertise in a specific industry.

---

### 11.13 Summary: Category Rankings

| Rank | Category | Overall Rating | Capital Turnover | Systematizable? | Recommended Allocation |
|---|---|---|---|---|---|
| **1** | Weather / Temperature | **9/10** | Daily | Fully automated | 50-60% of capital |
| **2** | Economics: CPI | **7/10** | Monthly | Fully automated | 15-20% of capital |
| **3** | Hurricanes (seasonal) | **7/10** | Episodic (Jun-Nov) | Fully automated | 10-15% when active |
| **4** | Entertainment / Awards | **6/10** | Episodic | Manual / semi-auto | Opportunistic only |
| **5** | Economics: NFP | **6/10** | Monthly | Fully automated | 5-10% of capital |
| **5** | Politics (cycle-dependent) | **6/10** (peak) | Monthly-Quarterly | Semi-automated | Opportunistic only |
| **7** | Fed Rate Decisions | **5/10** | 8x per year | Fully automated | Opportunistic only |
| **7** | Economics: GDP | **5/10** | Quarterly | Fully automated | Opportunistic only |
| **9** | Stock Market Levels | **4/10** | Daily-Weekly | Fully automated | Avoid |
| **9** | Company Events | **4/10** | Quarterly | Not systematizable | Avoid |
| **11** | Crypto Price | **3/10** | Daily | Partially automated | Avoid |
| **11** | Sports | **3/10** | Daily | Fully automated | Avoid |

### Capital Allocation Strategy at $100

**Phase 1: Weather Focus (Months 1-3)**
- Deploy 100% of active capital on weather/temperature contracts
- Start with 1-2 cities (NYC + Chicago — most liquid, best data coverage)
- Target 3-5 positions per day at $3-8 each
- Maintain $20 cash reserve
- Track every prediction for Brier score calibration
- Goal: validate model accuracy, achieve Brier score < 0.20

**Phase 2: Add Economics (Months 3-6)**
- Weather remains primary (60-70% of capital)
- Add CPI bracket contracts monthly (15-20%)
- Add NFP contracts monthly (10-15%)
- Capital should have grown to $120-$200 if weather model is calibrated

**Phase 3: Seasonal Expansion (Month 6+)**
- During hurricane season (Jun-Nov): allocate 10-15% to hurricane contracts using same NOAA modeling skills
- Opportunistic entertainment/political bets when obvious mispricings appear
- Scale capital as confidence and track record grow

### Key Insight: Skill Transfer

The core modeling skill — **calibrating ensemble probabilistic forecasts against market-implied probabilities** — transfers directly across weather, hurricanes, and (conceptually) to economic nowcasting. Building deep expertise in NOAA ensemble interpretation creates a portable edge that applies to 3 of the top 5 categories. This is the skill worth investing in.

---

## Appendix A: Key Formulas Reference

### Fee Calculation
```
Taker fee = ceil(0.07 * contracts * price * (1 - price))
Maker fee = ceil(0.0175 * contracts * price * (1 - price))
```

### Expected Value
```
EV = true_probability * $1.00 - market_price - fee
```

### Kelly Criterion (Binary)
```
f* = (b * p - q) / b
where b = (1 - price) / price, p = true_prob, q = 1 - p
```

### Fractional Kelly
```
bet_size = fraction * f* * bankroll
Recommended fraction: 0.25 for conservative, 0.50 for moderate
```

### Brier Score
```
BS = (1/N) * sum((forecast_i - outcome_i)^2)
```

### Market Making Spread
```
min_profitable_spread = 2 * maker_fee + adverse_selection_cost
```

### Arbitrage Profit
```
profit = $1.00 - (price_A_yes + price_B_no) - fees_A - fees_B - slippage
```

### Avellaneda-Stoikov for Binary Contracts
```
reservation_price = mid_price - inventory * gamma * sigma^2
optimal_spread = gamma * sigma^2 + (2/gamma) * ln(1 + gamma/k)
sigma = sqrt(P * (1 - P))  // binary contract volatility
```

---

## Appendix B: Weather Market Ticker Codes

| City | Ticker Series | NWS Station |
|---|---|---|
| New York City | KXHIGHNY | KNYC |
| Chicago | KXHIGHCHI | KORD |
| Miami | KXHIGHMIA | KMIA |
| Los Angeles | KXHIGHLAX | KLAX |
| Denver | KXHIGHDEN | KDEN |
| Austin | KXHIGHAUS | KAUS |

---

## Appendix C: Recommended Starting Path

1. **Week 1-2**: Create Kalshi account, deposit $200-500, explore the demo API environment
2. **Week 2-4**: Pick ONE market category (weather or CPI recommended for beginners). Study the data sources deeply
3. **Month 2**: Start paper trading on demo. Track every trade with probability estimate and outcome
4. **Month 3**: Calculate your Brier score. If < 0.22, consider live trading with small sizes
5. **Month 3-6**: Trade live with 0.25x Kelly sizing. Focus on maker orders only. Track P&L rigorously
6. **Month 6+**: If profitable, consider scaling capital and adding markets. Build automation

---

## Sources

### Academic Papers
- [Makers and Takers: The Economics of the Kalshi Prediction Market (Karl Whelan, UCD)](https://www.karlwhelan.com/Papers/Kalshi.pdf)
- [Kalshi and the Rise of Macro Markets (NBER/Federal Reserve)](https://www.nber.org/system/files/working_papers/w34702/w34702.pdf)
- [Application of the Kelly Criterion to Prediction Markets (arXiv)](https://arxiv.org/html/2412.14144v1)
- [Kelly Betting as Bayesian Model Evaluation (arXiv)](https://arxiv.org/html/2602.09982)
- [The Economics of the Kalshi Prediction Market (CEPR)](https://cepr.org/voxeu/columns/economics-kalshi-prediction-market)

### Kalshi Official
- [Kalshi Fee Schedule (Feb 2026 PDF)](https://kalshi.com/docs/kalshi-fee-schedule.pdf)
- [Kalshi API Documentation](https://docs.kalshi.com)
- [Kalshi API Rate Limits](https://docs.kalshi.com/getting_started/rate_limits)
- [Kalshi Market Maker Program](https://help.kalshi.com/en/articles/13823819-market-maker-program)
- [Kalshi Weather Markets](https://help.kalshi.com/markets/popular-markets/weather-markets)
- [Kalshi Order Types](https://news.kalshi.com/p/order-types)
- [Kalshi Market Lifecycle](https://news.kalshi.com/p/what-is-the-market-life-cycle)
- [Kalshi Liquidity Incentive Program](https://help.kalshi.com/incentive-programs/liquidity-incentive-program)
- [Kalshi CPI Markets](https://kalshi.com/category/economics/inflation)
- [Kalshi Fed Markets](https://kalshi.com/category/economics/fed)

### Strategy and Analysis
- [The Math of Prediction Markets (Substack)](https://navnoorbawa.substack.com/p/the-math-of-prediction-markets-binary)
- [Automated Market Making on Kalshi (Substack)](https://jdsemrau.substack.com/p/automated-market-making-on-kalshi)
- [Maker/Taker Math on Kalshi (Substack)](https://whirligigbear.substack.com/p/makertaker-math-on-kalshi)
- [How Prediction Market Arbitrage Works](https://www.trevorlasn.com/blog/how-prediction-market-polymarket-kalshi-arbitrage-works)
- [Market Making on Prediction Markets: Complete 2026 Guide](https://newyorkcityservers.com/blog/prediction-market-making-guide)
- [Cross-Platform Arbitrage Strategies (AhaSignals)](https://ahasignals.com/research/prediction-market-arbitrage-strategies/)
- [Kalshi API Python Tutorial (Alphascope)](https://www.alphascope.app/blog/kalshi-api-python)
- [Kalshi API Guide (AgentBets)](https://agentbets.ai/guides/kalshi-api-guide/)
- [Beginner Trading Strategies (SportsGrid)](https://www.sportsgrid.com/prediction-market/beginner-trading-strategies)
- [Kalshi Void Rules Loss ($30K)](https://fiftycentdollars.substack.com/p/i-lost-30k-due-to-kalshis-void-rules)

### Data Sources
- [FRED API](https://fred.stlouisfed.org/docs/api/fred/)
- [Cleveland Fed Inflation Nowcasting](https://www.clevelandfed.org/indicators-and-data/inflation-nowcasting)
- [Atlanta Fed GDPNow](https://www.atlantafed.org/research-and-data/data/gdpnow)
- [CME FedWatch Tool](https://www.cmegroup.com/markets/interest-rates/cme-fedwatch-tool.html)
- [BLS CPI Data](https://www.bls.gov/cpi/)
- [BLS CPI Component Tables](https://www.bls.gov/news.release/cpi.t01.htm)
- [USDA Food Price Outlook](https://www.ers.usda.gov/data-products/food-price-outlook)
- [NOAA GEFS Ensemble](https://www.ncei.noaa.gov/products/weather-climate-models/global-ensemble-forecast)
- [Open-Meteo (Free Weather API)](https://open-meteo.com)
- [NWS API](https://api.weather.gov)
- [Wethr.net (Station Forecasts)](https://wethr.net)
- [Ventusky (Multi-Model Weather)](https://ventusky.com)
- [Betting on Weather (Kalshi Guide)](https://www.bettingonweather.com/pages/kalshi.php)

### Tools and Reference Implementations
- [Dune Analytics Arbitrage Scanner](https://dune.com/the_liolik/99c)
- [DeFi Rate Calculators](https://defirate.com/prediction-markets/calculators/)
- [Weather Trading Bot (GitHub)](https://github.com/suislanchez/polymarket-kalshi-weather-bot)
- [Kalshi Market Making Project (GitHub)](https://github.com/nikhilnd/kalshi-market-making)
- [BTC Arbitrage Bot (GitHub)](https://github.com/CarlosIbCu/polymarket-kalshi-btc-arbitrage-bot)
- [Kalshi Deep Trading Bot (GitHub)](https://github.com/OctagonAI/kalshi-deep-trading-bot)
- [Brier Score Tracker](https://brier.fyi/)
- [Polymarket Accuracy Analysis (Fensory)](https://www.fensory.com/intelligence/predict/polymarket-accuracy-analysis-track-record-2026)
- [Prediction Hunt Fee Calculator](https://predictionhunt.com/calculator)
