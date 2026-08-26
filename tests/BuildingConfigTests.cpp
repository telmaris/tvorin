#include "economy/BuildingConfig.h"
#include "economy/ProductionBuildings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
    // Writes a temporary building data file for parser-level tests.
    std::filesystem::path WriteBuildingFixture(const std::string& text)
    {
        const auto path = std::filesystem::temp_directory_path() / "rts_building_fixture.rtsdata";
        std::ofstream file(path);
        file << text;
        return path;
    }
}

TEST(BuildingConfigTests, CoreDefinitionsAreLoaded)
{
    const auto& definitions = GetBuildingDefinitions();
    ASSERT_FALSE(definitions.empty());

    const auto& hq = GetBuildingDefinition(BuildingType::Headquarters);
    EXPECT_EQ(hq.type, BuildingType::Headquarters);
    EXPECT_EQ(hq.footprint.x, 4);
    EXPECT_EQ(hq.footprint.y, 4);
    EXPECT_FALSE(hq.storageBuffers.empty());
    auto paper = std::find_if(hq.storageBuffers.begin(), hq.storageBuffers.end(), [](const ResourceBufferDefinition& buffer)
    {
        return buffer.type == ResourceType::PAPER;
    });
    ASSERT_NE(paper, hq.storageBuffers.end());
    EXPECT_GT(paper->capacity, 0);

    const auto& road = GetBuildingDefinition(BuildingType::Road);
    EXPECT_EQ(road.road.upgradeLevel, 1);
    EXPECT_GT(road.road.maxCapacity, 0);
    EXPECT_GT(road.road.speedModifier, 0.0);

    // A bridge is the mandatory early-game crossing over the military track;
    // it must not be hidden behind a late research prerequisite.
    const auto& bridge = GetBuildingDefinition(BuildingType::Bridge);
    EXPECT_TRUE(bridge.requiredTechnologies.empty());
    ASSERT_EQ(bridge.buildCosts.size(), 2u);
    EXPECT_EQ(bridge.buildCosts[0].type, ResourceType::PLANKS);
    EXPECT_EQ(bridge.buildCosts[0].amount, 6);
    EXPECT_EQ(bridge.buildCosts[1].type, ResourceType::STONE);
    EXPECT_EQ(bridge.buildCosts[1].amount, 6);

    const auto& university = GetBuildingDefinition(BuildingType::University);
    EXPECT_EQ(university.production.workerCapacity, 60);

    // Decisions can gate only access to the University. Buildings otherwise
    // use technology prerequisites, leaving the decision tree free for its
    // strategic effects.
    for (const auto& definition : definitions)
    {
        if (definition.type == BuildingType::University)
            EXPECT_FALSE(definition.requiredFocuses.empty());
        else
            EXPECT_TRUE(definition.requiredFocuses.empty()) << definition.name;
    }
}

TEST(BuildingConfigTests, BuildPanelListsContainExpectedTypes)
{
    const auto& buildings = GetBuildableBuildingTypes();
    const auto& roads = GetBuildableRoadTypes();

    EXPECT_NE(std::find(buildings.begin(), buildings.end(), BuildingType::Woodcutter), buildings.end());
    EXPECT_NE(std::find(buildings.begin(), buildings.end(), BuildingType::Barracks), buildings.end());
    EXPECT_EQ(std::find(buildings.begin(), buildings.end(), BuildingType::Headquarters), buildings.end());

    // B6 (docs/work_plan_2026-07-13.md): Bridge shares the road build panel.
    ASSERT_EQ(roads.size(), 2u);
    EXPECT_EQ(roads.front(), BuildingType::Road);
    EXPECT_NE(std::find(roads.begin(), roads.end(), BuildingType::Bridge), roads.end());
}

