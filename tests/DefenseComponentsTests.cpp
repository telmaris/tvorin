#include "economy/Building.h"
#include "economy/BuildingConfig.h"
#include "simulation/MapGenerator.h"
#include "warfare/DefenseCoverage.h"

#include <gtest/gtest.h>

#include <type_traits>

TEST(DefenseComponentsTests, CatalogUsesAppendOnlyPilotTypes)
{
    EXPECT_GT(static_cast<int>(BuildingType::GuardTower),
              static_cast<int>(BuildingType::SiegeWorkshop));
    EXPECT_GT(static_cast<int>(BuildingType::Fortress),
              static_cast<int>(BuildingType::GuardTower));

    const auto& tower = GetBuildingDefinition(BuildingType::GuardTower);
    const auto& fortress = GetBuildingDefinition(BuildingType::Fortress);
    EXPECT_EQ(tower.type, BuildingType::GuardTower);
    EXPECT_EQ(fortress.type, BuildingType::Fortress);
    EXPECT_GT(tower.defense.coverageRadius, 0.0);
    EXPECT_GT(fortress.defense.garrisonCapacity, tower.defense.garrisonCapacity);
}

TEST(DefenseComponentsTests, DefenseBuildingIsComposedWithoutTowerPointers)
{
    GuardTower tower{1};
    EXPECT_TRUE(tower.HasComponent<DefenseCoverageComponent>());
    EXPECT_TRUE(tower.HasComponent<GarrisonComponent>());
    EXPECT_TRUE(tower.HasComponent<GarrisonUpkeepComponent>());
    EXPECT_TRUE(tower.HasComponent<LocalResourceBufferComponent>());
    EXPECT_TRUE(tower.HasComponent<LogisticsComponent>());
    EXPECT_TRUE(tower.HasComponent<SafetyComponent>());

    const auto* coverage = tower.GetComponent<DefenseCoverageComponent>();
    const auto* garrison = tower.GetComponent<GarrisonComponent>();
    const auto* upkeep = tower.GetComponent<GarrisonUpkeepComponent>();
    const auto* safety = tower.GetComponent<SafetyComponent>();
    ASSERT_NE(coverage, nullptr);
    ASSERT_NE(garrison, nullptr);
    ASSERT_NE(upkeep, nullptr);
    ASSERT_NE(safety, nullptr);
    EXPECT_EQ(garrison->GetEffectiveCapacity(tower), 12);
    EXPECT_DOUBLE_EQ(upkeep->packageSize, 1.0);
    EXPECT_DOUBLE_EQ(safety->intrinsicResilience, 0.35);
    EXPECT_FALSE(std::is_pointer_v<decltype(safety->intrinsicResilience)>);
    EXPECT_FALSE(std::is_pointer_v<decltype(coverage->requiredState)>);
}

TEST(DefenseComponentsTests, CoverageUsesFootprintCenterAndMultiplicativeClamp)
{
    TileMap map;
    map.params.sizeX = 40;
    map.params.sizeY = 40;
    map.tilemap.resize(static_cast<size_t>(map.params.sizeX * map.params.sizeY));

    GuardTower tower{1};
    tower.positionId = map.GetIdFromCoords({10, 10});
    Building target{2};
    target.positionId = map.GetIdFromCoords({18, 10});
    target.footprint = {2, 2};

    const auto center = DefenseCoverageService::GetBuildingCenter(map, tower);
    EXPECT_FLOAT_EQ(center.x, 11.0f);
    EXPECT_FLOAT_EQ(center.y, 11.0f);
    EXPECT_TRUE(DefenseCoverageService::Covers(map, tower, target));

    target.positionId = map.GetIdFromCoords({22, 10});
    EXPECT_FALSE(DefenseCoverageService::Covers(map, tower, target));

    EXPECT_EQ(DefenseCoverageService::ProtectionToBasisPoints(18.0), 1800);
    EXPECT_EQ(DefenseCoverageService::CombineProtectionBasisPoints({1800, 1800}), 3276);
    EXPECT_EQ(DefenseCoverageService::CombineProtectionBasisPoints({10000, 10000}), 10000);
    EXPECT_EQ(DefenseCoverageService::CombineProtectionBasisPoints({-20, 12000}), 10000);
}

TEST(DefenseComponentsTests, BuildingCenterUsesGeometricFootprintBounds)
{
    TileMap map;
    map.params.sizeX = 40;
    map.params.sizeY = 40;
    map.tilemap.resize(static_cast<size_t>(map.params.sizeX * map.params.sizeY));

    const auto expectCenter = [&map](Vec2i footprint, Vec2f expected)
    {
        Building building{1};
        building.positionId = map.GetIdFromCoords({10, 10});
        building.footprint = footprint;
        EXPECT_FLOAT_EQ(DefenseCoverageService::GetBuildingCenter(map, building).x, expected.x);
        EXPECT_FLOAT_EQ(DefenseCoverageService::GetBuildingCenter(map, building).y, expected.y);
    };

    expectCenter({1, 1}, {10.5f, 10.5f});
    expectCenter({2, 2}, {11.0f, 11.0f});
    expectCenter({3, 2}, {11.5f, 11.0f});
}
