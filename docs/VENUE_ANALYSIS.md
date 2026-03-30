# Venue & Strategy Analysis: Where Should a Solo Retail Quant Trade?

## Purpose

This document provides a comprehensive, data-driven comparison of trading venues and strategy types for a solo retail developer building a C++20 automated trading system. The goal is to identify the venue/strategy combination that maximizes expected profit given the constraints of a single developer operating from a home connection.

The analysis is structured so another agent or analyst can read this document and make a well-informed recommendation.

**Data sources**: Combination of live API data (March 2026), exchange documentation, web research, and quantitative analysis. Specific sources cited throughout.

---

## 1. Operator Profile & Constraints

Understanding these constraints is critical — they define what venues and strategies are realistic.

| Constraint | Detail |
|-----------|--------|
| **Team size** | 1 developer (solo) |
| **Capital** | Assumed $5K-$50K starting (retail scale) |
| **Latency** | Home internet, ~100-250ms to Tokyo/Singapore exchanges |
| **Co-location** | None initially; cloud VPS possible ($50-150/mo) |
| **Tech stack** | C++20, capable of building low-latency systems |
| **Trading experience** | New to trading/crypto, strong engineering background |
| **Time horizon** | Building a system that can run autonomously 24/7 |
| **Risk tolerance** | Must preserve capital; learning while earning |
| **Regulatory** | Retail individual, no broker-dealer license |

### What These Constraints Mean

- **Latency-sensitive strategies are disadvantaged**: Any venue where co-located firms compete directly will adversely select a home-connection trader on correlated moves. The latency gap must be irrelevant to the edge.
- **Complexity budget is limited**: A solo dev can maintain ~1-2 exchange integrations and ~2-3 strategies. The system that ships beats the perfect system that doesn't.
- **Capital efficiency matters**: With limited capital, strategies that require large collateral (e.g., delta-neutral funding arb with full spot + perp positions) consume capital that could be deployed elsewhere.

---

## 2. Venue Comparison Matrix

### 2.1 Summary Scorecard

Each venue is rated 1-5 on factors critical to a solo retail market maker. Higher = better.

| Factor | Binance Altcoin Spot | Hyperliquid Perps | dYdX v4 | Polymarket | Kalshi | Deribit Options | CEX-DEX Arb |
|--------|---------------------|-------------------|---------|------------|--------|-----------------|-------------|
| **Spread width (edge size)** | 2 | 3 | 4 | 5 | 4 | 4 | 3 |
| **Competition sophistication** | 2 | 3 | 4 | 5 | 4 | 3 | 2 |
| **Latency sensitivity** | 2 | 4 | 4 | 5 | 5 | 4 | 2 |
| **API quality** | 5 | 4 | 4 | 3 | 4 | 5 | N/A |
| **Liquidity / volume** | 5 | 5 | 2 | 2 | 2 | 3 | varies |
| **Platform risk** | 4 | 3 | 3 | 3 | 5 | 4 | 2 |
| **Capital efficiency** | 3 | 4 | 4 | 4 | 4 | 3 | 2 |
| **Implementation complexity** | 3 | 3 | 3 | 3 | 4 | 1 | 1 |
| **Strategy portability** | 3 | 4 | 4 | 2 | 2 | 2 | 2 |
| **Regulatory clarity** | 3 | 2 | 2 | 3 | 5 | 3 | 1 |
| **TOTAL** | **32** | **35** | **34** | **35** | **39** | **32** | **17** |

> **Note**: The scorecard is a starting framework. The detailed analysis below provides the nuance that the numbers cannot capture. Some factors matter more than others depending on priorities.

---

## 3. Detailed Venue Analysis

### 3.1 Binance Altcoin Spot Market Making (Current Plan)

#### The Thesis
Altcoin pairs on Binance have wider spreads (10-40 bps) than BTC (1-3 bps), making them viable for retail market makers using Avellaneda-Stoikov with OBI and BTC-lead signals.

#### Strengths
- **Best API in crypto**: Mature REST + WebSocket APIs, well-documented, testnet available
- **Deepest liquidity**: Highest volume exchange globally ($10-30B+/day); fills are reliable
- **Proven strategy math**: Avellaneda-Stoikov is well-studied on CEX orderbooks
- **Funding rate arb ports directly**: Spot + futures on same platform, same API keys

#### Weaknesses — Competition Reality

