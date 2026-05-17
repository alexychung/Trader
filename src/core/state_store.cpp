#include "core/state_store.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>

#if defined(_WIN32)
  #include <io.h>
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <share.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace {
// Write buffer to path and fsync to disk before returning. Returns true on success.
// Durability matters here: we crash-recover from this file, so a partial write or
// pending OS buffer that the user's machine loses on power-cut would corrupt state.
bool write_and_fsync(const std::string& path, const std::string& contents) {
#if defined(_WIN32)
    int fd = -1;
    if (_sopen_s(&fd, path.c_str(),
                 _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                 _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0 || fd < 0) {
        return false;
    }
    const char* p = contents.data();
    size_t remaining = contents.size();
    while (remaining > 0) {
        int n = _write(fd, p, static_cast<unsigned>(remaining));
        if (n <= 0) { _close(fd); return false; }
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    int rc = _commit(fd);  // flush OS buffers to disk
    _close(fd);
    return rc == 0;
#else
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const char* p = contents.data();
    size_t remaining = contents.size();
    while (remaining > 0) {
        ssize_t n = ::write(fd, p, remaining);
        if (n <= 0) { ::close(fd); return false; }
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
#endif
}
} // namespace

namespace trader {

namespace fs = std::filesystem;
using json = nlohmann::json;

static int64_t to_epoch_seconds(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::seconds>(ts.time_since_epoch()).count();
}

static Timestamp from_epoch_seconds(int64_t s) {
    return Timestamp(std::chrono::seconds(s));
}

StateStore::StateStore(std::string path) : path_(std::move(path)) {
    auto parent = fs::path(path_).parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            spdlog::warn("StateStore: could not create dir {}: {}", parent.string(), ec.message());
        }
    }
}

static std::optional<PersistedState> read_file(const std::string& p) {
    std::ifstream in(p);
    if (!in) return std::nullopt;
    try {
        json j; in >> j;
        PersistedState s;
        s.version = j.value("version", 1);
        s.balance = j.value("balance", 100.0);
        s.starting_bankroll = j.value("starting_bankroll", 100.0);
        s.daily_pnl = j.value("daily_pnl", 0.0);
        s.cumulative_pnl = j.value("cumulative_pnl", 0.0);
        s.day_start = from_epoch_seconds(j.value("day_start", int64_t{0}));
        s.last_saved = from_epoch_seconds(j.value("last_saved", int64_t{0}));
        s.trades_today = j.value("trades_today", 0);
        s.last_fill_ts_ms = j.value("last_fill_ts_ms", int64_t{0});
        return s;
    } catch (const std::exception& e) {
        spdlog::warn("StateStore: failed to parse {}: {}", p, e.what());
        return std::nullopt;
    }
}

std::optional<PersistedState> StateStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto primary = read_file(path_);
    if (primary) return primary;

    std::string bak = path_ + ".bak";
    if (fs::exists(bak)) {
        spdlog::warn("StateStore: primary file missing/corrupt, falling back to {}", bak);
        return read_file(bak);
    }
    return std::nullopt;
}

bool StateStore::save(const PersistedState& state) {
    std::lock_guard<std::mutex> lock(mutex_);

    json j;
    j["version"] = state.version;
    j["balance"] = state.balance;
    j["starting_bankroll"] = state.starting_bankroll;
    j["daily_pnl"] = state.daily_pnl;
    j["cumulative_pnl"] = state.cumulative_pnl;
    j["day_start"] = to_epoch_seconds(state.day_start);
    j["last_saved"] = to_epoch_seconds(std::chrono::system_clock::now());
    j["trades_today"] = state.trades_today;
    j["last_fill_ts_ms"] = state.last_fill_ts_ms;

    std::string tmp = path_ + ".tmp";
    std::string payload = j.dump(2);
    if (!write_and_fsync(tmp, payload)) {
        spdlog::error("StateStore: write/fsync failed for {}", tmp);
        return false;
    }

    std::error_code ec;
    if (fs::exists(path_)) {
        // Move current to backup before replacing
        fs::copy_file(path_, path_ + ".bak", fs::copy_options::overwrite_existing, ec);
        if (ec) spdlog::warn("StateStore: backup copy failed: {}", ec.message());
        ec.clear();
    }

    fs::rename(tmp, path_, ec);
    if (ec) {
        // On Windows, rename can fail if target exists in some cases — try remove + rename
        fs::remove(path_, ec);
        ec.clear();
        fs::rename(tmp, path_, ec);
    }
    if (ec) {
        spdlog::error("StateStore: rename {} -> {} failed: {}", tmp, path_, ec.message());
        return false;
    }
    return true;
}

void StateStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    fs::remove(path_, ec);
    fs::remove(path_ + ".bak", ec);
    fs::remove(path_ + ".tmp", ec);
}

} // namespace trader