TEST(BuildingConfigTests, BridgeUsesTheSameUpgradeTiersAsRoad)
{
    const auto& road = GetBuildingDefinition(BuildingType::Road);
    const auto& bridge = GetBuildingDefinition(BuildingType::Bridge);

    ASSERT_EQ(bridge.upgradeLevels.size(), road.upgradeLevels.size());
    ASSERT_FALSE(bridge.upgradeLevels.empty());
    for (size_t i = 0; i < road.upgradeLevels.size(); ++i)
    {
        EXPECT_EQ(bridge.upgradeLevels[i].level, road.upgradeLevels[i].level);
        EXPECT_EQ(bridge.upgradeLevels[i].cost.size(), road.upgradeLevels[i].cost.size());
        EXPECT_DOUBLE_EQ(bridge.upgradeLevels[i].buildTime, road.upgradeLevels[i].buildTime);
    }
}

TEST(BuildingConfigTests, WeaponAndMetalBuildingsUseRequestedDefaultRecipes)
{
    const auto& foundry = GetBuildingDefinition(BuildingType::Foundry);
    ASSERT_FALSE(foundry.recipes.empty());
    EXPECT_EQ(foundry.recipes.front().name, "Iron");
    ASSERT_FALSE(foundry.recipes.front().production.outputs.empty());
    EXPECT_EQ(foundry.recipes.front().production.outputs.front().type, ResourceType::IRON);

    const auto& bowyer = GetBuildingDefinition(BuildingType::Bowyer);
    ASSERT_FALSE(bowyer.recipes.empty());
    EXPECT_EQ(bowyer.recipes.front().name, "Arrows");
    ASSERT_FALSE(bowyer.recipes.front().production.outputs.empty());
    EXPECT_EQ(bowyer.recipes.front().production.outputs.front().type, ResourceType::ARROWS);
}

TEST(BuildingConfigTests, InnProducesThreeFoodProvisionsPerCycle)
{
    const auto& inn = GetBuildingDefinition(BuildingType::Inn);
    auto food = std::find_if(inn.production.outputs.begin(), inn.production.outputs.end(),
        [](const ResourceAmountDefinition& output)
        {
            return output.type == ResourceType::FOOD_PROVISIONS;
        });
    ASSERT_NE(food, inn.production.outputs.end());
    EXPECT_EQ(food->amount, 3);
}

TEST(BuildingConfigTests, TerrainSpecificProductionCanBeFound)
{
    const auto* stoneMine = FindTerrainProductionDefinition(BuildingType::Mine, TileType::STONE);
    ASSERT_NE(stoneMine, nullptr);
    ASSERT_FALSE(stoneMine->production.outputs.empty());
    EXPECT_EQ(stoneMine->production.outputs.front().type, ResourceType::STONE);

    EXPECT_EQ(FindTerrainProductionDefinition(BuildingType::Mine, TileType::GRASS), nullptr);
}

TEST(BuildingConfigTests, AppliesDefinitionsToRuntimeBuildings)
{
    Woodcutter production{1};
    const auto& mill = GetBuildingDefinition(BuildingType::LumberMill);
    ApplyBuildingDefinition(production, mill);
    ApplyProductionDefinition(production, mill.production);

    EXPECT_EQ(production.name, mill.name);
    EXPECT_EQ(production.buildingType, BuildingType::LumberMill);
    EXPECT_FALSE(production.production.ingredients.empty());
    EXPECT_FALSE(production.production.products.empty());

    StorageBuilding storage{2};
    const auto& hq = GetBuildingDefinition(BuildingType::Headquarters);
    ApplyStorageDefinition(storage, hq);
    EXPECT_FALSE(storage.storage.buffers.empty());

}

