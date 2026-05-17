# Trader — Advanced Algorithms Research (2026)

> A companion reference on algorithmic techniques that are *not* covered in `RESEARCH_DATA_ALGORITHMS.md`, picked because they are both (a) genuinely absent from the existing doc, and (b) at least plausibly useful for a $100 Kalshi event-trading bot running on Phase 1 of a 2-phase system. Written April 2026 alongside the first live-on-demo deployment.

---

## Relationship to existing docs

This is a **companion to `RESEARCH_DATA_ALGORITHMS.md`**, intentionally does not duplicate it. Where the sister doc is definitive, it will be referenced by section (e.g. "§8.2"). If something is covered there, it is deliberately omitted here. The goal of this document is to sit *beside* the sister doc as a "what else is out there, and is it worth the engineering cost" reference.

**Already in the sister doc — not repeated here**:

- Data sources (weather, macro, options, Kalshi platform) — sister doc §2-§6.
- Standard and fractional Kelly, Bayesian Kelly with uniform MC, credal-set Kelly, drawdown-constrained Kelly, multi-bet Kelly via CVXPY — sister doc §9.1–9.9.
- Beta calibration, inductive Venn-Abers, conformal prediction basics, Dirichlet, EMOS/NGR, Brier decomposition, Spiegelhalter Z, adaptive/smooth ECE, PSI (as one drift signal among many) — sister doc §8.
- CVaR optimization, HRP, mean-variance failure arguments — sister doc §10.
- Linear/log/geometric pools, BMA, stacking, Bates-Granger, extremization — sister doc §11.
- SVI, SABR surface parametrization, Breeden-Litzenberger — sister doc §5.1–5.3.
- Favorite-longshot bias, walk-forward, purged+embargoed CV, deflated Sharpe — sister doc §11.6, §12.
- LightGBM, QRF, bridge regression, DFM, MIDAS — sister doc §7.

**What this doc adds** — 18 topics grouped into five clusters:

1. *Sequential inference* (Kalman, particle filters, BOCD, HMM, change-point)
2. *Online learning and regret minimization* (EW-RLS, FTRL, Hedge, EXP3, online isotonic)
3. *Dependence and joint-probability* (copulas, QMC, importance sampling, EVT)
4. *Signal extraction on prices themselves* (mean-reversion, OU fits, meta-labeling, GARCH, jumps)
5. *Bandits, scoring rules, and diagnostics* (Thompson, UCB, LinUCB, CRPS variants, Page-Hinkley, ADWIN, JS divergence)

See the final section, **"Recommended order of implementation for this bot,"** for the opinionated ordering. If you only have time to read one section of this doc, read that one.

---

## Summary table

| # | Topic | Worth it for $100 bot? | Cost (engineer-weeks) | Primary payoff |
|---|---|---|---|---|
| 1 | Kalman / particle filter on latent state | **Maybe** — weather-station bias yes; macro no | 2-4 | Recovers a slow-moving bias term station-by-station |
| 2 | Online / streaming calibration | **Yes, eventually** | 1-2 | Removes "refit weekly" latency, cuts drift lag |
| 3 | Regret-minimizing ensembles (Hedge/EXP3/FTRL) | **Yes** | 1 | Cheap alternative to stacking; provably robust |
| 4 | BOCD / HMM / CUSUM | **Yes for monitoring, no for modeling** | 1 | Early-warning when model goes stale |
| 5 | Copula-based joint pricing | **Maybe** — only when cluster sizes ≥5 | 2 | Replaces cluster-cap hack with actual joint Kelly |
| 6 | Sobol / importance sampling / stratified MC | **Yes** | 0.5 | 10-100× faster Bayesian Kelly integration |
| 7 | KL / log-score / info-gain diagnostics | **Yes** | 0.5 | Cheap market-selection signal |
| 8 | OU / mean-reversion on Kalshi prices | **Probably no** for $100 capital | 1 | Orthogonal strategy, but needs scale |
| 9 | Cox / Kaplan-Meier / parametric hazards | **Yes for hurricane season** | 1 | Direct model for "does X happen by Y" |
| 10 | Heston / rough-vol / SABR extensions | **No** | 3 | Only helps deep-ITM crypto/equity derivatives |
| 11 | Bandits for category allocation (Thompson/LinUCB) | **Yes** | 1-2 | Principled exploration of $100 bankroll |
| 12 | Meta-labeling (Lopez de Prado) | **Yes if you have a primary model** | 1 | 5-15% precision uplift on existing edges |
| 13 | Online isotonic / PAVA | **Yes** | 1 | Streaming Venn-Abers |
| 14 | GARCH / jump-diffusion for underlyings | **No** (no real Kalshi product depends on it) | 2 | Marginal; sister-doc options priors dominate |
| 15 | CRPS variants (tail-weighted, energy, variogram) | **Yes for weather** | 0.5 | Tail-accurate scoring for temperature contracts |
| 16 | Page-Hinkley / ADWIN / KS-CUSUM / JS | **Yes** | 1 | Drift detection portfolio |
| 17 | Bandits for execution (cancel/hold/reprice) | **No** | 2 | Not enough orders/day at $100 to train |
| 18 | EVT (GPD / GEV) for tail contracts | **Yes for rare-event contracts** | 1 | Correct physics for "record temp / cat-5" markets |

"Engineer-weeks" assumes a solo developer with the existing Python sidecar pattern. "Cost" does not include validation/backtesting — typically add 50-100%.

---

## Table of Contents

