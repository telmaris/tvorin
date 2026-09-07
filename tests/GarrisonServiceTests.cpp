#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "warfare/GarrisonService.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    struct GarrisonFixture
    {
        TileMap map;
        Player player;
        Barracks* barracks{nullptr};
        GuardTower* tower{nullptr};

        GarrisonFixture()
            : player(0, map)
        {
            map.params.sizeX = 40;
            map.params.sizeY = 40;
            map.tilemap.reserve(static_cast<size_t>(map.params.sizeX * map.params.sizeY));
            for (int id = 0; id < map.params.sizeX * map.params.sizeY; ++id)
            {
                map.tilemap.emplace_back(id);
                map.tilemap.back().tileType = TileType::GRASS;
            }
            player.RebindTileMap(map);
            barracks = dynamic_cast<Barracks*>(map.PlaceLoadedBuilding(
                map.GetIdFromCoords({4, 4}), &player, std::make_unique<Barracks>(10)));
            tower = dynamic_cast<GuardTower*>(map.PlaceLoadedBuilding(
                map.GetIdFromCoords({12, 4}), &player, std::make_unique<GuardTower>(20)));
        }
    };
}

TEST(GarrisonServiceTests, AssignAndReturnAreAtomicAndUseCanonicalRoster)
{
    GarrisonFixture fixture;
    ASSERT_NE(fixture.barracks, nullptr);
    ASSERT_NE(fixture.tower, nullptr);

    BattleUnit unit{1, fixture.player.id, "militia"};
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(
        unit, fixture.barracks->provinceId, fixture.barracks->id));
    fixture.player.roster.AddUnit(std::move(unit));

    std::string failure;
    EXPECT_TRUE(GarrisonService::AssignUnitsToGarrison(
        fixture.player, *fixture.player.GetProvinceEconomy(), fixture.barracks->id,
        fixture.tower->id, {1}, failure)) << failure;
    ASSERT_EQ(fixture.player.roster.FindUnit(1)->assignment.kind,
              UnitAssignmentKind::DefensiveGarrison);
    EXPECT_EQ(fixture.player.roster.FindUnit(1)->assignment.buildingId, fixture.tower->id);
    EXPECT_EQ(GarrisonService::BuildSummary(
                  fixture.player, *fixture.player.GetProvinceEconomy(), *fixture.tower).used,
              1);

    EXPECT_TRUE(GarrisonService::ReturnUnitsToBarracks(
        fixture.player, *fixture.player.GetProvinceEconomy(), fixture.tower->id,
        fixture.barracks->id, {1}, failure)) << failure;
    EXPECT_EQ(fixture.player.roster.FindUnit(1)->assignment.kind,
              UnitAssignmentKind::BarracksReserve);
    EXPECT_EQ(fixture.player.roster.FindUnit(1)->assignment.buildingId, fixture.barracks->id);
}

TEST(GarrisonServiceTests, CapacityAndDuplicateRequestsAreRejectedBeforeMutation)
{
    GarrisonFixture fixture;
    ASSERT_NE(fixture.barracks, nullptr);
    ASSERT_NE(fixture.tower, nullptr);

    for (int id = 1; id <= 2; ++id)
    {
        BattleUnit unit{id, fixture.player.id, "militia"};
        ASSERT_TRUE(UnitAssignmentService::AssignReserve(
            unit, fixture.barracks->provinceId, fixture.barracks->id));
        fixture.player.roster.AddUnit(std::move(unit));
    }

    std::string failure;
    EXPECT_FALSE(GarrisonService::AssignUnitsToGarrison(
        fixture.player, *fixture.player.GetProvinceEconomy(), fixture.barracks->id,
        fixture.tower->id, {1, 1}, failure));
    EXPECT_EQ(fixture.player.roster.FindUnit(1)->assignment.kind,
              UnitAssignmentKind::BarracksReserve);

    fixture.tower->garrison.capacity = 1;
    EXPECT_TRUE(GarrisonService::AssignUnitsToGarrison(
        fixture.player, *fixture.player.GetProvinceEconomy(), fixture.barracks->id,
        fixture.tower->id, {1}, failure));
    EXPECT_FALSE(GarrisonService::AssignUnitsToGarrison(
        fixture.player, *fixture.player.GetProvinceEconomy(), fixture.barracks->id,
        fixture.tower->id, {2}, failure));
    EXPECT_EQ(fixture.player.roster.FindUnit(2)->assignment.kind,
              UnitAssignmentKind::BarracksReserve);
}

TEST(GarrisonServiceTests, UpkeepUsesWholePackagesAndReportsUnsuppliedWithoutDesertion)
{
    GarrisonFixture fixture;
    BattleUnit unit{1, fixture.player.id, "militia"};
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(
        unit, fixture.barracks->provinceId, fixture.barracks->id));
    fixture.player.roster.AddUnit(std::move(unit));
    std::string failure;
    ASSERT_TRUE(GarrisonService::AssignUnitsToGarrison(
        fixture.player, *fixture.player.GetProvinceEconomy(), fixture.barracks->id,
        fixture.tower->id, {1}, failure)) << failure;

    auto* upkeep = fixture.tower->GetComponent<GarrisonUpkeepComponent>();
    ASSERT_NE(upkeep, nullptr);
    GarrisonService::UpdateBuilding(*fixture.tower, 120.0);
    EXPECT_EQ(upkeep->debtMicros, GarrisonUpkeepComponent::DebtScale);
    EXPECT_EQ(upkeep->supplyStatus, GarrisonSupplyStatus::Unsupplied);
    EXPECT_EQ(fixture.player.roster.FindUnit(1)->assignment.kind,
              UnitAssignmentKind::DefensiveGarrison);

    auto* local = fixture.tower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(local, nullptr);
    local->buffers.at(ResourceType::FOOD_PROVISIONS).SetStoredAmount(1);
    GarrisonService::UpdateBuilding(*fixture.tower, 60.0);
    EXPECT_EQ(upkeep->debtMicros, GarrisonUpkeepComponent::DebtScale / 2);
    EXPECT_EQ(local->buffers.at(ResourceType::FOOD_PROVISIONS).buffer.size(), 0u);
    EXPECT_EQ(upkeep->supplyStatus, GarrisonSupplyStatus::Supplied);
}