TEST(BuildingConfigTests, LoadsBuildingDataFileWithProductionStorageRoadVillageAndMilitarySections)
{
    const auto path = WriteBuildingFixture(R"DATA(
# parser fixture
building Headquarters
    name "Custom HQ"
    tag "[HQ]"
    texture "assets/custom_hq.png"
    build_cost "Starting building"
    build_time 0
    transport_time 1.5
    dispatch_delay 0.25
    footprint 3 3
    texture_id 42
    storage WOOD 100 50
    storage STONE 90
end
building Woodcutter
    name "Fast Woodcutter"
    tag "[Fast]"
    texture "assets/fast.png"
    placement_category wood
    build_cost WOOD 11
    build_cost STONE 4
    build_time 6
    footprint 2 2
    texture_id 7
    production
        workers 4
        cycle_time 2.5
        input WATER 1
        output WOOD 2
        input_buffer WATER 5
        output_buffer WOOD 8
    end
end
building Mine
    terrain_production IRON_ORE
        workers 5
        cycle_time 3.5
        output IRON_ORE 3
        output_buffer IRON_ORE 12
    end
end
building Road
    road upgrade_level 2 max_capacity 9 speed_modifier 1.75
end
building Village
    village manpower_rate 0.4 population_cap 120 upkeep_interval 9 food_package_upkeep 2
    upgrade level 2 population_cap 360 manpower_rate 0.9 cost WOOD 5 build_time 3
end
)DATA");

    const auto definitions = LoadBuildingDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 5u);

    const auto& hq = definitions[0];
    EXPECT_EQ(hq.type, BuildingType::Headquarters);
    EXPECT_EQ(hq.name, "Custom HQ");
    EXPECT_EQ(hq.texturePath, "assets/custom_hq.png");
    EXPECT_EQ(hq.buildCostText, "Starting building");
    EXPECT_DOUBLE_EQ(hq.transportTime, 1.5);
    EXPECT_DOUBLE_EQ(hq.dispatchDelay, 0.25);
    EXPECT_EQ(hq.textureId, 42);
    ASSERT_EQ(hq.storageBuffers.size(), 2u);
    EXPECT_EQ(hq.storageBuffers[0].type, ResourceType::WOOD);
    EXPECT_EQ(hq.storageBuffers[0].initialAmount, 50);
    EXPECT_EQ(hq.storageBuffers[1].initialAmount, 0);

    const auto& woodcutter = definitions[1];
    EXPECT_EQ(woodcutter.placementCategory, BuildingPlacementCategory::Wood);
    EXPECT_EQ(woodcutter.buildCostText, "WOOD 11, STONE 4");
    ASSERT_EQ(woodcutter.buildCosts.size(), 2u);
    EXPECT_EQ(woodcutter.production.workerCapacity, 4);
    EXPECT_DOUBLE_EQ(woodcutter.production.cycleTime, 2.5);
    ASSERT_EQ(woodcutter.production.inputs.size(), 1u);
    EXPECT_EQ(woodcutter.production.inputs[0].type, ResourceType::WATER);
    ASSERT_EQ(woodcutter.production.outputs.size(), 1u);
    EXPECT_EQ(woodcutter.production.outputs[0].amount, 2);
    ASSERT_EQ(woodcutter.production.inputBuffers.size(), 1u);
    ASSERT_EQ(woodcutter.production.outputBuffers.size(), 1u);

    const auto& mine = definitions[2];
    ASSERT_EQ(mine.terrainProductions.size(), 1u);
    EXPECT_EQ(mine.terrainProductions[0].tileType, TileType::IRON_ORE);
    EXPECT_EQ(mine.terrainProductions[0].production.workerCapacity, 5);

    const auto& road = definitions[3];
    EXPECT_EQ(road.road.upgradeLevel, 2);
    EXPECT_EQ(road.road.maxCapacity, 9);
    EXPECT_DOUBLE_EQ(road.road.speedModifier, 1.75);

    const auto& village = definitions[4];
    EXPECT_DOUBLE_EQ(village.village.manpowerRate, 0.4);
    EXPECT_EQ(village.village.populationCap, 120);
    EXPECT_DOUBLE_EQ(village.village.foodPackageUpkeep, 2.0);
    ASSERT_EQ(village.upgradeLevels.size(), 1u);
    EXPECT_EQ(village.upgradeLevels[0].level, 2);
    ASSERT_TRUE(village.upgradeLevels[0].populationCap.has_value());
    EXPECT_EQ(*village.upgradeLevels[0].populationCap, 360);
    ASSERT_TRUE(village.upgradeLevels[0].manpowerRate.has_value());
    EXPECT_DOUBLE_EQ(*village.upgradeLevels[0].manpowerRate, 0.9);
}

