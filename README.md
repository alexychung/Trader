# Trader

C++20 dual-venue automated trading system.

- **Phase 1 (active):** Kalshi event trading bot — quantitative probability models on public data (BLS, NOAA, FRED) to find mispriced prediction-market contracts.
- **Phase 2 (planned):** dYdX v4 perp market making.

See `CLAUDE.md` for the project overview and `docs/` for research.

## Setup ($100 set-and-forget Kalshi bot)

### 1. Build

```bash
# Windows (vcpkg manifest mode — first build pulls deps and takes a while)
cmake --preset default
cmake --build --preset default
```

### 2. Get a Kalshi API key

Demo and prod are separate venues with separate key pairs. Create a key in
each as needed — the loader picks `kalshi.demo` when `mode: paper` and
`kalshi.prod` when `mode: live` (see `config/config.example.yaml`).

1. Open an account at https://kalshi.com (prod) or https://demo.kalshi.co (demo). KYC is required on prod.
2. Generate an RSA API key under **Account → API Keys**. Kalshi shows the private key text exactly **once** — save it yourself.
3. Save the PEM text to `secrets/kalshi_api_key_demo.pem` (or `..._prod.pem`). If Kalshi hands you a PKCS#1 key (`-----BEGIN RSA PRIVATE KEY-----`), convert once:
   ```bash
   openssl pkcs8 -topk8 -nocrypt -in secrets/kalshi_api_key_demo.pem \
       -out secrets/kalshi_api_key_demo.pkcs8.pem
   mv secrets/kalshi_api_key_demo.pkcs8.pem secrets/kalshi_api_key_demo.pem
   ```
4. Paste the key ID (UUID) into `config/config.yaml` under `kalshi.demo.api_key_id` / `kalshi.prod.api_key_id`. `secrets/` is git-ignored.

### 3. (Optional) Get free data feed keys

- **FRED** (Fed funds, GDP nowcasts): https://fred.stlouisfed.org/docs/api/api_key.html
- **BLS** (CPI, NFP — works without a key but with low rate limit): https://data.bls.gov/registrationEngine/

### 4. Configure

`config/config.yaml` ships with safe paper-mode defaults pointed at the **demo API**. Edit it:

- Replace `api_key_id` with your Kalshi key ID.
- Optionally fill in `feeds.bls.api_key` and `feeds.fred.api_key`.

### 5. Run (paper mode against demo API — recommended for first 48 hours)

```bash
./build/Debug/trader config/config.yaml
```

The bot will:
- Connect to the Kalshi demo API.
- Pull market catalogs every 60s, generate model probabilities, and place paper orders against the demo book.
- Log every decision (real trades + shadow predictions) to `data/calibration.json`.
- Persist bankroll/PnL/state to `data/state.json` (atomic, with backup) so restarts pick up where it left off.
- Auto-pause categories whose Brier score deteriorates past 0.25, with weekly micro-probes to allow recovery.
- Auto-cancel unfilled orders after 5 minutes; reprice on >2¢ market moves; force-exit positions on 10pp model flips.

### 6. Going live (real money)

After 48–72 hours of clean demo-mode running:

1. Edit `config/config.yaml` → `mode: "live"`.
2. Update `kalshi.api_base` and `kalshi.ws_url` to the production URLs.
3. Deposit funds at https://kalshi.com.
4. Reset state: `rm data/state.json data/state.json.bak` (so initial bankroll matches your deposit).
5. Run with the per-session live confirmation flag:

```bash
./build/Debug/trader config/config.yaml --confirm-live
```

The bot will refuse to start in live mode without `--confirm-live`. It will **also** refuse to start if the actual Kalshi balance differs from the persisted balance by more than $1 (catches stale state files and accidental redeploys).

### 7. Monitor

Tail the log:

```bash
tail -f logs/trader.log
```

Status lines look like:

```
Status: Balance $98.40 (cap $150.00) | Exposure $12.30 | Daily $-1.60 |
        Real 14 (Brier 0.182) | Shadows 88 (Brier 0.205) | Probes/Expl 3
```

- **cap** is the bankroll cap fed to Kelly sizing — `min(current_balance, starting × 1.5)`. Prevents runaway scaling on a lucky streak.
- **Brier (real)** = calibration on actually-traded markets.
- **Brier (all)** = unbiased calibration including shadow predictions on rejected markets.
- **Probes/Expl** = exploratory micro-trades the bot took to escape the cold-start trap.

### 8. Kill switch

Triggers automatically on:
- Daily loss past `kill_switch_loss` (default $30).
- 3 consecutive order failures.
- WS connectivity loss > 60s.

You can also Ctrl+C — the bot does a clean shutdown and persists state.

## Layout

```
src/
  core/           types, config, event bus, logging, state store, staleness gate, dashboard
  exchange/
    kalshi/       REST + WS clients, auth, exchange wrapper, order manager
  strategy/
    kalshi/       probability engine, models, edge detection, market filter,
                  calibration logger, adaptive sizer, prob calibrator, event strategy
  risk/           risk manager, kill switch
tests/            Google Test unit + integration tests
config/           YAML configuration
data/             persistent state (state.json, calibration.json) — git-ignored
docs/             research + venue analysis
```