**Major market makers confirmed active on Binance altcoins:**
- **Wintermute**: Designated MM for hundreds of pairs, has market-making agreements with token projects
- **GSR**: Established MM, active via OTC + exchange
- **DWF Labs**: Very active on mid-cap altcoin listings
- **Amber Group**: Hong Kong-based, active on Asian exchanges
- **Auros, CMS Holdings, Folkvang, B2C2**: Smaller but active
- **Binance internal MMs**: Widely believed (unconfirmed) to operate internal market-making desks

**For any Binance-listed altcoin, the token project typically has a market-making agreement with 1-3 professional firms.** Even "low liquidity" altcoins have at least one professional MM quoting.

**Fee structure punishes small traders:**
| VIP Level | 30d Volume | Maker Fee | Taker Fee |
|-----------|-----------|-----------|-----------|
| Regular | < $1M | 0.10% (10 bps) | 0.10% |
| VIP 1 | >= $1M | 0.09% | 0.10% |
| VIP 2 | >= $5M | 0.08% | 0.10% |
| VIP 3 | >= $10M | 0.07% | 0.08% |
| VIP 9 | >= $4B | 0.02% | 0.04% |

With BNB discount: ~7.5 bps maker. **Professional MMs get negotiated rates (often 0 bps maker, sometimes -0.5 to -2 bps rebate).** This creates a 10-15 bps fee disadvantage for retail.

**Spread compression over time**: Altcoin spreads have roughly halved from 2022 to 2025:
- AVAX/USDT: 3-10 bps (was 10-25 bps in 2022)
- ARB/USDT: 5-15 bps (compressed since launch)
- NEAR/USDT: 5-15 bps
- SUI/USDT: 5-15 bps (compressed quickly as volume grew)

**Latency disadvantage is structural:**
- Binance matching engine: AWS Tokyo (ap-northeast-1)
- Co-located institutional: <1ms round-trip
- Home connection from US: 100-250ms
- **100-250x latency disadvantage** — your stale quotes get picked off on every correlated move

#### Realistic Edge Assessment
- **Expected spread capture**: 5-15 bps on mid-cap altcoins
- **Round-trip fees**: 15 bps (base) or 12 bps (with BNB)
- **Net edge**: 0-3 bps before adverse selection
- **After adverse selection**: Likely **negative** from home connection
- **Verdict**: Marginal to negative EV for retail at current spread levels

---

### 3.2 Hyperliquid Perpetual Market Making

#### Platform Overview
Custom L1 blockchain purpose-built for perpetual futures trading. Fully on-chain CLOB with sub-second finality. The most CEX-like DEX.

- **Volume**: $5-15B/day, ~44% of all DEX perp volume (dominant venue)
- **Open interest**: $6-9.5B
- **Markets**: 311+ perps including crypto, commodities, equity perps
- **Architecture**: Custom L1 (HyperBFT consensus), fully on-chain orderbook
- **Block time**: ~200ms-1s

#### API Details
- **REST**: POST to `https://api.hyperliquid.xyz/info` (reads) and `/exchange` (writes)
- **WebSocket**: `wss://api.hyperliquid.xyz/ws` — real-time L2 book, trades, user events
- **gRPC**: 9 streams including L2/L4 book data with compression (performance option)
- **Authentication**: EVM wallet signatures (EIP-712 typed data), no API key registration — permissionless
- **Testnet**: `https://api.hyperliquid-testnet.xyz`
- **SDKs**: Python (official), Rust (official), TypeScript/Go/Elixir (community). No C++ SDK — implement REST/WS directly.

**Order Types:**
- **ALO (Add Liquidity Only / Post-Only)**: Canceled if it would immediately match
- Limit (GTC), IOC, Market, Stop, Scale, TWAP, Reduce-Only
- **Critical MM feature**: ALO and cancel orders are **prioritized above GTC and IOC** in the matching engine — structural advantage for market makers
- **batchModify**: Atomic cancel-replace for multiple orders in one request — essential for requoting

**Rate Limits:**
- IP-based: 1,200 weight/minute (exchange actions: 1 + floor(batch_size/40) weight)
- Address-based: 1 request per 1 USDC of cumulative all-time volume (10,000 initial buffer)
- Cancel allowance is more generous: min(limit + 100,000, limit × 2)
- Max open orders: 1,000 base + 1 per 5M USDC volume (cap 5,000)
- WebSocket: 10 connections, 1,000 subscriptions, 2,000 messages/min

#### Fee Structure (CORRECTED — not maker rebate at base tier)

