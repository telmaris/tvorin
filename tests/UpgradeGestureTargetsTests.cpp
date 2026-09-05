#include "ui/UpgradeGestureTargets.h"

#include <gtest/gtest.h>

TEST(UpgradeGestureTargetsTests, RetracingAndFootprintRevisitsSubmitOnlyOnce)
{
    UpgradeGestureTargets targets;

    EXPECT_TRUE(targets.InsertIfNew(10));
    EXPECT_TRUE(targets.InsertIfNew(11));
    EXPECT_FALSE(targets.InsertIfNew(10));
    EXPECT_FALSE(targets.InsertIfNew(11));
    EXPECT_EQ(targets.Size(), 2u);
}

TEST(UpgradeGestureTargetsTests, ResetStartsAViewIndependentGesture)
{
    UpgradeGestureTargets targets;
    ASSERT_TRUE(targets.InsertIfNew(42));

    targets.Reset();

    EXPECT_TRUE(targets.InsertIfNew(42));
    EXPECT_EQ(targets.Size(), 1u);
}

TEST(UpgradeGestureTargetsTests, TwentyUniqueRoadsProduceTwentyGestureTargets)
{
    UpgradeGestureTargets targets;
    for (int buildingId = 100; buildingId < 120; ++buildingId)
        EXPECT_TRUE(targets.InsertIfNew(buildingId));
    for (int buildingId = 119; buildingId >= 100; --buildingId)
        EXPECT_FALSE(targets.InsertIfNew(buildingId));

    EXPECT_EQ(targets.Size(), 20u);
}
