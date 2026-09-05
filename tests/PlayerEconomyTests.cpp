#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "economy/ProductionBuildings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Creates a tiny owned grass map for player economy tests.
    void PrepareOwnedMap(TileMap& map, Player* owner)
    {
        map.params.sizeX = 8;
        map.params.sizeY = 8;
        map.tilemap.clear();
        for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
        {
            Tile tile{i};
            tile.owner = owner;
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }
}

TEST(PlayerEconomyTests, PopulationCapCountsFinishedVillages)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(1)));
    ASSERT_NE(village, nullptr);
    village->population.populationCap = 42;
    village->constructionRemaining = 0.0;

    EXPECT_EQ(player.GetPopulationCap(), 42);
}

TEST(PlayerEconomyTests, AddManpowerRespectsPopulationCapIncludingWorkersAndSoldiers)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(1)));
    ASSERT_NE(village, nullptr);
    village->population.populationCap = 10;

    player.strategicResources.Set(StrategicResourceType::Workers, 3);
    player.strategicResources.Set(StrategicResourceType::Soldiers, 2);

    EXPECT_DOUBLE_EQ(player.AddManpower(10.0), 5.0);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 5.0);
    EXPECT_DOUBLE_EQ(player.GetTotalPopulation(), 10.0);
}

TEST(PlayerEconomyTests, AutoAssignWorkersMovesManpowerIntoProductionBuilding)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    Woodcutter building{7};
    building.owner = &player;
    building.workers.capacity = 4;
    building.workers.assigned = 1;
    player.strategicResources.Set(StrategicResourceType::Manpower, 10);

    EXPECT_EQ(player.AutoAssignWorkers(&building), 3);
    EXPECT_EQ(building.workers.assigned, 4);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 7.0);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Workers), 3.0);
}

TEST(PlayerEconomyTests, ManpowerGrowthAppliesToPopulationComponent)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(1)));
    ASSERT_NE(village, nullptr);
    village->population.manpowerRate = 0.2;

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::ManpowerRate, 0.0, 1.12, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:manpower_boost"});

    EXPECT_NEAR(player.ResolveStat(village->population.manpowerRate, village), 0.224, 0.0001);
}

TEST(PlayerEconomyTests, TelemetryDoesNotReportTheoreticalProduction)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    Woodcutter woodcutter{7};
    woodcutter.owner = &player;
    woodcutter.workers.assigned = woodcutter.workers.capacity.GetBase();

    player.UpdateEconomyTelemetry(1.0);

    EXPECT_FALSE(player.GetProvinceEconomy()->economyTelemetry.current.productionRatesPerMinute.contains(ResourceType::WOOD));
}

TEST(PlayerEconomyTests, TelemetryRecordsActualProductionAndInputConsumption)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    LumberMill lumberMill{7};
    lumberMill.owner = &player;
    lumberMill.workers.assigned = lumberMill.workers.capacity.GetBase();

    const int woodPerCycle = lumberMill.production.ingredients[ResourceType::WOOD];
    ASSERT_GT(woodPerCycle, 0);
    lumberMill.production.inputBuffers[ResourceType::WOOD].SetStoredAmount(woodPerCycle);

    lumberMill.production.Produce(lumberMill, 0.01);
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_GE(player.GetProvinceEconomy()->economyTelemetry.current.consumptionRatesPerMinute[ResourceType::WOOD],
              woodPerCycle * 60);
    EXPECT_FALSE(player.GetProvinceEconomy()->economyTelemetry.current.productionRatesPerMinute.contains(ResourceType::PLANKS));

    lumberMill.production.elapsed = lumberMill.production.GetModifiedCycleTime(lumberMill);
    lumberMill.production.Produce(lumberMill, 0.01);
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_GT(player.GetProvinceEconomy()->economyTelemetry.current.productionRatesPerMinute[ResourceType::PLANKS], 0);
}