| Tier | 14d Volume | Taker | Maker |
|------|-----------|-------|-------|
| Base | < $5M | 4.5 bps | **1.5 bps** |
| 1 | >= $5M | 4.0 bps | 1.2 bps |
| 2 | >= $25M | 3.5 bps | 0.8 bps |
| 3 | >= $100M | 3.0 bps | 0.4 bps |
| 4 | >= $500M | 2.8 bps | **0 bps** |
| 5 | >= $2B | 2.6 bps | **0 bps** |
| 6 | >= $7B | 2.4 bps | **0 bps** |

**Maker rebate program** (requires % of total exchange maker volume):
- 0.5% of maker volume: -0.1 bps rebate
- 1.5%: -0.2 bps rebate
- 3.0%: -0.3 bps rebate

**HYPE staking**: 10+ HYPE = 5% discount, up to 500K+ HYPE = 40% discount.

**Assessment**: Base maker fee of 1.5 bps is real cost. Better than Binance's 7.5-10 bps, but NOT the maker rebate some sources claim. You need $500M+ 14-day volume for zero maker fees — unrealistic for solo retail.

#### Competition Analysis
- **HLP (Hyperliquid Liquidity Provider) vault**: The primary competitor. Run by Hyperliquid team with **Jane Street alumni**. ~$380M TVL. Quotes across all pairs 24/7 with proprietary algorithms. This is sophisticated, well-funded competition.
- **External MMs**: Growing but less numerous than Binance. Mix of quant funds and retail/semi-pro bots.
- **Barrier to entry**: Low (permissionless, no KYC for trading) — means competition will increase over time.
- **Where solo dev can compete**: Less liquid mid/small-cap perps where HLP may quote wider or with less depth.

#### Latency Characteristics
- **Validators**: AWS Tokyo (ap-northeast-1)
- From Tokyo: ~2-3ms RTT
- From US: ~150-200ms RTT
- **On-chain confirmation**: median ~200ms, p99 ~900ms
- **Key advantage**: ALO/cancel priority in matching engine means your maker orders are structurally protected
- **Advanced optimization**: Run non-validating node locally, build books from node output (requires 32+ cores, 500 MB/s disk)

#### Funding Rate Mechanics
- **Two components**: Fixed interest rate (0.01% per 8h) + premium component (perp vs oracle deviation)
- **Settlement**: Every 1 hour at 1/8 of 8h rate (more granular than CEX 8h settlements)
- **Cap**: 4% per hour
- **Peer-to-peer**: No protocol fees on funding
- **Important**: Limited spot pairs on Hyperliquid — delta-neutral hedging may require a CEX for the spot leg

#### Platform Risks
- **JELLY Incident (March 2025)**: Whale manipulated illiquid JELLY token, pumping 429%. HLP lost ~$12-15M. Validators reached consensus in ~2 minutes to delist JELLY and make attacker's position worthless. Exposed **centralization risk** — team can unilaterally delist and force-settle.
- **Bridge risk**: Funds enter via Arbitrum bridge (USDC). Only 4 validators at time of DPRK scare. Compromising 3/4 could drain bridge. Mitigation: Circle could freeze USDC.
- **DPRK hacker scare (late 2024)**: DPRK-linked addresses detected trading (lost $700K). Raised bridge attack concerns but no breach.
- **US access**: Officially blocked (ToS prohibition, no KYC). Using from US violates ToS and carries legal risk.
- **Smart contract risk**: Custom L1, less battle-tested than Ethereum. HyperEVM projects have had exploits ($782K loss).

#### Edge Assessment for Solo Retail
- **Pros**: ALO priority, batch modify, growing volume, less competition than Binance on mid-caps
- **Cons**: 1.5 bps base maker fee eats into edge, HLP is sophisticated competition, platform risk (JELLY), US access concerns
- **Verdict**: Viable but not the slam-dunk some suggest. Edge exists on mid/long-tail pairs but is more modest than commonly claimed.

---

### 3.3 dYdX v4

#### Platform Overview
Sovereign Cosmos SDK appchain with **off-chain orderbook on validators** and on-chain settlement. Block time ~1.5s.

- **Volume**: $200-500M/day (distant second to Hyperliquid)
- **Markets**: 200+ perpetual pairs
- **Lifetime volume**: $1.5T+ across all versions