TEST(BuildingConfigTests, ProductionChainsHaveThematicPlacementCategories)
{
    EXPECT_EQ(GetBuildingDefinition(BuildingType::Woodcutter).placementCategory,
              BuildingPlacementCategory::Wood);
    EXPECT_EQ(GetBuildingDefinition(BuildingType::LumberMill).placementCategory,
              BuildingPlacementCategory::Wood);
    EXPECT_EQ(GetBuildingDefinition(BuildingType::Mine).placementCategory,
              BuildingPlacementCategory::Metal);
    EXPECT_EQ(GetBuildingDefinition(BuildingType::Foundry).placementCategory,
              BuildingPlacementCategory::Metal);
    EXPECT_EQ(GetBuildingDefinition(BuildingType::WheatFarm).placementCategory,
              BuildingPlacementCategory::Food);
    EXPECT_EQ(GetBuildingDefinition(BuildingType::Bakery).placementCategory,
              BuildingPlacementCategory::Food);
    EXPECT_EQ(GetBuildingDefinition(BuildingType::Barracks).placementCategory,
              BuildingPlacementCategory::Military);
    EXPECT_EQ(GetBuildingDefinition(BuildingType::DefenseTower).placementCategory,
              BuildingPlacementCategory::Military);
}

TEST(BuildingConfigTests, RecipeTechnologyAndFocusRequirementsAreLoaded)
{
    const auto path = WriteBuildingFixture(R"DATA(
building Smith
    recipe "Refined Tools"
        requires_tech waterwheel_gearing
        requires_focus crown_manufactories
        workers 6
        cycle_time 8
        input IRON 1
        output TOOLS 2
    end
end
)DATA");

    const auto definitions = LoadBuildingDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 1u);
    ASSERT_EQ(definitions.front().recipes.size(), 1u);
    const auto& recipe = definitions.front().recipes.front();
    ASSERT_EQ(recipe.requiredTechnologies.size(), 1u);
    EXPECT_EQ(recipe.requiredTechnologies.front(), "waterwheel_gearing");
    ASSERT_EQ(recipe.requiredFocuses.size(), 1u);
    EXPECT_EQ(recipe.requiredFocuses.front(), "crown_manufactories");
}

TEST(BuildingConfigTests, MissingBuildingDataUsesBuiltInDefaults)
{
    const auto definitions = LoadBuildingDefinitionsFromFile("missing_building_fixture.rtsdata");

    EXPECT_NE(std::find_if(definitions.begin(), definitions.end(), [](const BuildingDefinition& definition)
    {
        return definition.type == BuildingType::Headquarters;
    }), definitions.end());
}

TEST(BuildingConfigTests, UpgradeLookupRequiresAnExactValidNextLevel)
{
    BuildingDefinition definition;
    definition.type = BuildingType::Road;
    definition.upgradeLevels = {
        BuildingUpgradeLevelDefinition{2, {{ResourceType::STONE, 2}}, 5.0, {}, {}, {}},
        BuildingUpgradeLevelDefinition{3, {{ResourceType::STONE, 3}}, -1.0, {}, {}, {}}};

    ASSERT_NE(FindUpgradeLevelDefinition(definition, 2), nullptr);
    EXPECT_EQ(FindUpgradeLevelDefinition(definition, 3), nullptr);
    EXPECT_FALSE(IsValidUpgradeLevelDefinition(definition.upgradeLevels[1]));
}