TEST(PlayerEconomyTests, TelemetryDoesNotRecordBuildCostAsConsumption)
{
    // 2026-07-19 (user report): a build cost is a one-time spend, not a
    // recurring per-minute drain — recording it into the same rolling-window
    // telemetry as real production-input/population consumption made every
    // construction spike that resource's reported "consumption rate" for the
    // rest of the window, which fed straight into DiagnoseResourceNeed's
    // urgency and kept pushing the AI to build MORE producers of whatever it
    // had just spent on building (observed: AI kept stacking Woodcutters).
    // The ai_economy_bias virtual-consumption mechanism already exists
    // specifically to represent non-constant costs like this one — real
    // telemetry recording it too was double-counting.
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* storage = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    ASSERT_NE(storage, nullptr);
    storage->storage.buffers[ResourceType::WOOD].SetStoredAmount(5);

    ASSERT_TRUE(player.TryPayBuildCost({{ResourceType::WOOD, 3}}));
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_EQ(player.GetProvinceEconomy()->economyTelemetry.current.consumptionRatesPerMinute[ResourceType::WOOD], 0);
}

TEST(PlayerEconomyTests, TelemetryDoesNotRecordBuildingPlacementCostAsConsumption)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* storage = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 4}), &player, std::make_unique<Headquarters>(1)));
    ASSERT_NE(storage, nullptr);
    storage->storage.buffers[ResourceType::STONE] = ResourceBuffer{ResourceType::STONE, 10};
    storage->storage.buffers[ResourceType::STONE].SetStoredAmount(10);

    const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
    ASSERT_FALSE(roadDefinition.buildCosts.empty());
    ASSERT_NE(player.Build<Road>(map.GetIdFromCoords({0, 0})), nullptr);
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_EQ(player.GetProvinceEconomy()->economyTelemetry.current.consumptionRatesPerMinute[ResourceType::STONE], 0);
}

TEST(PlayerEconomyTests, BuildCostModifierReducesEffectiveBuildCosts)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
    ASSERT_FALSE(roadDefinition.buildCosts.empty());
    auto baseline = player.GetEffectiveBuildCosts(roadDefinition);
    ASSERT_EQ(baseline.size(), roadDefinition.buildCosts.size());
    for (size_t i = 0; i < baseline.size(); i++)
    {
        EXPECT_EQ(baseline[i].type, roadDefinition.buildCosts[i].type);
        EXPECT_EQ(baseline[i].amount, roadDefinition.buildCosts[i].amount);
    }

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::BuildCost,
        0.0,
        0.5,
        BalanceModifierScope::Global(),
        std::nullopt,
        std::nullopt,
        "test:build_cost_discount"});

    auto discounted = player.GetEffectiveBuildCosts(roadDefinition);
    ASSERT_EQ(discounted.size(), roadDefinition.buildCosts.size());
    for (size_t i = 0; i < discounted.size(); i++)
    {
        EXPECT_EQ(discounted[i].type, roadDefinition.buildCosts[i].type);
        EXPECT_EQ(discounted[i].amount, static_cast<int>(std::round(roadDefinition.buildCosts[i].amount * 0.5)));
    }
}

TEST(PlayerEconomyTests, BuildRequirementFailuresUseCatalogDisplayNames)
{
    TileMap map;
    Player player{0, map};

    const auto* technology = FindTechnologyDefinition("catapult_construction");
    ASSERT_NE(technology, nullptr);
    ASSERT_FALSE(technology->name.empty());

    const auto& university = GetBuildingDefinition(BuildingType::University);
    ASSERT_FALSE(university.requiredFocuses.empty());

    BuildingDefinition definition;
    definition.requiredTechnologies = {technology->id};
    definition.requiredFocuses = university.requiredFocuses;

    const auto failures = player.GetBuildRequirementFailures(definition);

    EXPECT_NE(std::find(failures.begin(), failures.end(),
                        "Requires technology: " + technology->name), failures.end());
    EXPECT_EQ(std::find(failures.begin(), failures.end(),
                        "Requires technology: " + technology->id), failures.end());

    for (const auto& focusId : definition.requiredFocuses)
    {
        const auto* focus = FindFocusDefinition(focusId);
        ASSERT_NE(focus, nullptr) << focusId;
        ASSERT_FALSE(focus->name.empty()) << focusId;
        EXPECT_NE(std::find(failures.begin(), failures.end(),
                            "Requires focus: " + focus->name), failures.end());
        if (focus->name != focusId)
        {
            EXPECT_EQ(std::find(failures.begin(), failures.end(),
                                "Requires focus: " + focusId), failures.end());
        }
    }
}