#### API Details
- **Indexer REST**: HTTP queries for markets, orderbooks, positions, fills. Rate limit: 100 req/10s per IP.
- **Indexer WebSocket**: Real-time L2 book, trades, accounts. 32 channel limit for orderbook/trades.
- **Order placement**: Submit transactions directly to chain via gRPC/REST/Tendermint RPC.
- **Short-term orders** (MM use case): Stay in-memory up to 20 blocks (~30s), high throughput, no chain storage. Rate limit: **533/second** — very generous.
- **Order types**: Market, Limit, Stop, TWAP. Time-in-force: IOC, FOK, Good-Till-Block, Good-Till-Block-Time.
- **Client libraries**: TypeScript, Python.

#### Fee Structure

| Tier | 30d Volume | Taker | Maker |
|------|-----------|-------|-------|
| 1 | < $1M | 5.0 bps | **1.0 bps** |
| 2 | >= $1M | 4.5 bps | 1.0 bps |
| 3 | >= $5M | 4.0 bps | 0.5 bps |
| 4 | >= $25M | 3.5 bps | **0 bps** |
| 5 | >= $50M | 3.0 bps | **0 bps** |
| 6 | >= $100M | 2.5 bps | **-0.7 bps** |
| 7 | >= $200M | 2.5 bps | **-1.1 bps** |

**BTC-USD and SOL-USD are currently fee-free** (both maker and taker) — promotional.
**50% fee rebate program** on positive fees. Staking discounts up to 50%.

**Base maker fee (1.0 bps) is lower than Hyperliquid (1.5 bps).** This matters when spreads are thin.

#### LIVE SPREAD DATA (March 30, 2026 — captured from dYdX indexer)

| Pair | Best Bid | Best Ask | Spread (bps) |
|------|----------|----------|-------------|
| BTC-USD | $66,868 | $66,869 | **0.1 bps** |
| ETH-USD | $2,040.50 | $2,040.60 | **0.5 bps** |
| SOL-USD | $83.27 | $83.28 | **1.2 bps** |
| LINK-USD | $8.712 | $8.724 | **13.8 bps** |
| AVAX-USD | $8.850 | $8.867 | **19.2 bps** |
| NEAR-USD | $1.167 | $1.170 | **25.7 bps** |
| ARB-USD | $0.0912 | $0.0914 | **25.2 bps** |

**This is the most important data in this document.** The altcoin perps on dYdX show 14-26 bps spreads — wide enough for profitable market making even with 1 bps maker fees. These are the exact same altcoins targeted in the current Binance plan, but with **far less competition and far better fee economics**.

#### Competition
- **MegaVault**: Primary liquidity provider — automated MM vault with ~$70-80M TVL. Runs generic strategy across all markets. A specialized maker with alpha signals could outperform on specific pairs.
- **Institutional MMs**: dYdX foundation recruits them, but adoption is limited.
- **Wide spreads + thin books = competition is thin.** The 14-26 bps spreads on altcoins confirm this.

#### Latency
- Block time ~1.5s
- Short-term orders processed in-memory by validators — effective latency depends on network propagation
- API performance improved ~98% in 2025 infrastructure upgrades
- **Not competitive with CEX latency, but adequate for wide-spread altcoin MM**

#### Platform Risks
- Cosmos ecosystem security depends on validator set
- Lower volume means fewer fills per unit time
- Token incentives may inflate current volume — organic baseline unclear
- Similar regulatory uncertainty to Hyperliquid

#### Edge Assessment
- **14-26 bps spreads on target altcoins with 1 bps maker fee = 13-25 bps of gross edge**
- **Lower competition than both Binance and Hyperliquid**
- **Risk**: Low volume means potentially few fills per day
- **Verdict**: **Strongest pure edge for the Avellaneda-Stoikov strategy among all venues analyzed**

---

### 3.4 Emerging DEX Venues: Lighter & Paradex

Two newer venues deserve attention due to **zero maker fees**:

#### Lighter (Arbitrum ZK-rollup)
- **Architecture**: Orderbook-based with ZK proof verification
- **Fees**: **Zero** — no maker or taker fees
- **Volume**: $3.75-4.58B/day, OI: $1.53B
- **Latency**: Sub-150ms for premium users
- **Launch**: TGE December 2025 — very new platform
- **Risk**: Volume may be inflated by airdrop farming. Platform maturity is untested.
- **Assessment**: Zero fees are extremely attractive — all spread capture is pure profit. But newness = high platform risk and potentially inflated metrics.

