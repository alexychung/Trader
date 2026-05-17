#pragma once

#include "core/types.hpp"
#include <string>
#include <mutex>
#include <optional>

namespace trader {

// Persistent state for the bot. Survives restarts/crashes.
// Written atomically (temp file + rename) with a backup copy.
struct PersistedState {
    int version = 1;
    double balance = 100.0;            // Current cash balance (after fills, before settlement)
    double starting_bankroll = 100.0;  // Initial deposit; used to cap effective Kelly bankroll
    double daily_pnl = 0.0;            // Cumulative settled PnL since last day reset
    double cumulative_pnl = 0.0;       // All-time settled PnL (info only)
    Timestamp day_start;               // When the current trading day began
    Timestamp last_saved;
    int trades_today = 0;
    // Server-epoch (ms) of the last fill processed. Passed to
    // KalshiExchange::reconcile_fills_since on startup so any fills that
    // arrived while the bot was down (or during a WS disconnect) are
    // replayed via REST rather than silently dropped.
    int64_t last_fill_ts_ms = 0;
};

class StateStore {
public:
    explicit StateStore(std::string path);

    // Load state from disk. Returns nullopt if no file exists or file is corrupt
    // (caller should fall back to defaults). On corrupt file, the .bak is tried.
    std::optional<PersistedState> load();

    // Atomically save state. Writes to <path>.tmp, fsyncs, renames over <path>,
    // and copies the previous good file to <path>.bak. Returns true on success.
    bool save(const PersistedState& state);

    // Delete state file (and backup). Used in tests / fresh start.
    void clear();

    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::mutex mutex_;
};

} // namespace trader
