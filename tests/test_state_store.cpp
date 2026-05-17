#include <gtest/gtest.h>
#include "core/state_store.hpp"
#include <filesystem>
#include <fstream>
#include <random>

using namespace trader;
namespace fs = std::filesystem;

class StateStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::random_device rd;
        path_ = (fs::temp_directory_path() / ("state_test_" + std::to_string(rd()) + ".json")).string();
        cleanup();
    }
    void TearDown() override { cleanup(); }
    void cleanup() {
        std::error_code ec;
        fs::remove(path_, ec);
        fs::remove(path_ + ".bak", ec);
        fs::remove(path_ + ".tmp", ec);
    }
    std::string path_;
};

TEST_F(StateStoreTest, LoadReturnsNulloptWhenMissing) {
    StateStore store(path_);
    auto loaded = store.load();
    EXPECT_FALSE(loaded.has_value());
}

TEST_F(StateStoreTest, SaveAndReload) {
    StateStore store(path_);
    PersistedState s;
    s.balance = 87.42;
    s.starting_bankroll = 100.0;
    s.daily_pnl = -3.50;
    s.cumulative_pnl = -12.58;
    s.trades_today = 7;

    ASSERT_TRUE(store.save(s));
    ASSERT_TRUE(fs::exists(path_));

    auto loaded = store.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_NEAR(loaded->balance, 87.42, 0.001);
    EXPECT_NEAR(loaded->starting_bankroll, 100.0, 0.001);
    EXPECT_NEAR(loaded->daily_pnl, -3.50, 0.001);
    EXPECT_NEAR(loaded->cumulative_pnl, -12.58, 0.001);
    EXPECT_EQ(loaded->trades_today, 7);
}

TEST_F(StateStoreTest, BackupCreatedOnSecondSave) {
    StateStore store(path_);
    PersistedState s;
    s.balance = 100.0;
    store.save(s);
    s.balance = 95.0;
    store.save(s);
    EXPECT_TRUE(fs::exists(path_ + ".bak"));
}

TEST_F(StateStoreTest, FallsBackToBackupOnCorrupt) {
    StateStore store(path_);
    PersistedState s;
    s.balance = 88.0;
    store.save(s);
    s.balance = 77.0;
    store.save(s);  // creates .bak with balance=88

    // Corrupt the primary file.
    {
        std::ofstream out(path_, std::ios::trunc);
        out << "{ this is { not valid JSON";
    }

    auto loaded = store.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_NEAR(loaded->balance, 88.0, 0.001);  // came from backup
}

TEST_F(StateStoreTest, ClearRemovesFiles) {
    StateStore store(path_);
    PersistedState s;
    store.save(s);
    store.save(s);  // creates .bak too
    store.clear();
    EXPECT_FALSE(fs::exists(path_));
    EXPECT_FALSE(fs::exists(path_ + ".bak"));
}