#### Paradex (Starknet appchain)
- **Architecture**: Encrypted orderbook with on-chain settlement
- **Fees**: **Zero**
- **Volume**: ~$1.47B/day, OI: $796M
- **Markets**: 600+
- **Unique feature**: Encrypted orderbook provides privacy — competitors can't see your quoting patterns
- **Assessment**: Zero fees + 600 markets + encrypted book = interesting for a systematic maker. But very new and unproven.

**Both venues are speculative options.** Zero fees create excellent unit economics, but platform risk is significantly higher than established venues. Worth monitoring but not recommended as primary venue.

---

### 3.5 Polymarket (Prediction Market)

#### Platform Overview
Dominant crypto-native prediction market on Polygon. CLOB with off-chain matching, on-chain settlement.

- **Peak volume**: >$300M/day (2024 US election)
- **Non-event volume**: $5-50M/day (dropped 70-90% from peak)
- **Contract type**: Binary outcome shares priced $0.00-$1.00

#### API & Fees
- **REST/WebSocket**: CLOB API at `clob.polymarket.com`
- **Auth**: API key + Polygon wallet ECDSA signing
- **Maker fee**: 0% (free)
- **Taker fee**: ~2% of potential profit

#### Spreads & Competition
- **High-profile markets**: 1-3 cent spreads ($100-300 bps)
- **Mid-tier markets**: 3-10 cent spreads
- **Long-tail markets**: 10-30 cent spreads (5-30% implied)
- **Competition**: Firms like Susquehanna and Jump were active on elections. **Long-tail markets have thin, unsophisticated competition** — this is the realistic opportunity.
- **Post-election drop**: Volume dropped dramatically. Platform diversifying into sports, crypto prices, geopolitical events.

#### What Edge Looks Like (Different from Crypto Perps)
| Dimension | Crypto Perps | Prediction Markets |
|-----------|-------------|-------------------|
| Spread capture | 1-25 bps | 100-3000 bps |
| Volume per market | Very high | Very low |
| Latency sensitivity | Moderate-high | Low (seconds/minutes) |
| Inventory risk | Unbounded | Bounded ($0-$1) |
| Hedging | Delta hedge | Cross-market only |
| Number of markets | Dozens of pairs | Hundreds to thousands |
| Edge source | Speed + signals | Probability estimation |

**Key difference**: Edge comes from **better probability models** (Bayesian inference, polling aggregation, economic models), not speed. A portfolio of 30-100 thin markets generates consistent returns.

#### Risks
- **US access**: Blocked (CFTC settlement 2022, geo-restricted)
- **Volume dependency**: Drops dramatically between major events
- **Resolution risk**: Ambiguous market resolution criteria
- **Strategy portability**: Low (~35% code reuse from current plan)

#### Profit Estimate
- Portfolio approach on long-tail markets with $25K capital: **$300-$1,500/month**
- During high-volume events: 2-5x baseline
- Annualized: 14-72% return (wide range due to event dependency)

---

### 3.6 Kalshi

#### Platform Overview
**CFTC-regulated** Designated Contract Market (DCM) — the only fully legal US prediction market exchange.

- **Settlement**: USD (real US dollars, not crypto)
- **Volume**: $2-10M/day (growing, lower than Polymarket)
- **Markets**: Hundreds across economics, weather, politics, finance, culture
- **Regulatory**: Won landmark court case vs CFTC (2023) on political event contracts

#### API & Fees
- **REST/WebSocket**: Well-documented at `trading-api.readme.io`
- **Auth**: API key + secret (HMAC signing — simpler than crypto wallet signing)
- **Fee**: 0.7 cents per contract per side (both maker and taker)
- Economics: At 50c price, fee is ~1.4%. At 90c price, ~0.78%. At 10c price, ~7%.

#### Spreads & Competition
- Headline markets: 2-5 cent spreads
- Mid-tier: 5-15 cents
- Long-tail: 15-40 cents
- **Less sophisticated competition than Polymarket** — US-only access skews retail
- **Many markets have very thin books** (< $1K total liquidity at best bid/ask)

#### Strengths
- **Regulatory clarity**: CFTC-regulated = lowest regulatory risk of any venue
- **USD settlement**: No crypto/bridge/smart contract risk
- **Quantitative markets**: CPI, GDP, temperature, Fed decisions — well-suited to systematic modeling with public data
- **API simplicity**: Standard REST with API key auth — simplest to implement in C++

#### Risks
- **Low volume**: Fewer fills = slower capital turnover
- **Strategy portability**: Low (~35% code reuse)
- **Congressional risk**: Ongoing debates about event contract regulation

