#include <gtest/gtest.h>
#include "risk/cluster_limiter.hpp"

using namespace trader;

TEST(ClusterLimiter, KeyExtraction) {
    EXPECT_EQ(ClusterLimiter::cluster_key("KXHIGHNY-26APR26-T75"), "KXHIGHNY-26APR26");
    EXPECT_EQ(ClusterLimiter::cluster_key("KXCPI-26MAY-T3.0"), "KXCPI-26MAY");
    EXPECT_EQ(ClusterLimiter::cluster_key("KXFED-26JUN-T5.25"), "KXFED-26JUN");
    EXPECT_EQ(ClusterLimiter::cluster_key("SINGLESEG"), "SINGLESEG");
    EXPECT_EQ(ClusterLimiter::cluster_key("ONLY-TWO"), "ONLY-TWO");
}

TEST(ClusterLimiter, AllowsUnderCap) {
    ClusterLimiter cl({.max_per_cluster = 30.0});
    EXPECT_TRUE(cl.check("KXHIGHNY-26APR26-T75", 10.0));
    cl.on_fill("KXHIGHNY-26APR26-T75", 10.0);
    EXPECT_TRUE(cl.check("KXHIGHNY-26APR26-T80", 19.0));  // 10 + 19 = 29 < 30
}

TEST(ClusterLimiter, RejectsOverCap) {
    ClusterLimiter cl({.max_per_cluster = 30.0});
    cl.on_fill("KXHIGHNY-26APR26-T75", 25.0);
    EXPECT_FALSE(cl.check("KXHIGHNY-26APR26-T80", 10.0));  // 25 + 10 = 35 > 30
    auto reason = cl.reject_reason("KXHIGHNY-26APR26-T80", 10.0);
    EXPECT_FALSE(reason.empty());
    EXPECT_NE(reason.find("cluster"), std::string::npos);
}

TEST(ClusterLimiter, DifferentClustersIndependent) {
    ClusterLimiter cl({.max_per_cluster = 30.0});
    cl.on_fill("KXHIGHNY-26APR26-T75", 25.0);
    // Different cluster (different date) — should still allow.
    EXPECT_TRUE(cl.check("KXHIGHNY-26APR27-T75", 25.0));
}

TEST(ClusterLimiter, SettlementReleasesExposure) {
    ClusterLimiter cl({.max_per_cluster = 30.0});
    cl.on_fill("KXHIGHNY-26APR26-T75", 25.0);
    EXPECT_FALSE(cl.check("KXHIGHNY-26APR26-T80", 10.0));
    cl.on_settlement("KXHIGHNY-26APR26-T75");
    EXPECT_TRUE(cl.check("KXHIGHNY-26APR26-T80", 10.0));
}

TEST(ClusterLimiter, ClusterExposureSums) {
    ClusterLimiter cl;
    cl.on_fill("KXHIGHNY-26APR26-T75", 5.0);
    cl.on_fill("KXHIGHNY-26APR26-T80", 7.0);
    cl.on_fill("KXHIGHNY-26APR27-T75", 3.0);
    EXPECT_NEAR(cl.cluster_exposure("KXHIGHNY-26APR26"), 12.0, 0.001);
    EXPECT_NEAR(cl.cluster_exposure("KXHIGHNY-26APR27"), 3.0, 0.001);

    auto all = cl.all_clusters();
    EXPECT_EQ(all.size(), 2u);
}

TEST(ClusterLimiter, AccumulatesOnRepeatFill) {
    ClusterLimiter cl;
    cl.on_fill("KXHIGHNY-26APR26-T75", 5.0);
    cl.on_fill("KXHIGHNY-26APR26-T75", 7.0);  // same ticker, more contracts
    EXPECT_NEAR(cl.cluster_exposure("KXHIGHNY-26APR26"), 12.0, 0.001);
}
