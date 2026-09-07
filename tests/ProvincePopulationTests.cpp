#include "economy/ProvincePopulation.h"

#include <gtest/gtest.h>

TEST(ProvincePopulationTests, AllocatesResidentsProportionallyAndStably)
{
    const auto allocation = AllocateProvinceVillageResidents(90.0, {100.0, 200.0, 0.0});
    ASSERT_EQ(allocation.size(), 3u);
    EXPECT_DOUBLE_EQ(allocation[0], 30.0);
    EXPECT_DOUBLE_EQ(allocation[1], 60.0);
    EXPECT_DOUBLE_EQ(allocation[2], 0.0);
}

TEST(ProvincePopulationTests, OverflowIsKeptInsteadOfDeletingPopulation)
{
    const auto allocation = AllocateProvinceVillageResidents(500.0, {100.0, 200.0});
    ASSERT_EQ(allocation.size(), 2u);
    EXPECT_DOUBLE_EQ(allocation[0] + allocation[1], 300.0);
}

TEST(ProvincePopulationTests, ZeroCapacityDoesNotAssignResidents)
{
    const auto allocation = AllocateProvinceVillageResidents(500.0, {0.0, -1.0});
    ASSERT_EQ(allocation.size(), 2u);
    EXPECT_DOUBLE_EQ(allocation[0], 0.0);
    EXPECT_DOUBLE_EQ(allocation[1], 0.0);
}
