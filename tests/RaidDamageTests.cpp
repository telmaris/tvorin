#include "warfare/RaidDamage.h"

#include <gtest/gtest.h>

TEST(RaidDamageTests, UsesStableIdsAndMultiplicativeSafety)
{
    RaidBuildingSnapshot unsafe;
    unsafe.buildingId = 20;
    unsafe.buildingType = BuildingType::StorageBuilding;
    unsafe.completed = true;
    unsafe.owned = true;
    unsafe.raidDestructible = true;
    unsafe.raidStockLossTarget = true;
    unsafe.baseDestructionChanceBasisPoints = 10000;
    unsafe.stockLossFractionBasisPoints = 10000;
    unsafe.resources = {{ResourceType::WOOD, 4}};

    RaidBuildingSnapshot protectedBuilding = unsafe;
    protectedBuilding.buildingId = 10;
    protectedBuilding.intrinsicResilienceBasisPoints = 9999;
    protectedBuilding.coverageProtectionBasisPoints = 9999;
    protectedBuilding.garrisonStrengthBasisPoints = 9999;
    protectedBuilding.readinessBasisPoints = 10000;
    protectedBuilding.resources.clear();

    const auto first = RaidDamageResolver::Resolve(
        {unsafe, protectedBuilding}, 7, 3, 42);
    const auto second = RaidDamageResolver::Resolve(
        {protectedBuilding, unsafe}, 7, 3, 42);
    ASSERT_TRUE(first.valid);
    ASSERT_EQ(first.buildings.size(), second.buildings.size());
    for (size_t index = 0; index < first.buildings.size(); ++index)
    {
        EXPECT_EQ(first.buildings[index].buildingId, second.buildings[index].buildingId);
        EXPECT_EQ(first.buildings[index].destroyed, second.buildings[index].destroyed);
        EXPECT_EQ(first.buildings[index].lostResources, second.buildings[index].lostResources);
    }
    ASSERT_EQ(first.buildings.size(), 1u);
    EXPECT_EQ(first.buildings.front().buildingId, 20);
    EXPECT_EQ(first.lostResources.at(ResourceType::WOOD), 4);
}

TEST(RaidDamageTests, DoesNotTargetHeadquartersRoadsOrConstruction)
{
    RaidBuildingSnapshot hq;
    hq.buildingId = 1;
    hq.buildingType = BuildingType::Headquarters;
    hq.completed = true;
    hq.owned = true;
    hq.raidDestructible = true;
    hq.baseDestructionChanceBasisPoints = 10000;

    RaidBuildingSnapshot road = hq;
    road.buildingId = 2;
    road.buildingType = BuildingType::Road;

    RaidBuildingSnapshot construction = hq;
    construction.buildingId = 3;
    construction.buildingType = BuildingType::StorageBuilding;
    construction.completed = false;

    const auto result = RaidDamageResolver::Resolve(
        {hq, road, construction}, 9, 4, 17);
    ASSERT_TRUE(result.valid);
    EXPECT_TRUE(result.buildings.empty());
}