1. [Sequential Bayesian updating on latent state](#1-sequential-bayesian-updating-on-latent-state)
2. [Online / streaming learning](#2-online--streaming-learning)
3. [Regret-minimizing model ensembles](#3-regret-minimizing-model-ensembles)
4. [Change-point detection and regime switching](#4-change-point-detection-and-regime-switching)
5. [Copula-based joint probability modeling](#5-copula-based-joint-probability-modeling)
6. [Monte Carlo improvements](#6-monte-carlo-improvements)
7. [Information-theoretic sizing and diagnostics](#7-information-theoretic-sizing-and-diagnostics)
8. [Mean-reversion signals on Kalshi prices](#8-mean-reversion-signals-on-kalshi-prices)
9. [Survival / hazard models for time-to-event contracts](#9-survival--hazard-models-for-time-to-event-contracts)
10. [Heston / rough-vol / SABR extensions](#10-heston--rough-vol--sabr-extensions)
11. [Bandit algorithms for category exploration](#11-bandit-algorithms-for-category-exploration)
12. [Meta-labeling / secondary classifier](#12-meta-labeling--secondary-classifier)
13. [Online calibration-preserving ensembles](#13-online-calibration-preserving-ensembles)
14. [Volatility & jump models](#14-volatility--jump-models)
15. [Scoring rules beyond Brier / log](#15-scoring-rules-beyond-brier--log)
16. [Practical drift detection suite](#16-practical-drift-detection-suite)
17. [Multi-armed bandit for execution](#17-multi-armed-bandit-for-execution)
18. [Extreme value theory for tail risk](#18-extreme-value-theory-for-tail-risk)
19. [Recommended order of implementation for this bot](#19-recommended-order-of-implementation-for-this-bot)

---

## 1. Sequential Bayesian updating on latent state

### What it is

State-space models (SSMs) treat an unobserved state `x_t` as a Markov process, and observations `y_t` as noisy readouts. Kalman filters are the closed-form solution when `x_t` is linear-Gaussian; particle filters (Gordon-Salmond-Smith 1993 bootstrap filter) handle nonlinear, non-Gaussian transitions by a weighted empirical distribution over particles. The big win: you do not refit a model from scratch when new data arrives — you update a posterior over the hidden state in one step, in constant memory. You get a full posterior distribution, not just a point estimate.

### Why it matters for Kalshi

Two concrete cases:

1. **Per-station warmth bias.** The NBM or AIFS ensemble mean at JFK vs realized temperature has a drift that shifts over the year (urban heat island, seasonal albedo, sensor rehoming after maintenance). A Kalman filter with a slow random walk on "JFK bias" and daily obs error gives you an evolving corrective term: `T_corrected = T_ensemble + bias_t`. The current code recomputes this by rolling-average — a state-space model is the right object, gives you the uncertainty in the bias, and naturally handles missing observations.

2. **Latent "who is trading" state in Kalshi order flow.** A two-state HMM (retail vs informed) on trade size and timing can gate whether to trust near-market-open price as fair. This is speculative but not crazy.

The sister doc (§7.3) mentions Dynamic Factor Models as Kalman-driven — that is a specific structure. This section is about the general pattern: **any time you have a slowly-drifting unobserved quantity that you could estimate online.**

### Math sketch

Linear-Gaussian Kalman:
```
x_t   = F · x_{t-1} + w_t,   w_t ~ N(0, Q)   (state eqn)
y_t   = H · x_t     + v_t,   v_t ~ N(0, R)   (obs eqn)

Predict:
  x̂_{t|t-1} = F · x̂_{t-1|t-1}
  P_{t|t-1} = F · P_{t-1|t-1} · Fᵀ + Q

Update:
  K_t       = P_{t|t-1} · Hᵀ · (H · P_{t|t-1} · Hᵀ + R)⁻¹
  x̂_{t|t}   = x̂_{t|t-1} + K_t · (y_t − H · x̂_{t|t-1})
  P_{t|t}   = (I − K_t · H) · P_{t|t-1}
```

For the station-bias case, the minimal 1-D version is:
```
bias_t   = bias_{t-1} + w_t,   w_t ~ N(0, q)   // slow random walk
resid_t  = bias_t + v_t,       v_t ~ N(0, r)   // daily forecast - obs residual

// Filter update (scalar)
p_pred  = p_post_prev + q
K       = p_pred / (p_pred + r)
bias_t  = bias_t_prev + K · (resid_t − bias_t_prev)
p_post  = (1 − K) · p_pred
```

Tune `q` to match realized bias volatility; `r` from residual variance once bias is removed.

Bootstrap particle filter (for nonlinear / non-Gaussian):
```
for each particle i:
  x_t^(i) ~ p(x_t | x_{t-1}^(i))          // propagate
  w_t^(i) ∝ p(y_t | x_t^(i))              // weight
normalize; resample when ESS < N/2
```

### Practical recipe

**Kalman for station bias — C++ in ~80 LOC.**
- 1-D scalar is 15 lines, no Eigen needed.
- 4-D state (bias, trend, seasonal_sin, seasonal_cos) is ~60 lines with Eigen.
- Store `(station_id, bias, posterior_var, last_update_ts)` in SQLite.
- Update after every weather-contract settlement: residual = `observed − ensemble_mean`.
- Re-use during live trading: `corrected_ensemble = ensemble + bias_posterior_mean`.

**Particle filter — only if the dynamics are nonlinear.** `particles` library in Python (Chopin-Papaspiliopoulos), or roll your own in C++ (~200 LOC). Honestly, if you are reaching for a PF on a $100 bankroll, you are almost certainly over-engineering.

**Library**: no C++ standard; write your own. Python prototype: `pykalman` (archived but works), `filterpy` (maintained), `dynamax` (JAX, overkill).

### Citations

- Gordon, Salmond, Smith 1993, "Novel approach to nonlinear/non-Gaussian Bayesian state estimation" — original bootstrap filter: <https://digital-library.theiet.org/doi/10.1049/ip-f-2.1993.0015>
- Särkkä 2013, *Bayesian Filtering and Smoothing* — free PDF at <https://users.aalto.fi/~ssarkka/pub/cup_book_online_20131111.pdf>. Best modern textbook.
- `filterpy` Python library: <https://github.com/rlabbe/filterpy>
- Labbe's tutorial book: <https://github.com/rlabbe/Kalman-and-Bayesian-Filters-in-Python>

### When to skip

**Skip if you only have one station or one macro series.** Kalman shines when you have ~10+ parallel slowly-drifting unobserved quantities (stations × seasons × sensors) to track in a unified framework. For a single bias term with 500+ observations, a simple exponentially-weighted moving average (`α=0.05`) gets 90% of the benefit. The Kalman filter is the *right* object, but a scalar EWMA on residuals is probably enough — and is already close to what `AdaptiveSizer` does for the Brier score.

**Also skip particle filters.** At $100 capital, if your state needs to be non-Gaussian nonlinear, you do not have enough data for the particle cloud to be meaningful. PF is a 2028 problem, not a 2026 one.

---

## 2. Online / streaming learning

### What it is

Recursive algorithms that update model parameters in constant time per new observation, without retraining from scratch. The canonical members:

- **Recursive Least Squares (RLS)** — exact online solution for least-squares regression. Exponentially-weighted RLS (EW-RLS) adds a forgetting factor `λ` to discount old observations.
- **Online Gradient Descent (OGD)** — one gradient step per observation. Simple, works for convex losses.
- **FTRL-Proximal** (McMahan 2013, Google) — Follow-The-Regularized-Leader with a proximal term. Industrial-grade: handles high-dim sparse features, strong sparsity via L1, adaptive per-coordinate learning rates. Powers Google's ad CTR prediction.

Contrast with the current "refit weekly on last 90 days" approach: online methods update **every** settlement, have zero refit latency, and give you adaptive response to regime changes — as long as you get the forgetting factor right.

### Why it matters for Kalshi

The current `ProbabilityCalibrator` (disabled by default per its own comment) blends `w · model + (1−w) · market` where `w` grows with resolved samples. A calibrator should be fit, not hand-tuned. Real Beta calibration (sister doc §8.2) with 3 params can be fit online via SGD on logistic loss in ~30 lines of C++ — and it updates after every settled bet instead of waiting for Sunday's cron.

Second concrete win: **per-category calibrator without offline batching**. Today, a new category (entertainment, say) needs to accumulate 100+ settlements before batch re-fit rotates into the trading loop. FTRL-Proximal with `λ = 0.99` and L1 regularization gives you a usable calibrator after ~50 observations, plus it prunes useless features automatically.

### Math sketch

**EW-RLS** — for a linear model `ŷ = θᵀ x`:
```
P_t = (1/λ) · (P_{t-1} − (P_{t-1} x_t x_tᵀ P_{t-1}) / (λ + x_tᵀ P_{t-1} x_t))
θ_t = θ_{t-1} + P_t x_t (y_t − x_tᵀ θ_{t-1})
```
`λ ∈ (0.9, 1)`, smaller = faster adaptation.

**OGD** — for convex loss `ℓ`:
```
θ_t = θ_{t-1} − η_t · ∇ℓ(θ_{t-1}; x_t, y_t)
η_t = η_0 / √t            (standard step-size schedule)
```

**FTRL-Proximal** (per-coordinate, for logistic regression):
```
z_i    += g_i − σ_i · θ_i                 (where σ_i is the per-coord "learning-rate delta")
σ_i_t   = (1/α) · (√n_{i,t} − √n_{i,t-1})
n_i    += g_i²
θ_i     = {0                                    if |z_i| ≤ λ₁
          (−(z_i − sign(z_i) λ₁)) / ((β + √n_i)/α + λ₂)  otherwise}
g_i = gradient of loss on feature i; α, β, λ₁, λ₂ = hyperparameters
```
(See McMahan 2013 Alg. 1 for the canonical pseudocode.) L1 weight `λ₁` controls sparsity — critical when you have 500+ market-category dummy features.

### Practical recipe

**For Beta calibration, start with OGD on logistic loss.** Beta calibration is a 3-parameter logistic on `[log s, −log(1−s)]`. Five lines of C++:

```cpp
// Beta calibrator: params = (a, b, c), loss = logistic
struct OnlineBetaCalibrator {
    double a = 1.0, b = 1.0, c = 0.0;
    double eta0 = 0.1;
    long t = 0;
    void update(double s, int y) {
        double z = a * std::log(std::max(s, 1e-6))
                 - b * std::log(std::max(1.0 - s, 1e-6))
                 + c;
        double p = 1.0 / (1.0 + std::exp(-z));
        double err = p - y;
        double eta = eta0 / std::sqrt(++t);
        a -= eta * err *  std::log(std::max(s, 1e-6));
        b -= eta * err * -std::log(std::max(1.0 - s, 1e-6));
        c -= eta * err;
    }
    double calibrate(double s) const {
        double z = a*std::log(s) - b*std::log(1-s) + c;
        return 1.0 / (1.0 + std::exp(-z));
    }
};
```

Run this in parallel with the batch calibrator for a month, compare Brier. Graduate to online-only once you trust it.

**For FTRL-Proximal**, the reference implementation (~100 LOC) is in the appendix of McMahan 2013. Use if you add ≥20 categorical features to the calibrator.

**Python sidecar**: `river` (successor to `creme`) is the de-facto online-ML library. `river.linear_model.LogisticRegression(optimizer=optim.FTRLProximal())` is one line.

### Citations

- McMahan, Holt et al. 2013, "Ad Click Prediction: a View from the Trenches" (KDD): <https://research.google.com/pubs/pub41159.html> — FTRL-Proximal, production-scale.
- Hazan 2019, *Introduction to Online Convex Optimization* — free at <https://arxiv.org/abs/1909.05207>.
- Cesa-Bianchi & Lugosi 2006, *Prediction, Learning, and Games* — Cambridge; canonical OCO textbook.
- `river` Python library: <https://riverml.xyz/>
- `online-ml/deep-river` (OGD / FTRL in Python): <https://github.com/online-ml/river>

### When to skip

**Skip FTRL if you have <10 features.** For a 3-parameter Beta calibrator, plain OGD or even batch refit is fine — the gain from FTRL's sparsity is zero. FTRL earns its complexity only when you have L1-regularizable categorical explosions.

**Skip all online methods if you cannot validate online.** Online learners are harder to debug than batch: the parameters move, so a single wrong update leaks forward forever. Put in shadow-mode comparison vs the batch model for **at least** 4 weeks before trusting online-only. The refit-weekly approach is a feature for a two-person team, not a bug.

---

## 3. Regret-minimizing model ensembles

### What it is

Online algorithms that combine `K` forecasters into one, with provable regret bounds: the cumulative loss is bounded above by `min_k loss_k + O(√(T log K))`, even under adversarial forecasters. The classic three:

- **Hedge / Multiplicative Weights** (Freund-Schapire 1997) — full-information setting (all forecasters' losses observable). `w_{t+1,i} ∝ w_{t,i} · exp(−η · loss_{t,i})`.
- **EXP3** (Auer et al. 2002) — bandit setting: you only observe the loss of the forecaster you *chose*. Exploration via ε-mixing.
- **FTRL** (regularized leader, also from McMahan 2013 / Zinkevich 2003) — generalizes both, with richer regularization choices.

Contrast with stacking (sister doc §11.3): stacking needs a held-out validation set to fit the meta-model. Hedge does not — weights adapt online from the live stream of settled outcomes.

### Why it matters for Kalshi

The bot already runs multiple probability forecasters per contract (weather ensemble + NBM + your QRF + market price + Kalshi forecast-percentiles). Right now (per `probability_engine.cpp`) they are combined by a fixed rule. A Hedge layer above them gives:

- Weights that adapt to recent performance without retraining.
- A provable upper bound: you will do no worse than the best single forecaster, plus a vanishing regret term.
- Natural decay: forecasters that degrade (model drift) get demoted automatically.

This is a **cheap, low-risk upgrade**. Maybe a week of work. Honestly should be implemented before stacking — stacking needs far more data and careful CV.

### Math sketch

**Hedge** on log-loss forecasts:
```
// K experts, each outputting p_{t,k} ∈ (0, 1) for a binary outcome y_t
// Initialize
w_0,k = 1 for all k
η     = √(8 ln K / T)            // optimal known-T

for t = 1, 2, ...:
    p̄_t = Σ_k w_{t-1,k} p_{t,k} / Σ_k w_{t-1,k}
    observe y_t
    for k:
        ℓ_{t,k}   = −[y_t log p_{t,k} + (1−y_t) log(1−p_{t,k})]     // log loss
        w_{t,k}   = w_{t-1,k} · exp(−η · ℓ_{t,k})
```

Regret after `T` rounds: `Σ_t ℓ(p̄_t, y_t) − min_k Σ_t ℓ(p_{t,k}, y_t) ≤ √(T ln K / 2)` (Cesa-Bianchi-Lugosi Thm 2.2).

**EXP3** — bandit version, useful if you can only evaluate one forecaster per decision (cost of eval, or mutually-exclusive models):
```
p_{t,k} ∝ (1 − γ) · w_{t,k} / Σ_j w_{t,j} + γ/K
sample k_t ~ p_{t,·}
observe ℓ_{t,k_t}
ℓ̂_{t,k_t} = ℓ_{t,k_t} / p_{t,k_t}   // importance-weighted
w_{t+1,k_t} = w_{t,k_t} · exp(−η · ℓ̂_{t,k_t})
```
Expected regret: `O(√(TK log K))`. Auer et al. 2002 give the constants.

**FTRL** with entropy regularization recovers Hedge; with L2 regularization gives online gradient descent. The unifying view is useful when you want to mix sparsity (L1) with expert aggregation.

### Practical recipe

**Hedge for forecaster aggregation — 30 LOC C++:**

```cpp
class HedgeAggregator {
    std::vector<double> w_;
    double eta_;
public:
    HedgeAggregator(int K, double eta) : w_(K, 1.0), eta_(eta) {}
    double predict(const std::vector<double>& ps) const {
        double num = 0.0, den = 0.0;
        for (size_t k = 0; k < ps.size(); ++k) {
            num += w_[k] * ps[k];
            den += w_[k];
        }
        return num / den;
    }
    void observe(const std::vector<double>& ps, int y) {
        for (size_t k = 0; k < ps.size(); ++k) {
            double p = std::clamp(ps[k], 1e-6, 1.0 - 1e-6);
            double loss = -(y * std::log(p) + (1-y) * std::log(1-p));
            w_[k] *= std::exp(-eta_ * loss);
        }
        // rescale to prevent underflow
        double mx = *std::max_element(w_.begin(), w_.end());
        if (mx < 1e-100) for (auto& wi : w_) wi /= mx;
    }
};
```

- `eta ≈ √(8 log K / T_expected)`. For K=5 forecasters, T=1000 expected settlements, `eta ≈ 0.114`.
- Start `eta` higher (0.3) for the first 50 rounds to adapt fast; anneal to theoretical optimum.
- Persist `w_` in SQLite after every update.

**For drift-robustness**, combine with a "restart" rule: when *all* weights concentrate on one expert for >50 rounds (Herfindahl > 0.9), **halve** `w_k` for the leader and renormalize. This is a hack but prevents one expert from locking out the others after a regime change. The principled version is Fixed-Share (Herbster-Warmuth 1998).

**Library**: for C++ there is none worth adopting — write it. Python: `river.ensemble.BaggingClassifier`, or the textbook implementation in ~50 LOC.

### Citations

- Freund-Schapire 1997, "A Decision-Theoretic Generalization of On-Line Learning" — Hedge + boosting: <https://www.sciencedirect.com/science/article/pii/S002200009791504X> (JCSS 55); preprint <https://www.cs.princeton.edu/courses/archive/spring07/cos424/papers/FreundSchapireJCSS97.pdf>.
- Auer, Cesa-Bianchi, Freund, Schapire 2002, "The Nonstochastic Multi-Armed Bandit Problem" (SICOMP): <https://www.schapire.net/papers/AuerCeFrSc01.pdf>.
- Cesa-Bianchi & Lugosi 2006, *Prediction, Learning, and Games* — canonical textbook.
- Herbster-Warmuth 1998, "Tracking the Best Expert" — Fixed-Share.
- Arora, Hazan, Kale 2012, "The Multiplicative Weights Update Method: A Meta-Algorithm and Applications" — survey: <https://www.cs.princeton.edu/~arora/pubs/MWsurvey.pdf>.

### When to skip

**Skip if you only have 1-2 forecasters.** Hedge is overkill below K=3; a simple 50/50 mix (or inverse-Brier weighted) is fine.

**Skip EXP3 here.** EXP3 is for when you cannot evaluate all experts cheaply. All your forecasters are cheap — just run them all and Hedge. EXP3 earns its keep in expensive-to-evaluate setups (e.g., A/B testing website variants).

**Do not replace stacking with Hedge if you already have stacking working.** Stacking with CV will win on a stationary setting because it exploits joint structure. Hedge wins when the environment shifts. **Run both**: stacking for the stable regime, Hedge as a backup / check. If they diverge sharply, that's a useful signal on its own.

---

## 4. Change-point detection and regime switching

### What it is

Statistical methods that identify when the generative process of a time series has changed. Four usable families:

1. **CUSUM** (Page 1954) — monitors cumulative sum of standardized residuals; flags when it crosses a threshold.
2. **Bayesian Online Change-Point Detection (BOCD)** — Adams & MacKay 2007. Maintains `P(r_t | data)` where `r_t` is "run length since last change-point." Exact Bayesian inference, online, in `O(T)` space and time (can be truncated to constant space). The modern default.
3. **Hidden Markov Models (HMMs)** — fit `K` regimes with transition matrix, filter with forward-backward. Best when regimes are persistent and recur.
4. **Kolmogorov-Smirnov (KS) CUSUM** — distribution-free. Useful for detecting changes in the *shape* of residuals, not just mean.

### Why it matters for Kalshi

Two hot spots:

1. **Model Brier-score surveillance.** Current `AdaptiveSizer` compares rolling Brier to all-time Brier via a simple ratio. BOCD on the Brier series gives a principled posterior probability of "the model just broke," with calibrated false-alarm rate. When `P(run_length < 10) > 0.5`, pause trading in that category and refit.

2. **Detecting sharps entering a market.** The Kalshi orderbook for a particular contract may price calmly for weeks, then tighten dramatically when informed traders arrive (e.g., a known weather-prediction fund takes a position). BOCD on the mid-price or on bid-ask spread detects this regime change and flags the contract as "no longer a soft market" — shrink size or skip.

### Math sketch

**CUSUM** on standardized residuals `z_t`:
```
S_t⁺ = max(0, S_{t-1}⁺ + z_t − k)       // upper CUSUM
S_t⁻ = max(0, S_{t-1}⁻ − z_t − k)       // lower
alarm if S_t⁺ > h or S_t⁻ > h
k = minimum shift size to detect (0.5 std, say)
h = threshold (4-5 std is typical; tune for false-alarm rate)
```

**BOCD** (Adams-MacKay):
```
// r_t ∈ {0, 1, 2, ...} = run length since last change-point
// π(x_t | x_{t-r_t:t-1}) = predictive under the model for run length r
// H(r) = hazard function — prob. of change-point at run r (constant = 1/λ for memoryless)

for each t:
    π_t(r) = predictive prob of y_t given last r points      (update sufficient stats)
    growth[r] = P(r_{t-1} = r-1) · π_t(r-1) · (1 − H(r-1))   // extend run
    cp[0]     = Σ_r P(r_{t-1} = r) · π_t(r) · H(r)           // restart
    P(r_t = r) ∝ [growth ∪ cp]
    normalize
```

With a conjugate Gaussian model (mean unknown, variance known), sufficient stats = `(n, Σx, Σx²)`. See Gundersen's blog tutorial for a clean Python impl.

**HMM** — fit by Baum-Welch (EM):
```
// K states, transition matrix A, emission p(y | state)
forward:  α_t(k)   = p(y_1:t, s_t = k)
backward: β_t(k)   = p(y_{t+1:T} | s_t = k)
posterior: γ_t(k)  = α_t(k) β_t(k) / Σ_j α_t(j) β_t(j)
```

### Practical recipe

**BOCD on the rolling Brier per category — 60 LOC Python sidecar.**

```python
# conjugate Gaussian BOCD
import numpy as np
class BOCD:
    def __init__(self, hazard, mu0=0.2, kappa0=1, alpha0=1, beta0=1):
        self.h = hazard        # constant hazard = 1/expected_run_length
        self.mu, self.k, self.a, self.b = np.array([mu0]), np.array([kappa0]), np.array([alpha0]), np.array([beta0])
        self.probs = np.array([1.0])

    def update(self, x):
        # Student-t predictive
        df = 2 * self.a
        sigma = np.sqrt(self.b * (self.k + 1) / (self.a * self.k))
        from scipy.stats import t
        pred = t.pdf((x - self.mu)/sigma, df=df) / sigma

        H = self.h
        growth = self.probs * pred * (1 - H)
        cp = np.sum(self.probs * pred * H)
        self.probs = np.concatenate([[cp], growth])
        self.probs /= self.probs.sum()

        # update suff. stats: prepend a fresh prior, grow existing
        self.mu = np.concatenate([[0.2], (self.k * self.mu + x) / (self.k + 1)])
        self.k  = np.concatenate([[1.0], self.k + 1])
        self.a  = np.concatenate([[1.0], self.a + 0.5])
        self.b  = np.concatenate([[1.0], self.b + (self.k * (x - self.mu[1:])**2) / (2*(self.k+1))])

    def cp_prob(self):
        return self.probs[0]   # P(this was a change-point)
```

Pipe per-category Brier through a `BOCD(hazard=1/200)` (200 settlements = expected stable regime). When `cp_prob > 0.5`, log an alert and pause that category for one epoch.

For C++: implement the forward filter only; keep `probs` as a `std::vector<double>` truncated to length 500 (drop tail when `sum(tail) < 1e-6`). ~120 LOC.

**HMM** — for 2-state "calm / sharp" on Kalshi mid-price volatility, use `hmmlearn` Python sidecar, 5 LOC. Do not write HMM in C++.

### Citations

- Adams & MacKay 2007, "Bayesian Online Changepoint Detection": <https://arxiv.org/abs/0710.3742>. 800+ citations; the canonical online change-point paper.
- Page 1954, "Continuous Inspection Schemes" (Biometrika) — original CUSUM.
- Gundersen 2019 blog, "Bayesian Online Changepoint Detection" (best tutorial): <https://gregorygundersen.com/blog/2019/08/13/bocd/>
- Gundersen 2020, "Implementing Bayesian Online Changepoint Detection": <https://gregorygundersen.com/blog/2020/10/20/implementing-bocd/>
- Killick, Fearnhead, Eckley 2012, "Optimal Detection of Changepoints with a Linear Computational Cost" (PELT): <https://arxiv.org/abs/1101.1438>
- `bocd` Python library: <https://pypi.org/project/bocd/>
- `ruptures` (offline change-point, batch): <https://centre-borelli.github.io/ruptures-docs/>
- `hmmlearn`: <https://hmmlearn.readthedocs.io>

### When to skip

**Skip HMM for $100 capital.** HMMs need hundreds of observations per state to estimate transition probabilities. Your "sharp vs retail" regime lives or dies on ~20 regime changes per year across all categories — Baum-Welch will overfit badly.

**Skip offline change-point methods (PELT) for live monitoring.** They are for retrospective analysis only.

**Do not use BOCD as a trading signal directly** — only as a monitoring / gating signal. Trading on "a regime just started" is a recipe for late entries. Use BOCD to pause, not to enter.

---

## 5. Copula-based joint probability modeling

### What it is

Sklar's theorem (1959) states that any joint distribution `F(x₁, ..., x_n)` can be decomposed as `C(F₁(x₁), ..., F_n(x_n))` where `F_i` are marginal CDFs and `C` is a **copula** — a joint CDF on `[0,1]^n` with uniform marginals. Copulas encode dependence separately from marginals. The two most useful families:

- **Gaussian copula**: `C(u) = Φ_Σ(Φ⁻¹(u₁), ..., Φ⁻¹(u_n))`. One correlation matrix Σ captures all dependence. No tail dependence (probability of simultaneous extremes vanishes).
- **Student-t copula**: same as Gaussian but with t marginals in the latent Gaussian space. Has tail dependence via the ν (df) parameter.

For binary outcomes, the copula structure lets you write `P(Y_1=1, Y_2=1) = C(p_1, p_2)` directly, preserving each marginal probability.

### Why it matters for Kalshi

Today, the cluster-cap in `cluster_limiter.cpp` handles correlation by a hard allocation rule: "weather cluster ≤ 50%." Blunt. If you hold long-YES positions on "NYC > 75°F," "DC > 75°F," "Boston > 75°F" on the same day, the hard cap treats them as 100% correlated. They are closer to 0.6.

Copulas let you **price the joint directly**, compute joint P&L distributions, and feed into multi-bet Kelly (sister doc §9.8) with true scenario weights rather than the naive correlation-matrix diagonal. Effect size at $100: modest (cluster cap is a 20% sizing haircut, the copula gets you maybe 10% better sizing). But it removes a hack and sets up cleanly for Phase 2 crypto markets where copulas matter more.

### Math sketch

**Gaussian copula**, fit from historical weather residuals:
```
1. For each pair of contracts (i, j), compute historical joint (y_i, y_j) pairs  
   across past settlements of "similar" contracts.
2. For each series, compute the probability integral transform:
     u_i = rank(y_i) / (n+1)            (empirical CDF at each point)
3. Gaussian-space:
     z_i = Φ⁻¹(u_i)
4. Estimate Σ from sample correlations of z.
5. For a new prediction:
   - Marginal probs p_1, ..., p_n (from model).
   - z_i* = Φ⁻¹(p_i).
   - Joint P(all YES) = Φ_Σ(z_1*, ..., z_n*).
```

**For bivariate `P(both YES)`** with Gaussian copula, ρ:
```
P(Y_1=1, Y_2=1) = BVN(Φ⁻¹(p_1), Φ⁻¹(p_2); ρ)
```
where `BVN` is the bivariate normal CDF. Closed-form via Drezner-Wesolowsky or Genz's algorithm.

**Multi-bet Kelly with copula scenarios**: enumerate `2^n` joint outcomes, compute each outcome's Kelly-weighted growth, solve the concave program:
```
for (y_1, ..., y_n) ∈ {0,1}^n:
    P(y) = C-derived joint probability
    payoff_i(y) = (1/c_i − 1) if y_i else −1                  (on the YES leg)
max_f  Σ_y P(y) · log(1 + fᵀ · payoff(y))
s.t.   Σ f ≤ 1, f ≥ 0
```
For `n ≤ 6` this is tractable (64 scenarios, each a linear expression).

### Practical recipe

- **Fit Σ offline** in Python from 2-3 years of weather-contract resolutions; export as a JSON matrix per category-pair.
- **C++ side**: read Σ; for each new multi-bet decision, compute joint probs for each 2^n scenario in closed form (or via `boost::math::bivariate_normal_cdf` for n=2, Genz for n≥3).
- **Feed scenarios into the CVXPY / ECOS multi-bet Kelly solver** (sister doc §9.8).
- Target: n ≤ 5 simultaneous bets per cluster. Beyond that, the exponential blowup dominates and you need a different scheme (Monte Carlo integration of the copula).

**Library candidates**:
- Python offline: `copulas` (<https://github.com/sdv-dev/Copulas>), `pyvinecopulib`, `scikit-learn` joint Gaussian.
- C++: `boost::math` has `bivariate_normal_cdf`; Genz's MVN algorithm is public-domain Fortran, wrapped in `boost::math::quadrature` or call `stats::pmvnorm` via Python sidecar if truly higher-dimensional.

### Citations

- Sklar 1959, "Fonctions de répartition à n dimensions et leurs marges" (Publ. Inst. Statist. Univ. Paris 8) — original.
- Joe 2014, *Dependence Modeling with Copulas* — CRC Press. Reference.
- MacKenzie & Spears 2014 — *The Formula That Killed Wall Street: The Gaussian Copula and the Material Cultures of Modelling* — sober cautionary read; <https://journals.sagepub.com/doi/10.1177/0306312713499781>.
- Embrechts, Lindskog, McNeil 2001, "Modelling Dependence with Copulas and Applications to Risk Management": <https://people.math.ethz.ch/~embrecht/ftp/copchapter.pdf>
- Genz 1992, "Numerical Computation of Multivariate Normal Probabilities" — the workhorse algorithm: <http://www.math.wsu.edu/faculty/genz/papers/mvn.pdf>
- Python `copulas` library: <https://github.com/sdv-dev/Copulas>

### When to skip

**Skip if your cluster has fewer than 3 bets.** Bivariate correlation is enough; fit `ρ` from historical pairs and use the hard cluster-cap as a 1.5× haircut on `f_independent`.

**Skip Gaussian copula for crypto / VIX / fat-tail series.** Its zero tail-dependence will underestimate joint crashes. Use t-copula (extra `ν` parameter) with `ν ≈ 5-8` for equity / crypto correlated events.

**Skip if you do not have stationary joint data.** Copula fits need historical co-occurrences; at $100 you have few. Rely on engineering heuristics (same-day same-metro weather = ρ ≈ 0.7) for the first year.

---

## 6. Monte Carlo improvements

### What it is

Three upgrades to naive uniform Monte Carlo, all of which lower variance for the same sample count:

- **Quasi-Monte Carlo (QMC) — Sobol / Halton sequences.** Deterministic low-discrepancy sequences that fill the unit cube more evenly than pseudo-random. Convergence rate `O(N⁻¹ · (log N)^d)` beats MC's `O(N⁻¹/²)` for `d < ~10`.
- **Importance sampling.** Sample from a proposal `q` instead of target `p`, reweight by `p/q`. Dramatically cuts variance for tail probabilities (a tail event might need 10⁶ MC samples but 10³ IS samples).
- **Stratified sampling.** Partition the sample space into strata, draw proportionally from each. Guaranteed variance reduction vs IID MC.

Sister doc §9.4 uses naive MC for Bayesian Kelly integration. This is the single easiest wall-clock improvement in the stack.

### Why it matters for Kalshi

The Bayesian Kelly calculation (sister doc §9.4) integrates `log(1 + f · X(p))` over a Beta posterior. The integrand is smooth. QMC via Sobol converges 10-100× faster than pseudo-random MC for the typical `d = 1` (Beta) or `d = 3` (Dirichlet) posteriors used here.

Concrete: instead of 10,000 Beta samples to get 0.1% noise in the Kelly estimate, use 500 Sobol points — 20× faster, critical if you re-run Kelly per-tick on 100 markets.

### Math sketch

**Sobol-based integration of a 1-D posterior:**
```
// Want: E[log(1 + f · X(p))] where p ~ Beta(α, β)
u_n = n-th Sobol point in [0, 1]^1
p_n = Beta_inv_cdf(u_n; α, β)              // transform
X_n = p_n · (1 − c)/c − (1 − p_n)
I_N = (1/N) Σ log(1 + f · X_n)
```

**Importance sampling for a tail event** (e.g. "probability VIX > 50"):
```
p(x)            = true density
q(x)            = proposal (heavier tail, e.g. t_3 vs N(0,1))
IS estimate     = (1/N) Σ [1{x_n > 50} · p(x_n)/q(x_n)]   where x_n ~ q
Optimal q(x)    ∝ |f(x)| · p(x)            (zero-variance; unreachable)
```
Practical rule: make `q` heavier-tailed than `p` by a factor of 2-3; check ESS `(Σw)² / Σw²` ≥ 0.5 · N.

**Stratified sampling** of a Beta posterior for Kelly:
```
Divide [0, 1] into M equal-probability strata using Beta quantiles.
Sample n_m IID from stratum m; combine with weight 1/M.
Variance ≤ IID variance (Rao-Blackwell).
```

### Practical recipe

**Sobol via `boost::math::quasi_random` or Broda** (commercial, free for research):

```cpp
#include <boost/random/sobol.hpp>
// 1-D Sobol for Beta sampling
boost::random::sobol gen(1);          // dim=1
auto sample_beta = [&](double a, double b){
    double u = gen() / (double)std::numeric_limits<uint32_t>::max();
    return boost::math::ibeta_inv(a, b, u);
};
double I = 0;
for (int n = 0; n < N; ++n) {
    double p  = sample_beta(alpha, beta_);
    double X  = p * (1 - c)/c - (1 - p);
    I += std::log(1 + f * X);
}
I /= N;
```

**Scrambled Sobol** (Owen 1998) reduces pathology in the first points; use `scipy.stats.qmc.Sobol(d, scramble=True)` in Python.

**Importance sampling** — for tail integrals (e.g., `P(drawdown > 50%)`):
- Proposal: `f*` shifted by the tail direction.
- Keep `log(weights)` for numerical stability.
- Monitor ESS; refit proposal if ESS < 0.3 · N.

**Stratified sampling** is 15 LOC. Use it when Sobol is overkill.

**Library**: `scipy.stats.qmc` for Python (Sobol, Halton, Latin hypercube). C++: `boost::random::sobol`, or Broda SobolGenerator.

### Citations

- Niederreiter 1992, *Random Number Generation and Quasi-Monte Carlo Methods* — classic reference.
- Owen 1998, "Scrambling Sobol' and Niederreiter-Xing points" (J. Complexity 14): <https://artowen.su.domains/reports/siopt.pdf>
- Glasserman 2003, *Monte Carlo Methods in Financial Engineering* — Springer. Chapters 5-7 cover IS, stratification, and QMC for option pricing.
- Broda Sobol whitepaper: <https://www.broda.co.uk/doc/WP-178-01-Algorithmics-Broda%20Sobol%20Whitepaper.pdf>
- `scipy.stats.qmc`: <https://docs.scipy.org/doc/scipy/reference/stats.qmc.html>

### When to skip

**Skip IS for smooth non-tail integrals.** For `E[log(1 + f · X)]` where `p ∈ (0.2, 0.8)`, naive MC at N=5000 is already noise-free at 0.1%. IS shines only for `P < 0.01` events.

**Skip QMC if your dimensionality is ≥ 10.** Low-discrepancy sequences lose their edge quickly past `d ~ 10` (the `(log N)^d` factor kicks in). For a 15-D copula integral, use MCMC (NUTS via Stan) or plain MC with N=100k.

**Do not write your own Sobol generator.** Use Boost or SciPy. Hand-rolled Sobol with the wrong direction numbers silently gives biased results.

---

## 7. Information-theoretic sizing and diagnostics

### What it is

Use information-theoretic quantities as bet-selection and diagnostic signals:

- **KL divergence** `D(p ∥ q) = Σ p log(p/q)` between model `p` and market `q` — quantifies expected extra surprise from treating the market as correct when your model is correct.
- **Log score** (aka log loss) `−log p̂(y_obs)` — a proper scoring rule. Strictly more peaked than Brier on confident-and-wrong bets.
- **Expected information gain (EIG)** `I(Y; outcome)` for market selection — pick markets where the outcome resolves the largest amount of your posterior uncertainty.

Kelly growth rate for a two-outcome bet with true prob `p` and market odds `q` equals `D(p ∥ q)` when bet-sized optimally. This is not incidental — Kelly *is* an information-theoretic object.

### Why it matters for Kalshi

Three concrete uses:

1. **Early-warning diagnostic.** `D(market ∥ model)` spiking in a category means either the model or the market is drifting. Log a rolling-mean and fire an alert when the current value is >3× its long-run average. Simpler than PSI, more interpretable. (Sister doc §8.8 mentions KS and PSI; KL sits alongside them.)

2. **Market-selection filter.** For a universe of 200 listed contracts, rank by `|D(model ∥ market)|` and only evaluate / trade the top 10% by surprise. Concentrates compute and cuts transaction costs.

3. **Log loss > Brier for optimization.** Brier underweights extreme-confidence misses; log loss does not. The existing `probability_calibrator.cpp` should optimize log loss, report Brier. Swap in 10 minutes.

### Math sketch

**KL divergence binary:**
```
D(p ∥ q) = p log(p/q) + (1-p) log((1-p)/(1-q))
```

**Growth rate of a Kelly bet at edge** (Cover-Thomas 2006, *Elements of Information Theory*, §6):
```
g(f*) = E_p[log(payoff ratio)] = D(p ∥ q)
```
— exactly the KL divergence. Bigger surprise = faster growth (when bet-sized correctly).

**Log score:**
```
ℓ_log(p̂, y) = −[y log p̂ + (1-y) log(1-p̂)]
```
Strictly proper, unbounded at `p̂ → 0` with `y = 1`. Calibrators optimized under log loss give better tail behavior than under Brier.

**Expected information gain** for a binary contract about underlying variable X:
```
EIG(X) = H(X) − E_Y[H(X | Y)]
       = H(X) − P(Y=1)·H(X|Y=1) − P(Y=0)·H(X|Y=0)
```
Pick the contract that maximally reduces posterior entropy of the thing you care about. Mostly useful for combinatorial markets (e.g. "which of {CPI, PPI, PCE} releases do I watch?") where you cannot trade everything.

### Practical recipe

**Add three columns to the calibration DB schema**:

```sql
ALTER TABLE predictions ADD COLUMN kl_market REAL;  -- D(model || market)
ALTER TABLE predictions ADD COLUMN log_loss REAL;
ALTER TABLE predictions ADD COLUMN brier REAL;
```

Compute both at prediction time and at settlement. Then:

```cpp
double kl_divergence_binary(double p_model, double p_market) {
    auto clamp = [](double x){ return std::clamp(x, 1e-6, 1.0 - 1e-6); };
    p_model = clamp(p_model); p_market = clamp(p_market);
    return p_model * std::log(p_model / p_market)
         + (1-p_model) * std::log((1-p_model) / (1-p_market));
}
```

**Market-selection filter**:
```cpp
// Among 200 listed markets, pick the top 20 by |KL(model || market)|
// and only run the expensive decision pipeline on those.
std::sort(markets.begin(), markets.end(),
    [](auto& a, auto& b){ return std::abs(a.kl) > std::abs(b.kl); });
markets.resize(20);
```

**Alert rule**: when rolling 20-bet mean of `KL(market || model)` exceeds 3× long-run mean in a category, pause trading in that category.

### Citations

- Cover & Thomas 2006, *Elements of Information Theory*, 2nd ed. — Ch. 6 "Gambling and Data Compression" derives Kelly = KL.
- Kelly 1956, "A New Interpretation of Information Rate" (Bell System Technical Journal): <https://www.princeton.edu/~wbialek/rome/refs/kelly_56.pdf> — the original, remarkably readable.
- Gneiting & Raftery 2007, "Strictly Proper Scoring Rules, Prediction, and Estimation" (JASA): <https://stat.washington.edu/people/raftery/Research/PDF/Gneiting2007jasa.pdf>
- Lindley 1956 on expected information gain (Annals of Math Stat) — classic.

### When to skip

**Skip EIG (information-gain market selection) for $100 capital.** The combinatorial-info-gain framing earns its keep at $10k+ when you need to allocate attention across hundreds of markets. At $100, just rank by absolute edge.

**Do not switch away from Brier for reporting.** Log loss has unbounded support; a single 0.001-probability hit dominates a week of Brier summary. Report Brier + log loss side by side.

---

## 8. Mean-reversion signals on Kalshi prices themselves

### What it is

Treat the Kalshi yes-price trajectory `p_t` for a single market as a time series, and ask: is there exploitable mean-reversion? Two workhorses:

- **Ornstein-Uhlenbeck (OU) process**: `dp = θ(μ − p) dt + σ dW`. Fit `θ, μ, σ`; half-life = `log(2)/θ`.
- **Variance ratio test** (Lo-MacKinlay 1988) — is the variance of `k`-period returns `k` times the variance of 1-period returns? Reject → mean-reverting (ratio < 1) or trending (ratio > 1).

If Kalshi mispricings mean-revert within the market lifetime, a pure statistical-arb strategy (buy when `p_t < μ − 1σ`, sell at `μ`) may outperform the fundamental-model approach, or combine with it.

### Why it matters for Kalshi

Kalshi books are often thin, retail-heavy, and slow. A headline that briefly moves price 5% away from "truth" may revert as limit-order-book liquidity slowly fills. If mean-reversion is real and half-life is shorter than time-to-settlement, you have a cheap signal.

Caveat: "mean" on a prediction market is time-varying because information arrives. A fixed OU `μ` is wrong. The useful version is OU on `p_t − fundamental_t` where `fundamental_t` is your model — i.e., the residual between market and model. If the residual is OU with short half-life, you have a pairs-trading-style signal.

### Math sketch

**OU MLE** (discrete):
```
Δp_t = θ · (μ − p_{t-1}) · Δt + σ · √Δt · ε_t
```
Regress `Δp_t` on `p_{t-1}`: intercept = `θ μ Δt`, slope = `−θ Δt`, residual std = `σ √Δt`.

```
half_life = log(2) / θ
```
Typical interpretation: if `half_life < time_to_settlement / 2`, reversion is exploitable.

**Variance ratio test:**
```
VR(k) = Var(r_k) / (k · Var(r_1))
```
Where `r_k = p_t − p_{t-k}`. `VR = 1` under random walk; `< 1` = mean-reverting; `> 1` = trending. Use Lo-MacKinlay 1988 heteroskedasticity-robust test statistic.

### Practical recipe

**First: does it exist?** Download 2 years of candle history (new endpoint `GET /historical/markets/{ticker}/candlesticks`, sister doc §2.2). For 100 settled markets per category, fit OU on `p_t − model_t` residuals. Report distribution of half-lives.

- If median `half_life > market_duration`, reversion is not usable. Stop.
- If median `half_life < market_duration / 3`, consider a reversion signal.

**Python prototype, 30 LOC:**

```python
import numpy as np
from statsmodels.regression.linear_model import OLS

def fit_ou(prices, dt=1/1440):   # 1-min candles, dt in days
    dp = np.diff(prices)
    p_lag = prices[:-1]
    X = np.column_stack([np.ones(len(p_lag)), p_lag])
    res = OLS(dp, X).fit()
    theta_mu_dt, neg_theta_dt = res.params
    theta = -neg_theta_dt / dt
    mu = theta_mu_dt / (theta * dt) if theta > 0 else np.mean(prices)
    half_life_days = np.log(2) / theta if theta > 0 else np.inf
    return theta, mu, half_life_days
```

**Signal rule**: if residual `p_t − model_t` crosses −1σ (from fit), place a small YES long; exit at μ or at settlement (whichever first). Cap at 2-5% of bankroll per reversion signal.

### Citations

- Uhlenbeck & Ornstein 1930, "On the theory of the Brownian motion" (Physical Review).
- Lo & MacKinlay 1988, "Stock Market Prices Do Not Follow Random Walks" (Review of Financial Studies): <https://www.nber.org/papers/w2168>
- Aït-Sahalia 2002, "Maximum likelihood estimation of discretely sampled diffusions" — closed-form transitional densities.
- Hudson & Thames tutorial on OU half-life: <https://hudson-and-thames-arbitragelab.readthedocs-hosted.com/en/latest/cointegration_approach/half_life.html>

### When to skip

**Skip for $100 capital.** Mean-reversion arbitrage on Kalshi needs:
- Reliable intra-day data per market (only now available via Mar 6 2026 endpoint).
- Enough bankroll to place 0.5% exposure per signal across 50+ markets simultaneously.
- Execution at mid-price or better — but Kalshi spreads are 2-40%, so you pay more crossing the spread than the reversion captures.

This is a 2028 topic for a 5-figure bankroll. Until then, use reversion only as a **feature** to the main model: "did the price move away from our fundamental estimate? Weight that in the sizer."

---

## 9. Survival / hazard models for time-to-event contracts

### What it is

Statistical models for "does event X happen by time Y." Three families:

- **Kaplan-Meier (KM) estimator** — nonparametric empirical survival function from censored data.
- **Cox proportional hazards (Cox 1972)** — semi-parametric regression on the hazard function `h(t | x) = h_0(t) · exp(βᵀx)`. Classic in medical statistics; generalizes cleanly to prediction markets.
- **Parametric hazards** — Weibull (monotone hazard), log-normal (unimodal), Gompertz (exponentially-growing hazard). Use when you have strong prior on the shape.

"Will there be a cat-3+ hurricane by Sept 15" is a perfect survival-analysis question, not a binary classification.

### Why it matters for Kalshi

Kalshi has entire categories structured as "does X occur by date Y":
- "Hurricane Ian-strength storm in Gulf of Mexico by Sep 30?"
- "Fed rate cut by March meeting?"
- "CPI YoY print under 2.5% in any month in Q2?"

Each is a survival question. Today the bot likely treats them as one-shot binary classifications. A hazard model uses the full history of *when* similar events occurred in past years — the KM curve for hurricane strikes 2000-2024 is directly the reference prior.

Second benefit: **daily update without re-modeling**. As each day passes without the event, the conditional survival probability `S(t | t > today)` is the KM curve restricted to the future. Recompute in O(1).

### Math sketch

**Kaplan-Meier**:
```
S_hat(t) = ∏_{t_i ≤ t} (1 − d_i / n_i)
```
`d_i` = failures at `t_i`, `n_i` = at-risk count just before `t_i`.

**Cox model**:
```
h(t | x) = h_0(t) · exp(β₁ x₁ + ... + β_p x_p)
```
`h_0(t)` left unspecified. Partial likelihood:
```
L(β) = ∏_{i: event at t_i} exp(βᵀ x_i) / Σ_{j ∈ risk set} exp(βᵀ x_j)
```
Newton-Raphson; `β` hat asymptotically normal.

**Weibull hazard**:
```
h(t) = (γ/λ)(t/λ)^(γ−1),  S(t) = exp(−(t/λ)^γ)
```

### Practical recipe

**Hurricane category** is the obvious application:
1. Pull HURDAT2 (sister doc §3 references) — 1900-2025 hurricane records.
2. For each historical year, record time-to-first-cat-3 hitting Gulf coast.
3. Fit a Weibull hazard with covariates: ENSO state, SST anomaly, MJO phase. (Simple Cox model.)
4. Current-year prediction: integrate the model's hazard from today to contract end date → `1 − S(contract_end | today)` = yes probability.

**Code**: use `lifelines` Python sidecar; no good C++ Cox implementation exists.

```python
from lifelines import CoxPHFitter
cph = CoxPHFitter()
cph.fit(df, duration_col='days_to_first_cat3', event_col='event',
        formula='enso_nino34 + sst_anom + mjo_phase')
cph.predict_survival_function(new_covariates, times=[today, end_date])
```

Export survival function samples as JSON; load from C++ trading side.

**For Fed-rate-cut-by-date** contracts, a parametric hazard makes less sense (too few events) — use the CME FedWatch implied probs from sister doc §5 directly.

### Citations

- Cox 1972, "Regression Models and Life-Tables" (JRSSB 34): <https://www.jstor.org/stable/2985181>
- Kaplan & Meier 1958, "Nonparametric estimation from incomplete observations" (JASA 53).
- Kleinbaum & Klein 2012, *Survival Analysis: A Self-Learning Text*, 3rd ed. — best introductory textbook.
- `lifelines` Python library (definitive): <https://lifelines.readthedocs.io/>
- `scikit-survival`: <https://scikit-survival.readthedocs.io/>

### When to skip

**Skip if the event is one-shot with a specific release date** (CPI March print, FOMC decision). Survival analysis is for *counting-process* events: hurricanes, earthquakes, "first X by Y." For a fixed-schedule binary outcome, use classification.

**Skip Cox regression at $100 capital** unless you have 50+ historical events in the category. Cat-3+ hurricanes: 30-40 in HURDAT2 per relevant geography — marginal, but okay. CPI prints > 2.5% in a given month: 240+ monthly records since 2000 — fine.

**Do not implement Cox in C++.** The Breslow tie-handling and partial-likelihood optimization are subtle. Python sidecar, full stop.

---

## 10. Heston / rough-vol / SABR extensions for options-implied priors

### What it is

Sister doc §5.2 covers SVI parametrization. The richer alternatives:

- **Heston (1993)** — stochastic-volatility diffusion: `dv = κ(θ − v)dt + ξ√v dZ`. FFT-priced via Carr-Madan. 5 params, calibrates the whole surface.
- **Rough Bergomi / rough-vol** (Bayer-Friz-Gatheral 2016) — `log v_t` is fractional Brownian motion with Hurst `H ≈ 0.1`. Matches empirical realized-vol roughness; reproduces the short-maturity vol smile better than any Markov stochastic-vol model. Pricing: Monte Carlo under a fractional Wiener process.
- **Shifted SABR / ZABR** — extensions for low-rate environments where Hagan's formula breaks.

### Why it matters for Kalshi

Honestly? Mostly doesn't. Kalshi option-related contracts (SPX-above-X, VIX-above-X) are resolution-at-expiry digital options, and for mid-strike short-dated resolutions, SVI is fine. The sister doc §5 recipe — SVI + Breeden-Litzenberger — dominates.

Two edge cases where Heston / rough-vol *might* matter:

1. **Long-dated Kalshi macro tail contracts.** "S&P closes under 3000 at any point in 2026" depends on the vol-of-vol term structure. Heston captures this; SVI (per-expiry) does not.
2. **Crypto contracts with very short-dated resolution** (e.g., "BTC above 100k at 4pm tomorrow"). Realized-vol roughness matters at sub-daily horizons. But the bot probably should not be trading crypto micro-horizon contracts at $100 capital.

### Math sketch

**Heston**:
```
dS = r S dt + √v S dW₁
dv = κ(θ − v) dt + ξ √v dW₂
corr(W₁, W₂) = ρ
```
Call price: inverse-FFT of a known characteristic function. 5 params: `(v_0, κ, θ, ξ, ρ)`.

**rough Bergomi**:
```
v_t = ξ_0(t) · exp(η · W_t^H − 0.5 η² · t^{2H})
```
`W_t^H` = Riemann-Liouville fractional Brownian motion with Hurst `H ≈ 0.1`. No Markov embedding (not a diffusion).

### Practical recipe

**If you absolutely need this**: `QuantLib` (<https://www.quantlib.org/>) has full Heston with FFT pricing and Andersen's QE scheme for MC. Python wrapper: `QuantLib-Python`. ~50 LOC to calibrate a Heston surface.

For rough vol: `rbergomi` Python package (<https://github.com/ryanmccrickerd/rough_bergomi>). This is research code. Do not port.

### Citations

- Heston 1993, "A Closed-Form Solution for Options with Stochastic Volatility" (RFS 6).
- Bayer, Friz, Gatheral 2016, "Pricing under rough volatility" (Quantitative Finance 16): <https://papers.ssrn.com/sol3/papers.cfm?abstract_id=2554754>
- Hagan et al. 2002 SABR: see sister doc §5.2.
- QuantLib: <https://www.quantlib.org/>
- Gatheral 2006, *The Volatility Surface* — book.

### When to skip

**Skip for this bot — at minimum until 2027.** The marginal improvement from Heston-over-SVI on Kalshi-relevant digital pricing is under 1% of edge. The implementation is weeks. Not worth it.

Rough-vol is a genuinely new area of research — important for equity vol-of-vol — but has zero current Kalshi contract surface that would benefit. Revisit only if Kalshi lists a term-structure product.

---

## 11. Bandit algorithms for category exploration

### What it is

Multi-armed bandits: at each round pick one of `K` arms, observe reward, minimize regret. Key algorithms:

- **UCB1** (Auer-Cesa-Bianchi-Fischer 2002) — `score_k = μ̂_k + √(2 log t / n_k)`. Exploration bonus shrinks as arm is pulled.
- **UCB-V** — UCB with empirical variance, tighter for low-variance arms.
- **Thompson sampling** (Thompson 1933, Chapelle-Li 2011) — sample `p ~ posterior_k`, pick arm with max sample.
- **LinUCB** (Li et al. 2010) — contextual: `arm = argmax θ̂_kᵀ x + α √(xᵀ A_k⁻¹ x)`.

Sister doc §9.12 mentions Thompson and UCB briefly for exploration-exploitation. This section goes deeper: which bandit for which decision in this bot?

### Why it matters for Kalshi

Three distinct bandit decisions live in this bot:

1. **Category allocation.** Given K categories (weather-temp, weather-hurricane, CPI, NFP, Fed, entertainment), how much of your bankroll to allocate to each? Non-stationary: weather-hurricane is seasonal. Arms here = categories; reward = realized Sharpe over a rolling window.
2. **Within-category market picking.** Given 20 temperature contracts today, trade all / half / one? Arms = subset selection strategies.
3. **Model choice per category.** Ensemble, QRF, Cleveland Fed alone — which to bet on? See §3 (Hedge) — when rewards are observable for all experts, use Hedge; when you can only evaluate one per round, use EXP3 (or Thompson).

For decisions (1) and (2), **Thompson sampling with Beta-Bernoulli prior** is the opinionated default:

- Straightforward to implement: per-category prior `Beta(α_k, β_k)`, update `α_k += wins, β_k += losses` after settlement, pick category by sample from posterior.
- Chapelle-Li 2011 empirically beats UCB in industry deployment.
- Naturally non-stationary if you decay `(α_k, β_k)` over time.

### Math sketch

**UCB1**:
```
at round t, pick arm argmax_k  μ̂_k + √(2 ln t / n_k)
```

**Thompson sampling (Beta-Bernoulli)**:
```
for each round:
  for each arm k:
    θ_k ~ Beta(α_k, β_k)       // sample posterior mean
  play arm argmax θ_k
  observe r (binary win/loss)
  α_k += r;  β_k += (1-r)
```

**LinUCB** — context vector `x_t`:
```
A_k = I_d (init)
b_k = 0   (init)
for each round:
  θ̂_k   = A_k⁻¹ b_k
  ucb_k  = θ̂_kᵀ x_t + α · √(x_tᵀ A_k⁻¹ x_t)
  play k* = argmax ucb_k
  observe r_t
  A_{k*} += x_t x_tᵀ
  b_{k*} += r_t x_t
```
α usually 1.0-2.0.

### Practical recipe

**Category-level Thompson — 40 LOC, reuses `AdaptiveSizer` infrastructure.**

```cpp
struct ThompsonArm {
    double alpha = 1.0, beta = 1.0;   // Beta(1,1) uniform prior
    double decay = 0.995;             // per-settlement decay
    void observe(bool win) {
        alpha *= decay; beta *= decay;    // forget old
        if (win) ++alpha; else ++beta;
    }
    double sample(std::mt19937& rng) const {
        std::gamma_distribution<double> g_a(alpha, 1.0), g_b(beta, 1.0);
        double a = g_a(rng), b = g_b(rng);
        return a / (a + b);
    }
};
```

- Runs alongside `AdaptiveSizer`. TS picks "which category gets the next dollar"; AdaptiveSizer picks "given category, how much per bet."
- Use `win` = true if realized_pnl > 0 in that bet. Thompson is the category-exploration budget.
- Every day, draw one sample from each category; allocate the day's trading budget proportionally to sample values (softmax with temperature).

**LinUCB for within-category market selection** — context = (edge, Venn-Abers interval width, time-to-settlement, market volume). Probably overkill until you have 2k+ labeled trades per category. Save for 2027.

### Citations

- Thompson 1933, "On the likelihood that one unknown probability exceeds another" (Biometrika 25): <https://www.jstor.org/stable/2332286>
- Auer, Cesa-Bianchi, Fischer 2002, "Finite-time Analysis of the Multiarmed Bandit Problem" (ML): <https://link.springer.com/article/10.1023/A:1013689704352>
- Chapelle & Li 2011, "An Empirical Evaluation of Thompson Sampling" (NIPS): <https://papers.nips.cc/paper/4321-an-empirical-evaluation-of-thompson-sampling>
- Li, Chu, Langford, Schapire 2010, "A Contextual-Bandit Approach to Personalized News Article Recommendation": <https://arxiv.org/abs/1003.0146>
- Russo et al. 2018, "A Tutorial on Thompson Sampling": <https://arxiv.org/abs/1707.02038>
- Lattimore & Szepesvári 2020, *Bandit Algorithms* — free online: <https://tor-lattimore.com/downloads/book/book.pdf>

### When to skip

**Skip LinUCB.** Contextual bandits earn their keep at scale (1000+ arms, continuous features, millions of trials). Your bot has ~10 categories, ~200 markets/day. Non-contextual Thompson is strictly better at this scale.

**Skip UCB1 in favor of Thompson.** UCB was state-of-the-art in 2002; Thompson is the 2026 default. Same compute, better empirical performance per Chapelle-Li.

**Do not stack bandits and Hedge on the same decision.** If model-selection runs through Hedge, and category-allocation runs through Thompson, great — they are orthogonal. If you try "bandit of bandits," you will accumulate so much exploration noise that your $100 evaporates to fees before any learning happens.

---

## 12. Meta-labeling / secondary classifier

### What it is

López de Prado's (2018) *Advances in Financial Machine Learning* Ch. 3 introduces meta-labeling: a **primary** model generates trading signals (buy/sell/hold); a **secondary** binary classifier predicts whether each primary signal will be profitable, and the predicted probability sizes the bet.

Structure:
```
Primary model:    outputs side (buy/sell) + raw trigger
Secondary model:  inputs [primary's features ∪ primary's raw prob ∪ regime features]
                  target = 1 if primary's next bet wins, 0 otherwise
                  output = meta-probability p_meta
Final bet size:   proportional to p_meta (e.g., Kelly with p = p_meta)
```

Key property: meta-labeling strictly increases precision at the cost of recall. You take *fewer* bets, but more of them win. This is precisely the trade a $100 trader wants: variance reduction > quantity.

### Why it matters for Kalshi

The current stack has a primary model (`probability_engine.cpp`) that produces `p_model` per contract. `EdgeDetector` signals a bet when `p_model > p_market + threshold`. Meta-labeling adds:

- A secondary classifier (LightGBM on CV folds) whose target is "did the `EdgeDetector` signal win?"
- Inputs: everything the primary used, plus `edge`, `Venn-Abers width`, `market volume`, `time-to-settlement`, `category regime flags`.
- Output: `p_meta ∈ [0,1]` — calibrated confidence in the primary signal.
- Final sizing: multiply Kelly fraction by `p_meta` (or use `p_meta` as the probability in Kelly directly, with `market_price` as the "fair" prob — a second-stage Bayesian Kelly).

Expected lift: 5-15% increase in out-of-sample Sharpe per López de Prado's published backtests on trend-following strategies. Event contracts have similar structure (binary outcome, clean label).

### Math sketch

```
Primary:     p_primary(x) ∈ [0,1]
             bet_trigger = p_primary > p_market + τ
Secondary:   p_meta(x, p_primary, regime) ∈ [0,1]  // trained on {bet_trigger fires → win/loss}
Sized Kelly: f_final = p_meta · f_Kelly(p_primary, c)
```

Alternative (equivalent under some assumptions): use `p_meta` as the shrinkage weight in a Bayesian-Kelly combination of `p_primary` and `p_market`.

### Practical recipe

1. **Gate the primary** by an unchanged `EdgeDetector` — no changes to the existing pipeline.
2. **Log all `bet_trigger = true` observations**, with full feature vector + outcome.
3. After 300 resolved trades per category: **train LightGBM secondary** on `{features → did_win}`.
4. Cross-validate with **purged + embargoed CV** (sister doc §12.3) to avoid leak.
5. Deploy: scale bet size by `p_meta`.

**Sidecar, ~80 LOC Python**:

```python
import lightgbm as lgb
import pandas as pd
from sklearn.model_selection import TimeSeriesSplit

df = pd.read_sql("SELECT * FROM bet_triggers WHERE resolved IS NOT NULL", conn)
X = df[['p_primary','edge','vaap_width','market_vol','ttl_hours','category_weather',...]]
y = df['won'].astype(int)
cv = TimeSeriesSplit(n_splits=5)
model = lgb.LGBMClassifier(objective='binary', n_estimators=300,
                           learning_rate=0.02, num_leaves=31)
# Purge: for each train fold, drop rows whose resolution date is within 1 day of any test row
# (simplified — for full purged+embargoed see Lopez de Prado 2018 ch. 7)
```

Export as JSON (tree dump); load from C++ with `lightgbm::predict`. Or just call the Python sidecar from C++.

### Citations

- López de Prado 2018, *Advances in Financial Machine Learning*, Ch. 3 "Labeling" + Ch. 7 "Cross-Validation in Finance". Wiley.
- Wikipedia Meta-Labeling: <https://en.wikipedia.org/wiki/Meta-Labeling>
- Singh & Joubert (Hudson & Thames) 2022, "Does Meta-Labeling Add to Signal Efficacy?": <https://hudsonthames.org/wp-content/uploads/2022/04/Does-Meta-Labeling-Add-to-Signal-Efficacy.pdf>
- `mlfin.py` (open-source port of *AFML*): <https://mlfinpy.readthedocs.io/>

### When to skip

**Skip if you have <200 resolved trades per category.** The secondary model needs enough positive / negative examples to avoid overfitting. Below 200, it memorizes your 10 most recent losses.

**Skip if the primary is already calibrated (post Beta + IVAP).** Meta-labeling partially overlaps with calibration: a well-calibrated primary has `p_primary = p_meta`. The secondary's uplift comes from features the primary does NOT see — typically market-microstructure (volume, spread, time-to-settle). If your primary already uses those, the meta-model has nothing to learn.

**Watch for data leakage.** This is the biggest foot-gun: the secondary sees the primary's output, which is a function of the label. Always use purged CV. Do not cut corners. A leaky meta-model will look amazing in backtest and bleed money live.

---

## 13. Online calibration-preserving ensembles

### What it is

Maintaining a calibrator online as new settlements arrive, without full recompute:

- **Online isotonic regression (PAVA)** — Kotłowski-Koolen 2016 show that the Exponential Weights algorithm over a covering net achieves `O(T^{1/3} log^{2/3} T)` regret for online isotonic regression, with a matching lower bound. In practice, a simpler "merge on violation" amortized-log update suffices. Given a new `(p, y)` pair, insert into sorted structure, then pool-adjacent-violators.
- **Online Venn-Abers.** Since IVAP is two isotonic queries, an online IVAP is just two online isotonics.
- **Sliding-window isotonic.** Recompute on a rolling 500-1000 sample window. Simpler than true online; loses exchangeability guarantee.

Sister doc §8.7 mentions "online isotonic" and "refit weekly on sliding window" as options. This section goes a level deeper on the actual maintenance algorithms.

### Why it matters for Kalshi

The existing `ProbabilityCalibrator` is disabled and trivial. Once real Beta calibration + Venn-Abers lands (sister doc §8 recommendation), keeping them current costs a non-trivial amount of CPU if done by full recompute every settlement. Online maintenance is:

- O(log n) per new observation for PAVA with a balanced tree.
- Memory-efficient: O(n) for the sorted structure.
- No cron, no batching — the calibrator reflects the just-settled prediction immediately.

### Math sketch

**PAVA as online merge** (simplified):
```
// maintain bins, each with (min_p, max_p, weighted_avg_y, n)
function insert(p, y):
    find bin containing p (or create new bin {p, p, y, 1})
    while last k bins have non-monotone y averages:
        merge the offending adjacent bins (weighted avg of y)
    // result: monotone sequence of bins
```

**Online Venn-Abers for a new test point s**:
```
// isotonic over calibration set ∪ {(s, 0)} -> p_0(s)
// isotonic over calibration set ∪ {(s, 1)} -> p_1(s)
// Kotłowski's bound: regret = O(T^{1/3} log^{2/3} T), matching lower bound
```

**Sliding-window refit**:
```
// trivial: maintain deque of last N settlements,
// recompute PAVA from scratch on insert (O(N log N))
// or amortize by re-running PAVA only every K inserts
```

### Practical recipe

**Sliding-window PAVA (opinionated default)**:
- Maintain `std::deque<std::pair<double, int>>` of last 1000 settlements.
- On insert: pop front, push back, call `pava()` on the whole deque. `pava()` is ~30 LOC.
- No real-time SLA — run in a separate thread, update the active calibrator atomically.
- Use exponential weights `w_i = 0.99^{i}` to down-weight old observations.

**True online PAVA (Kotłowski-Koolen)**:
- Interesting for theoretical completeness.
- In practice the constants dominate and sliding-window PAVA is simpler + comparable.
- Skip unless you are trading faster than 1 settlement/second.

**For online Venn-Abers**: the library `venn-abers` (Python) does batch IVAP. Online version is ~80 LOC on top of two online-isotonic instances. In C++, implement both at once (~150 LOC).

### Citations

- Kotłowski, Koolen, Malek 2016, "Online Isotonic Regression" (COLT): <https://arxiv.org/abs/1603.04190>
- Kotłowski et al. 2017, "Random Permutation Online Isotonic Regression" (NeurIPS): <https://papers.nips.cc/paper/7006>
- Vovk, Petej 2014, "Venn-Abers Predictors" — source for the batch algorithm.
- `venn-abers` Python: <https://pypi.org/project/venn-abers/>
- `scikit-learn` isotonic (batch reference): <https://scikit-learn.org/stable/modules/isotonic.html>

### When to skip

**Skip true online PAVA.** Sliding-window + exponential weights gets 95% of the benefit with 20% of the code. Online PAVA is a 2010s research topic, not a production reqt.

**Skip if refit-weekly is fine for your throughput.** If you settle 20 bets/week, batch Saturday-night recompute is perfectly adequate. Online pays only when settlements are continuous (intra-day markets, high volume).

---

## 14. Volatility & jump models for underlying series

### What it is

- **GARCH(1,1)** (Bollerslev 1986): `σ²_t = ω + α · r²_{t-1} + β · σ²_{t-1}`. Canonical volatility forecast; fitted by QMLE.
- **GJR-GARCH** (Glosten-Jagannathan-Runkle 1993): GARCH with asymmetric response to negative shocks — `+ γ · I{r_{t-1} < 0} · r²_{t-1}`. Captures "leverage effect."
- **Jump-diffusion** (Merton 1976): `dS = μS dt + σS dW + S(J-1)dN`, where `N` is Poisson and `log J ~ N(μ_J, σ_J²)`. Prices discontinuous moves.

### Why it matters for Kalshi

Mostly doesn't, per the summary table. Kalshi has very few *volatility-of-X* contracts. The main use is:

- **Volatility-regime flag.** Fit GJR-GARCH on S&P returns; if `σ_t > 2σ_long_run`, mark as high-vol regime and disable "S&P above X" or "VIX under Y" contracts.
- **Jump-diffusion-implied jump risk for crypto contracts.** If you trade "BTC above 100k by 4pm tomorrow," the Black-Scholes implied price from Deribit (sister doc §5.4) already has jumps baked into realized IV. A Merton jump-diffusion fit on BTC recent returns tells you whether the market's implied jump frequency is rising — a risk-off signal.

These are side signals, not the main act.

### Math sketch

**GJR-GARCH(1,1)** MLE:
```
r_t = μ + ε_t,  ε_t = σ_t z_t,  z_t ~ N(0,1)
σ²_t = ω + α · ε²_{t-1} + γ · I{ε_{t-1} < 0} · ε²_{t-1} + β · σ²_{t-1}
```
Constraint: `ω > 0, α, β, γ ≥ 0, α + β + 0.5γ < 1` (stationarity).

**Merton jump-diffusion** characteristic function (for calibration):
```
φ(u) = exp(T · [iu(μ − 0.5σ² − λκ) − 0.5u²σ² + λ(exp(iuμ_J − 0.5u²σ_J²) − 1)])
κ = exp(μ_J + 0.5σ_J²) − 1
```

### Practical recipe

**GJR-GARCH for S&P regime flag** — `arch` Python package:
```python
from arch import arch_model
m = arch_model(returns_pct, vol='Garch', p=1, o=1, q=1, dist='Normal').fit()
sigma_t = m.conditional_volatility.iloc[-1]
```
~5 LOC. Export `sigma_t` to C++ via JSON. Use as feature.

**Merton jump-diffusion calibration** — more work. `QuantLib` has it. Skip unless actually trading a contract whose payoff depends on tail jumps.

### Citations

- Bollerslev 1986, "Generalized Autoregressive Conditional Heteroskedasticity" (J. Econometrics): <https://www.sciencedirect.com/science/article/abs/pii/0304407686900631>
- Glosten-Jagannathan-Runkle 1993, "On the Relation between the Expected Value and the Volatility of the Nominal Excess Return on Stocks" (J. Finance): <https://faculty.washington.edu/ezivot/econ589/GJRJOF1993.pdf>
- Merton 1976, "Option pricing when underlying stock returns are discontinuous" (J. Financial Economics).
- `arch` Python library: <https://arch.readthedocs.io/>
- QuantLib Heston / Merton: <https://www.quantlib.org/>

### When to skip

**Skip jump-diffusion on Kalshi at $100.** No current Kalshi contract prices jump risk; you are modelling a thing you do not trade.

**Skip GARCH unless you have an equity / crypto vol contract.** For weather, macro, politics — GARCH is not the right object. For S&P / VIX / BTC, use the implied-vol surface from Deribit / SPX options directly (sister doc §5). That IS GARCH-equivalent.

---

## 15. Scoring rules beyond Brier / log

### What it is

Proper scoring rules for probabilistic forecasts beyond the two covered in sister doc:

- **Threshold-weighted CRPS** (Gneiting-Ranjan 2011) — CRPS with emphasis on a region (e.g., the upper tail): `twCRPS = ∫ w(x) · (F(x) − 1{x ≥ y})² dx`. Makes scoring sensitive to tail accuracy.
- **Quantile-weighted CRPS** (qwCRPS, same paper) — weight each quantile by its importance.
- **Energy score** (Gneiting-Stanberry-Grimit 2008) — multivariate generalization of CRPS. But see Scheuerer-Hamill 2015: energy score is **not sensitive** to dependence misspec. Use variogram score instead for that purpose.
- **Variogram score** (Scheuerer-Hamill 2015) — `VS_p(X, y) = Σ_{i,j} w_{ij} · (|X_i − X_j|^p − |y_i − y_j|^p)²`. Discriminates correlation structure. Use for validating a joint-copula forecast.

### Why it matters for Kalshi

**Daily temperature brackets are tail-sensitive.** Whether the forecast assigns 3% or 1% to "> 100°F" matters enormously for pricing the extreme strike. Brier-on-binary is insensitive to the tail shape. Tail-weighted CRPS on the underlying temperature distribution (pre-binarization) gives you the right loss to minimize in the weather post-processing step (EMOS / QRF).

**Variogram score** validates that your copula-joint (§5) gets cross-city correlation right. Energy score won't catch this.

### Math sketch

**Tail-weighted CRPS:**
```
twCRPS(F, y) = ∫_{-∞}^∞ w(x) · (F(x) − 1{x ≥ y})² dx
Typical weights:
  w_upper(x) = max(x − c, 0)²     (quadratic tail for right-tail focus)
  w_indicator(x) = 1{x > c}
```

**Energy score (multivariate)** — `X ~ F` joint forecast, `y ∈ ℝ^d`:
```
ES(F, y) = E_F ||X − y|| − 0.5 · E_{F,F'} ||X − X'||
```

**Variogram score of order p**:
```
VS_p(F, y) = Σ_{i,j=1..d} w_{ij} · (E_F[|X_i − X_j|^p] − |y_i − y_j|^p)²
```
`p = 0.5` or `p = 1` typical.

### Practical recipe

**Tail-weighted CRPS per weather category** — `scoringRules` R package or `properscoring` / `scoringrules` (Python):

```python
import scoringrules as sr
# F = ensemble members (N_members,)
# y = observed temp
twcrps = sr.twcrps_ensemble(F, y, chain_func=lambda x: np.maximum(x - 95, 0)**2)
```

Track per-station / per-category. Optimize QRF / EMOS (sister doc §7.1) to minimize twCRPS, not CRPS, when the contract is extreme.

**Variogram score** — for cross-city joint predictions:
```python
vs = sr.variogram_score(joint_ensemble, observations, p=0.5)
```
Use to reject copula models that look fine on marginals but get joint wrong.

### Citations

- Gneiting & Ranjan 2011, "Comparing Density Forecasts Using Threshold- and Quantile-Weighted Scoring Rules" (J. Business & Economic Statistics): <https://www.researchgate.net/publication/227369481>
- Scheuerer & Hamill 2015, "Variogram-Based Proper Scoring Rules for Probabilistic Forecasts of Multivariate Quantities" (MWR): <https://journals.ametsoc.org/view/journals/mwre/143/4/mwr-d-14-00269.1.xml>
- Gneiting, Stanberry, Grimit 2008, "Assessing probabilistic forecasts of multivariate quantities, with an application to ensemble predictions of surface winds" (Test).
- `scoringrules` Python (JAX-accelerated): <https://github.com/frazane/scoringrules>
- `scoringRules` R: <https://cran.r-project.org/web/packages/scoringRules/>

### When to skip

**Skip energy score entirely** — Scheuerer-Hamill showed it is blind to correlation errors. Use variogram score.

**Skip beyond CRPS for binary Kalshi outputs.** Once the forecast is binarized (`P(T > 75)`), Brier and log-loss are the right objects. Tail-weighted CRPS is for the *continuous* underlying forecast, before binarization. Use it to score the EMOS / QRF step, not the downstream Kelly bet.

---

## 16. Practical drift detection suite

### What it is

Beyond PSI (sister doc §8.8), a portfolio of complementary drift detectors:

- **Page-Hinkley test** — CUSUM on mean residual. Fires on sustained shifts in average error. Low latency on persistent drift.
- **ADWIN (Adaptive WINdowing)** — Bifet-Gavaldà 2007. Maintains a variable-length window; shrinks when drift is detected. Strong theoretical guarantees (bounded false positive + detection rate).
- **KS-CUSUM** — Kolmogorov-Smirnov statistic in CUSUM form. Distribution-free, catches shape changes.
- **Jensen-Shannon divergence** on predicted-probability histograms. Bounded in `[0, log 2]`, symmetric, smoother than KL.

Different detectors fire on different failure modes. The cliché is "run all of them and investigate on any alarm."

### Why it matters for Kalshi

Models drift when:
- Weather AI models are updated by ECMWF / GraphCast — ensemble means shift.
- A new type of contract is listed (sports expansions, new macro series).
- Market microstructure changes (Kalshi rate-limit tier promotion; new LIP tier).

Each detector catches a different symptom:
- Page-Hinkley: "calibration Brier is creeping up" — persistent mean drift.
- ADWIN: "recent 50 bets look different from earlier 500" — detected by window shrinkage.
- KS-CUSUM: "predicted-probability distribution has changed shape" — e.g., fewer 50/50 bets.
- JS-divergence: "market-price vs model-probability distribution has diverged" — can't see the change at the per-bet level but obvious in bulk.

### Math sketch

**Page-Hinkley**:
```
m_t = Σ_{i=1}^{t} (x_i − x̄_t − δ)     // cumulative deviation from running mean minus tolerance δ
M_t = min_{i ≤ t} m_i
PH_t = m_t − M_t
alarm if PH_t > λ
```
`δ` = tolerance (0.005 typical); `λ` = threshold (30-50 typical).

**ADWIN** (simplified):
```
maintain window W of all recent observations
repeatedly check if W can be split into W_0, W_1 s.t.
  |mean(W_0) − mean(W_1)| > ε_cut
if so: drop W_0 (older data)
ε_cut = √(1/(2·m) · log(4·|W|/δ))   Hoeffding-based
```

**KS-CUSUM**:
```
sliding window of residuals -> two halves (old, new)
D_ks = KS statistic between halves
if D_ks > threshold: alarm
(in CUSUM mode, accumulate std. deviates of per-obs KS residuals)
```

**Jensen-Shannon divergence**:
```
JS(P ∥ Q) = 0.5 · KL(P ∥ M) + 0.5 · KL(Q ∥ M),  M = 0.5·(P+Q)
bounded in [0, log 2] ≈ [0, 0.693]
alarm when JS > 0.1 (rough)
```

### Practical recipe

**Run four monitors in parallel**, one per category:

```cpp
struct DriftMonitor {
    PageHinkley ph{0.005, 40.0};   // delta, lambda
    ADWIN adwin{0.01};              // confidence delta
    KSCUSUM ks;
    JSDivergence js;

    DriftSignal observe(double pred_prob, int outcome, double market_prob) {
        DriftSignal sig{};
        double residual = outcome - pred_prob;
        sig.ph_alarm   = ph.observe(residual);
        sig.adwin_alarm = adwin.observe(residual);
        sig.ks_alarm   = ks.observe(residual);
        sig.js_alarm   = js.observe(pred_prob, market_prob);
        return sig;
    }
};
```

On any alarm: halt new trades in that category, log the signal + recent observations to the alert manager, trigger a refit offline.

**Library**: `river.drift` in Python has all four (Page-Hinkley, ADWIN, KS-Win, JS). For C++, each is 30-50 LOC; write them.

### Citations

- Page 1954, CUSUM — see §4.
- Bifet & Gavaldà 2007, "Learning from Time-Changing Data with Adaptive Windowing" (SDM): <https://www.cs.upc.edu/~gavalda/papers/adwin06.pdf>
- Gama, Žliobaitė, Bifet et al. 2014, "A survey on concept drift adaptation" (ACM CSUR): <https://dl.acm.org/doi/10.1145/2523813>
- `river` drift detectors: <https://riverml.xyz/latest/api/drift/>
- NannyML guide to univariate drift: <https://www.nannyml.com/blog/comprehensive-guide-univariate-methods>

### When to skip

**Skip any single detector in isolation — always run the suite.** Each alone has blind spots (Page-Hinkley misses distribution-shape changes; KS misses mean drifts with same variance; etc.). The suite is the right unit.

**Skip if you have no baseline.** Drift detection needs a "pre-drift" reference distribution. For a brand-new category with <100 observations, drift detection fires constantly on noise. Wait until 300+ observations before turning on.

---

## 17. Multi-armed bandit for execution

### What it is

Frame order-execution choices as a bandit problem:
- Arms: {cancel, hold-in-book, re-price-up-1-tick, re-price-down-1-tick, market-cross}.
- Context (for contextual bandit): {current spread, queue position, time-on-book, recent fill rate, age-of-quote, time-to-settlement}.
- Reward: realized fill-quality vs arrival price (+fill bonus, −adverse selection cost).

Analogous to the equity-market-making bandit literature (Alfonsi-Fruth-Schied; recent extensions by Cartea et al.).

### Why it matters for Kalshi

At $100 capital with integer (or 4-decimal fractional) contracts, the bot places maybe 5-50 orders/day. Bandit execution earns its keep when:

- You have enough volume to estimate per-arm fill rates (1000+ orders/arm).
- Spread capture matters materially (your edge per trade is small).
- Queue dynamics exist (HFT-style races).

None of these apply at $100 on Kalshi. Your spread is 2-40%; your edge is ~1-5 cents; queue position is not a thing (markets are slow). This section is documented for completeness (and Phase 2 dYdX), not because you should build it soon.

### Math sketch

**LinUCB** (sister doc §11 covers, this section cites): context `x_t` = [spread, queue_pos, time_on_book, recent_fill_rate, ttl], pick arm `argmax θ̂ᵀ x + α √(xᵀ A⁻¹ x)`.

**Realized reward**:
```
r = (arrival_price − fill_price) · sign(side) − fees − adverse_fill_penalty
```

Adverse-fill penalty: if the bet settled at a loss within X seconds, charge a penalty. This is the hard part — labeling the "right" execution is not obvious; Kearns-Nevmyvaka 2006 discusses.

### Practical recipe

Don't. Until 2027. Unless your trading volume reaches 1000+ orders/day on Kalshi.

If you must, the path is:
1. Log every order event (place, cancel, amend, fill) with full context.
2. Offline: fit LinUCB on the labeled data.
3. Shadow-mode for 4 weeks; compare to existing rule-based execution.
4. Deploy with conservative α and hard caps.

### Citations

- Kearns & Nevmyvaka 2006, "Reinforcement Learning for Optimized Trade Execution" (ICML): <https://www.cis.upenn.edu/~mkearns/papers/rlexec.pdf>
- Cartea, Jaimungal, Penalva 2015, *Algorithmic and High-Frequency Trading* (Cambridge).
- Hendricks & Wilcox 2014, "A reinforcement learning extension to the Almgren-Chriss framework for optimal trade execution": <https://arxiv.org/abs/1403.2229>

### When to skip

**Skip for the entire Phase 1 of this bot.** The assumptions (high volume, tight spreads, queue dynamics) don't match Kalshi at $100. Revisit in Phase 2 for dYdX v4 where Avellaneda-Stoikov already implies bandit-like ask/bid price decisions.

---

## 18. Extreme value theory (EVT) for tail risk

### What it is

Statistical theory of the largest-`k` observations in a sample. Two classical frameworks:

- **Block maxima** / GEV: for a random variable `X`, the distribution of `max(X_1, ..., X_n)` (as `n → ∞`) converges to the Generalized Extreme Value distribution with CDF `G(z) = exp(−[1 + ξ(z−μ)/σ]^(−1/ξ))`. `ξ` = shape: `> 0` heavy-tailed, `< 0` bounded.
- **Peaks Over Threshold (POT) / GPD**: for a high threshold `u`, `P(X > u + y | X > u)` converges to the Generalized Pareto Distribution (GPD). Efficient data use (all exceedances, not just block-max).

Coles 2001 is the canonical textbook.

### Why it matters for Kalshi

Two direct Kalshi applications:

1. **Record-temperature contracts**. "Will NYC hit 100°F this summer?" — the tail of the summer-max distribution. Pull historical ASOS daily maxes for NYC over 50 years; fit GEV to annual maxima or GPD to exceedances over 95°F. Reference probability for the contract is then `P(GEV.max > 100)` or `1 − F_GPD(100 − 95)`.

2. **Cat-5 hurricane contracts / major-landfall**. Similar: fit GPD to historical max wind speeds of Atlantic hurricanes, update with current-season ENSO / SST covariates.

Sister doc §3 and §9 touch on these categories but use ensemble / survival methods. EVT is the natural complement: ensemble models underestimate extreme-tail probabilities (they are trained to minimize CRPS, not tail accuracy). A GPD fit on residuals > 95th percentile can correct the ensemble's tail.

### Math sketch

**GEV**:
```
G(z) = exp(−[1 + ξ(z−μ)/σ]^(−1/ξ))       for 1 + ξ(z−μ)/σ > 0
     = exp(−exp(−(z−μ)/σ))                for ξ = 0 (Gumbel)
```

**GPD** (excess over threshold `u`):
```
H_u(y) = 1 − (1 + ξy/σ)^(−1/ξ)            for y > 0, 1 + ξy/σ > 0
```
Fit by MLE: `(ξ, σ)` from exceedance data.

**Threshold selection** — the single hardest decision in POT:
- Mean Residual Life (MRL) plot: `E[X − u | X > u]` should be linear in `u` above the "true" threshold.
- Parameter stability plot: estimated `(ξ, σ_u)` should be roughly constant as `u` increases.
- In practice: pick the 95th or 99th percentile; sensitivity-check.

### Practical recipe

**For "NYC > 100°F this summer" contract**:

```python
import scipy.stats as st
# 50 years of daily max temperatures for NYC (GHCN)
maxes = daily_maxes[daily_maxes >= 95]  # exceedances over 95°F
excesses = maxes - 95
shape, loc, scale = st.genpareto.fit(excesses, floc=0)
# P(X > 100) = P(exceedance > 5) given exceedance > 0
p_exceed_100 = 1 - st.genpareto.cdf(5, shape, loc=0, scale=scale)
```

Combine with a base-rate frequency: `P(any day > 95) = N_exceed / N_days`. Then:
```
P(max_season > 100) ≈ 1 − (1 − p_exceed_100)^(expected_exceedances_this_season)
```
(A Poisson-GPD model; calendar-days as Poisson exposure.)

**Library**: `scipy.stats.genextreme` and `genpareto`; `evt` R package; `pyextremes` Python.

### Citations

- Coles 2001, *An Introduction to Statistical Modeling of Extreme Values* — Springer. The reference.
- Pickands 1975, "Statistical Inference Using Extreme Order Statistics" (Annals of Statistics) — original GPD.
- Balkema & de Haan 1974 — complement to Pickands on POT.
- `pyextremes`: <https://github.com/georgebv/pyextremes>
- R `ismev` library: <https://cran.r-project.org/package=ismev>
- WeatherBench2's EVT verification: <https://weatherbench2.readthedocs.io>

### When to skip

**Skip for the everyday "does NYC hit 75°F" contract.** EVT is a tail method. For the bulk-probability part of the distribution (25th-75th percentile events), standard ensemble + EMOS + QRF dominate.

**Skip if you have <30 exceedances over your chosen threshold.** GPD MLE is unstable at small N.

**Be cautious with threshold selection.** A too-low threshold violates the GPD asymptotic regime; too high leaves you data-starved. Get this wrong and you get confidently-wrong tail probabilities. Always report a sensitivity analysis over threshold choices.

---

## 19. Recommended order of implementation for this bot

This is the opinionated version. You have finite engineering budget and $100 capital. Most of this doc is NOT worth building right now. Here is the short list — 4 items, strictly prioritized:

### #1: Regret-minimizing ensemble layer (§3, Hedge)

**Why first**: lowest engineering cost (1-week C++ implementation), highest immediate uplift. You already have multiple forecasters feeding `probability_engine.cpp`. Replace the current fixed combination with a Hedge aggregator. Provable worst-case bound vs best single expert. Zero risk vs current behavior (Hedge with `η = 0` reduces to whichever expert you initialize with).

**Cost**: 1 engineer-week C++, 1 week shadow-mode validation.
**Benefit**: Expected 5-10% Brier reduction in mixed-regime periods (anecdotal; but well-documented in Good Judgment Project data and in Satopää et al.). Plus a cleaner foundation for adding / removing forecasters over time.
**Honest concern**: if you have only 1-2 forecasters, this is premature. Need K ≥ 3 to justify.

### #2: Thompson sampling for category allocation (§11)

**Why second**: the existing `AdaptiveSizer` is per-category and hand-tuned. Thompson sampling with Beta-Bernoulli priors per category is a principled drop-in replacement for "which category gets the next dollar." Auto-explores new categories (including sports / entertainment, if you decide to try them) and auto-demotes broken ones.

**Cost**: 40 LOC C++ + 1 week of shadow-mode.
**Benefit**: Removes a major source of manual tuning. Honest exploration of categories you otherwise underweight by habit.
**Honest concern**: the current `AdaptiveSizer` already does something bandit-flavored (Brier-based down-weighting + disable cooldown). The delta from formalizing as Thompson is modest. But the formalism lets you add new arms (exploration probes, contextual features) cleanly later.

### #3: Drift detection suite (§16)

**Why third**: your models WILL drift. AI weather models are updated every few months. BLS revises definitions. Kalshi changes fees per series. If you find out the model drifted because PnL dropped 15% over a month, that's a $15 loss — material on $100. Four lightweight detectors (Page-Hinkley, ADWIN, KS-CUSUM, JS divergence), running per category on the existing settlement stream, fire early and specifically.

**Cost**: 2 engineer-weeks (~200 LOC C++ total, includes tests).
**Benefit**: Real-money savings via early category pauses; clear audit trail of what went wrong.
**Honest concern**: alerts fatigue. Tune thresholds on historical data before turning on live; rate-limit alerts per category.

### #4: Sobol / quasi-MC for Bayesian Kelly integration (§6)

**Why fourth**: the current Bayesian Kelly uses uniform MC (sister doc §9.4). Swapping to Sobol is a 30-LOC change for a 10-100× wall-clock speedup. Not a "new capability" — a pure efficiency win — but at the sub-$100 scale, CPU budget constrains how many markets you can evaluate per tick. This unlocks more markets per evaluation cycle.

**Cost**: 1 day (honest).
**Benefit**: Re-evaluate 100 markets in the time the bot currently evaluates 10. Indirect leverage: more markets evaluated = higher per-day edge capture.
**Honest concern**: very small win. Fourth priority for a reason.

---

### Everything else: deliberate "not yet"

| Topic | Why deferred |
|---|---|
| §1 Kalman for station bias | EWMA is 90% of the benefit at 10% of the code. Revisit if you find stations drifting badly. |
| §2 FTRL-Proximal | Premature without 20+ features in calibrator. |
| §4 BOCD / HMM | Page-Hinkley in §16 gets most of the surveillance value. BOCD is a future refinement. |
| §5 Copula joint pricing | Cluster cap is 80% of the benefit. Revisit when you have 5+ simultaneous bets in one cluster regularly. |
| §7 KL / info-gain diagnostics | Easy to add (a day) but low ROI. Do after #4 if there is spare time. |
| §8 OU mean-reversion | Needs volume and capital you do not have. 2028 topic. |
| §9 Survival / Cox for hurricanes | Highly relevant in June-October. Build *in* May 2026. Before hurricane season. |
| §10 Heston / rough-vol | No Kalshi product depends on it. Skip indefinitely. |
| §12 Meta-labeling | Needs 200+ primary-model trades per category first. ~2026 Q4. |
| §13 Online isotonic | Sliding-window PAVA is adequate. |
| §14 GARCH / jump | Skip; no matching Kalshi product. |
| §15 Tail-weighted CRPS | Do alongside QRF weather post-processing (sister doc §7.1 Phase 2). |
| §17 Execution bandit | Phase 2 / dYdX topic. |
| §18 EVT | Relevant when record-temp contracts list. Build in late spring. |

### Monthly re-read trigger

Read this "recommended order" list at the start of each month. If an item's conditional has now been met (e.g., "200 primary trades per category" → meta-labeling becomes live), move it up. Otherwise, keep shipping the current shortlist.

---

*Document version: 1.0 — April 2026. Companion to `RESEARCH_DATA_ALGORITHMS.md` (data sources, core algorithms, Kalshi platform). Maintenance: re-check library options yearly; re-read the recommended-order section monthly; update seasonal-relevance items (EVT, survival) ahead of hurricane season.*