---

### 3.7 Crypto Options (Deribit)

#### Platform Overview
Dominant crypto options exchange (~85-90% of all crypto options volume).

- **Volume**: $500M-$2B/day in options notional
- **Underlyings**: BTC, ETH, SOL
- **API**: JSON-RPC 2.0 over WebSocket (primary), REST, FIX (institutional)
- **Maker fee**: -0.01% to -0.02% **rebate** (you get paid)
- **Taker fee**: 0.03% of underlying

#### Vol Surface Edge
- Wings (far OTM) tend to be mispriced — low liquidity, wide spreads
- Term structure kinks around expiry rolls
- Crypto has a "call skew" bias (unlike equities' put skew) — flips during crashes
- Short-dated options (< 7 DTE) have widest spreads and most mispricing
- **Structural edge exists**: Crypto vol surfaces are genuinely mispriced vs realized

#### Complexity Assessment (Critical)
Options MM requires fundamentally different infrastructure:

1. **Vol surface construction**: SVI or SABR calibration, no-arb enforcement, real-time refitting
2. **Options pricing engine**: Black-Scholes + adjustments for fat tails, jumps
3. **Greeks calculator**: All first and second order Greeks, portfolio aggregation
4. **Delta hedging engine**: Automated hedging with configurable frequency
5. **Vol forecasting**: HAR, GARCH, or ML-based realized vs implied comparison
6. **Margin/risk calculator**: Mirror exchange's margin model

| Dimension | Spot/Perp MM | Options MM |
|-----------|-------------|------------|
| State space | 1D (price) | 3D+ (price, vol, time, strikes) |
| Risk | Inventory risk | Greeks risk (delta, gamma, vega, theta) |
| Math | Statistics, signals | Stochastic calculus, numerical methods |
| Capital | Moderate | Higher (margin for short options) |
| Build time | 8-13 weeks | +6-12 additional weeks |

**Competition**: QCP Capital, GSR, Paradigm, Jump, Jane Street reportedly active. Less brutal than spot BTC MM but more sophisticated than altcoin spot.

**Verdict**: High edge potential, extremely high implementation complexity. Best as Phase 2 after perp MM is working and profitable. QuantLib (C++) provides building blocks.

---

### 3.8 Cross-Venue Arbitrage (CEX-DEX)

**Not recommended for solo dev.** Here's why:

- **Extremely competitive**: Top 10-20 MEV searchers capture majority of opportunities on Ethereum
- **Multi-venue complexity**: Must maintain CEX API + blockchain node + smart contract interaction + gas management
- **MEV risk**: Transactions visible in mempool get front-run/sandwiched unless using Flashbots/Jito
- **Capital splitting**: Need pre-funded inventory on both venues
- **Execution asymmetry**: CEX fills in ms, DEX fills in seconds to minutes — arb can vanish during DEX confirmation
- **Gas costs eat profits**: Ethereum L1 gas can be $5-50 per swap
- **Most realistic variant**: CEX perps vs DEX perps (Hyperliquid/dYdX) exploiting funding rate divergences — less latency-sensitive but still complex

**Verdict**: Poor complexity/reward ratio for solo dev. Skip.

---

## 4. Strategy Portability Analysis

How much of the current codebase design (C++20, A-S model, SPSC bus, event-driven architecture) transfers:

| Component | Binance | Hyperliquid | dYdX v4 | Polymarket | Kalshi | Deribit |
|-----------|---------|-------------|---------|------------|--------|---------|
| Event-driven arch | 100% | 100% | 100% | 100% | 100% | 100% |
| SPSC event bus | 100% | 100% | 100% | 100% | 100% | 100% |
| A-S quoter | 100% | 95% | 95% | 10% | 10% | 20% |
| OBI signal | 100% | 90% | 85% | 20% | 20% | 40% |
| BTC-lead signal | 100% | 90% | 85% | 0% | 0% | 30% |
| Inventory mgmt | 100% | 95% | 95% | 50% | 50% | 30% |
| Risk framework | 100% | 90% | 90% | 70% | 70% | 60% |
| Funding rate arb | 100% | 85% | 85% | 0% | 0% | 0% |
| Kill switch | 100% | 95% | 90% | 90% | 90% | 90% |
| Exchange connectivity | — | rewrite | rewrite | rewrite | rewrite | rewrite |
| Order book struct | 100% | 95% | 95% | 80% | 80% | 70% |
| **Overall** | **100%** | **~90%** | **~88%** | **~35%** | **~35%** | **~40%** |

**Key insight**: Hyperliquid and dYdX require mainly an exchange connectivity rewrite. Core strategy logic is directly portable. Prediction markets and options require fundamentally different strategies.

---

## 5. Fee Economics Comparison (Per Round Trip as Maker)

This is the most important economic comparison. All values in basis points:

| Venue | Maker Fee (base) | Round Trip Cost | Typical Altcoin Spread | Net Edge Before Adverse Selection |
|-------|-----------------|-----------------|----------------------|----------------------------------|
| **Binance** | 7.5-10 bps | 15-20 bps | 5-15 bps | **-5 to -5 bps** (negative) |
| **Hyperliquid** | 1.5 bps | 3.0 bps | 5-20 bps (est.) | **2-17 bps** |
| **dYdX v4** | 1.0 bps | 2.0 bps | 14-26 bps (measured) | **12-24 bps** |
| **Lighter** | 0 bps | 0 bps | unknown | **full spread** |
| **Paradex** | 0 bps | 0 bps | unknown | **full spread** |
| **Deribit** | -1 to -2 bps | -2 to -4 bps (rebate) | varies | **spread + rebate** |
| **Polymarket** | 0 bps | 0 bps | 100-3000 bps | **100-3000 bps** |
| **Kalshi** | ~70-200 bps* | ~140-400 bps | 200-4000 bps | **60-3600 bps** |

*Kalshi fees vary by contract price.

**dYdX offers the best verified edge for the A-S strategy**: 12-24 bps net edge on the exact altcoins you're targeting, with live spread data confirming the opportunity.

---

## 6. Risk-Adjusted Profitability Estimation

Assumes $10K starting capital, 24/7 operation, competent solo dev. All estimates are speculative.

| Metric | Binance Altcoin | Hyperliquid | dYdX v4 | Polymarket | Kalshi |
|--------|----------------|-------------|---------|------------|--------|
| Edge per round trip | 0-3 bps | 5-15 bps | 12-24 bps | 50-300 bps | 50-500 bps |
| Round trips/day | 50-200 | 30-150 | 10-50 | 5-30 | 3-20 |
| Expected daily PnL | $0-15 | $15-80 | $10-60 | $5-60 | $3-30 |
| Monthly (realistic) | $0-200 | $300-1,200 | $200-1,000 | $200-800 | $100-500 |
| Max drawdown | 5-15% | 3-10% | 3-10% | 5-20% | 3-10% |
| Sharpe estimate | 0.5-2.0 | 2.0-3.5 | 2.0-4.0 | 1.0-3.0 | 1.5-3.5 |

> **Disclaimer**: These are rough estimates. Actual results depend on implementation quality, market regime, and competition dynamics. All trading involves risk of loss.

---

## 7. Implementation Timeline

Weeks to live trading from current state (solo C++ dev):

| Phase | Binance | Hyperliquid | dYdX v4 | Polymarket | Kalshi |
|-------|---------|-------------|---------|------------|--------|
| Exchange connectivity | 2-3w | 2-3w | 3-4w | 2-3w | 1-2w |
| Market data + book | 1-2w | 1-2w | 1-2w | 1-2w | 1-2w |
| Core strategy | 2-3w | 2-3w (port A-S) | 2-3w (port A-S) | 4-6w (new model) | 4-6w (new model) |
| Risk management | 1-2w | 1-2w | 1-2w | 1-2w | 1-2w |
| Testing + paper trade | 2-3w | 2-3w | 2-3w | 2-3w | 2-3w |
| **Total** | **8-13w** | **8-13w** | **9-14w** | **10-16w** | **9-15w** |

---

## 8. Multi-Venue Progression Paths

Start with ONE venue, expand later. Architecture should accommodate expansion.

### Path A: DEX Perp-First (Strongest for A-S strategy)
1. **Start**: dYdX v4 altcoin perp market making (widest verified spreads, lowest fees)
2. **Add**: Hyperliquid as second venue (more volume, same strategy)
3. **Add**: Funding rate arb across both venues
4. **Later**: Binance perps for cross-venue signals or arb

### Path B: Hyperliquid-First (More volume, more competitive)
1. **Start**: Hyperliquid mid/long-tail perp market making
2. **Add**: dYdX v4 for pairs with wider spreads
3. **Add**: Cross-venue funding rate divergence trades
4. **Later**: Deribit options (Phase 2, requires new models)

### Path C: Prediction-First (Different strategy entirely)
1. **Start**: Kalshi market making (US-legal, quantitative models)
2. **Add**: Polymarket for higher-volume events
3. **Expand**: Cross-platform prediction arb
4. **Later**: Crypto venues for diversification

### Path D: Multi-Asset (Advanced)
1. **Start**: dYdX v4 or Hyperliquid perp MM
2. **Add**: Deribit options MM (after perp is profitable, 6-12 months later)
3. **Expand**: Cross-product signals (options vol predicts perp moves)

---

## 9. Key Questions for Decision-Making

The evaluating agent should consider these questions:

### Priority 1: Economic Edge
1. Which venue offers the highest expected edge per trade after fees? (**dYdX v4 has 12-24 bps verified**)
2. Where is competition weakest relative to the operator's capabilities? (**dYdX altcoins, prediction market long-tail**)
3. How sustainable is the edge — structural or temporary? (**DEX perp spreads are structural while competition is thin; prediction markets are structural due to information asymmetry**)

### Priority 2: Feasibility
4. How much of the existing codebase design can be reused? (**~88-90% for DEX perps, ~35% for prediction markets**)
5. What's the fastest path to live trading with positive PnL? (**Hyperliquid/dYdX at 8-13 weeks**)
6. Does the operator have the domain knowledge needed? (**Perps: engineering suffices. Prediction markets: need domain knowledge. Options: need stochastic calculus.**)

### Priority 3: Risk
7. Platform risk: How much capital is at risk from venue failure? (**Kalshi lowest, DEXes moderate, Polymarket moderate**)
8. Regulatory risk: Could the venue become inaccessible? (**Kalshi safest, Hyperliquid/dYdX uncertain, Polymarket US-blocked**)
9. Drawdown risk: How much could be lost before the strategy is proven? (**All venues: 3-15% depending on risk management quality**)

### Priority 4: Growth
10. Can the strategy scale with more capital? (**DEX perps limited by venue depth; prediction markets limited by market size; Deribit options scale well**)
11. Does the venue have growing or declining volume? (**Hyperliquid: growing rapidly. dYdX: declining relative. Polymarket: event-dependent.**)
12. Does this build transferable skills? (**Perp MM skills transfer to all CLOB venues. Prediction MM skills transfer to options/betting. Options MM skills transfer to trad-fi.**)

---

## 10. Conclusion Framework

This document intentionally does **not** make a final recommendation. It provides data and analysis for an evaluating agent to weigh tradeoffs.

**The key tradeoff axes are:**

1. **Edge size vs. volume**: dYdX has the widest verified spreads but lowest volume. Hyperliquid has the most volume but tighter spreads and tougher competition. The optimal choice depends on whether edge-per-trade or trade-frequency matters more.

2. **Strategy portability vs. edge quality**: Perp DEXes reuse ~90% of current code. Prediction markets require ~65% rewrite but offer 10-100x wider spreads with less latency sensitivity.

3. **Platform maturity vs. competition level**: Binance is battle-tested but crowded. dYdX/Hyperliquid are newer but have better economics and less competition. Zero-fee venues (Lighter/Paradex) are unproven but have the best unit economics.

4. **Regulatory safety vs. edge access**: Kalshi is the safest regulatory choice but has the lowest volume. DEX perps have the best economics but uncertain regulatory futures.

5. **Single-venue depth vs. multi-venue breadth**: Starting with one venue and going deep beats spreading thin across multiple venues for a solo dev. The progression path matters.

The evaluating agent should weight these axes according to the operator's stated priorities and constraints, then make a specific, actionable recommendation.

---

## Appendix: Data Freshness & Caveats

| Data Point | Source | Freshness | Confidence |
|-----------|--------|-----------|------------|
| dYdX live spreads | dYdX indexer API, March 30 2026 | Live | High |
| Hyperliquid fee tiers | Hyperliquid docs, verified March 2026 | Current | High |
| dYdX fee tiers | dYdX docs, verified March 2026 | Current | High |
| Binance fee tiers | Binance.com, verified early 2025 | May be outdated | Medium |
| Hyperliquid volume | CoinMarketCap/exchange data, early 2026 | Recent | Medium |
| Polymarket volume | Post-election estimates | Approximate | Low-Medium |
| Kalshi volume | Public dashboard, approximate | Approximate | Medium |
| Competition analysis | News reports, public disclosures | Qualitative | Medium |
| PnL estimates | Derived from spreads/fees/assumptions | Speculative | Low |
