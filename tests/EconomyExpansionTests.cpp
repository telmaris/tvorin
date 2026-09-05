#include "data/Resource.h"
#include "core/GameWorld.h"
#include "economy/BuildingConfig.h"
#include "economy/ProductionBuildings.h"
#include "research/Technology.h"
#include "simulation/MapGenerator.h"
#include "warfare/UnitDefinition.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

TEST(EconomyExpansionTests, ResourceDisplayNamesAreHumanReadable)
{
    EXPECT_EQ(ResourceDisplayName(ResourceType::FOOD_PROVISIONS), "Food Provisions");
    EXPECT_EQ(ResourceDisplayName(ResourceType::COPPER_ORE), "Copper Ore");
    EXPECT_EQ(ResourceDisplayName(ResourceType::MECHANICAL_PARTS), "Mechanical Parts");
    EXPECT_EQ(ResourceDisplayName(ResourceType::WOOD), "Wood");
}

TEST(EconomyExpansionTests, ActiveBuildPanelContainsNewChainsAndHidesDormantPrototypes)
{
    const auto& types = GetBuildableBuildingTypes();
    auto contains = [&](BuildingType type)
    {
        return std::find(types.begin(), types.end(), type) != types.end();
    };

    EXPECT_TRUE(contains(BuildingType::AnimalFarm));
    EXPECT_TRUE(contains(BuildingType::Kiln));
    EXPECT_TRUE(contains(BuildingType::Copperworks));
    EXPECT_TRUE(contains(BuildingType::UrbanWorkshop));
    EXPECT_TRUE(contains(BuildingType::SiegeWorkshop));
    EXPECT_FALSE(contains(BuildingType::Mint));
    EXPECT_FALSE(contains(BuildingType::Glassworks));
    EXPECT_FALSE(contains(BuildingType::Powderworks));
}

TEST(EconomyExpansionTests, CopperworksExposesAllFourPilotSinks)
{
    const auto& definition = GetBuildingDefinition(BuildingType::Copperworks);
    ASSERT_EQ(definition.recipes.size(), 4u);

    std::vector<ResourceType> products;
    for (const auto& recipe : definition.recipes)
    {
        ASSERT_EQ(recipe.production.outputs.size(), 1u);
        products.push_back(recipe.production.outputs.front().type);
    }

    EXPECT_NE(std::find(products.begin(), products.end(), ResourceType::COPPERWARE), products.end());
    EXPECT_NE(std::find(products.begin(), products.end(), ResourceType::COPPER_VESSEL), products.end());
    EXPECT_NE(std::find(products.begin(), products.end(), ResourceType::COPPER_PIPE), products.end());
    EXPECT_NE(std::find(products.begin(), products.end(), ResourceType::MECHANICAL_PARTS), products.end());
}

TEST(EconomyExpansionTests, ClaySandAndFibreHaveMultipleUsefulOutputs)
{
    const auto& kiln = GetBuildingDefinition(BuildingType::Kiln);
    const auto& paperworks = GetBuildingDefinition(BuildingType::Paperworks);

    auto recipeProduces = [](const BuildingDefinition& definition, ResourceType type)
    {
        return std::any_of(definition.recipes.begin(), definition.recipes.end(),
                           [type](const ProductionRecipeDefinition& recipe)
                           {
                               return std::any_of(recipe.production.outputs.begin(),
                                                  recipe.production.outputs.end(),
                                                  [type](const ResourceAmountDefinition& output)
                                                  {
                                                      return output.type == type;
                                                  });
                           });
    };
    auto recipeConsumes = [](const BuildingDefinition& definition, ResourceType type)
    {
        return std::any_of(definition.recipes.begin(), definition.recipes.end(),
                           [type](const ProductionRecipeDefinition& recipe)
                           {
                               return std::any_of(recipe.production.inputs.begin(),
                                                  recipe.production.inputs.end(),
                                                  [type](const ResourceAmountDefinition& input)
                                                  {
                                                      return input.type == type;
                                                  });
                           });
    };

    EXPECT_TRUE(recipeProduces(kiln, ResourceType::POTTERY));
    EXPECT_TRUE(recipeProduces(kiln, ResourceType::BRICKS));
    EXPECT_TRUE(recipeProduces(kiln, ResourceType::GLASS));
    EXPECT_TRUE(recipeConsumes(kiln, ResourceType::CLAY));
    EXPECT_TRUE(recipeConsumes(kiln, ResourceType::SAND));
    EXPECT_TRUE(recipeConsumes(paperworks, ResourceType::FIBRE));
}

TEST(EconomyExpansionTests, SettlementUpgradesUseConfiguredStatsAndSupplyPackages)
{
    Village village{1};
    const auto& definition = GetBuildingDefinition(BuildingType::Village);
    auto findLevel = [&](int level) -> const BuildingUpgradeLevelDefinition&
    {
        auto it = std::find_if(definition.upgradeLevels.begin(), definition.upgradeLevels.end(),
            [level](const BuildingUpgradeLevelDefinition& value) { return value.level == level; });
        EXPECT_NE(it, definition.upgradeLevels.end());
        return *it;
    };

    EXPECT_EQ(village.upgrade.maxLevel, 3);
    EXPECT_EQ(village.population.GetActivePopulationCap(), definition.village.populationCap);
    EXPECT_DOUBLE_EQ(village.population.manpowerRate.GetBase(), definition.village.manpowerRate);
    EXPECT_TRUE(village.population.RequiresSupply(ResourceType::FOOD_PROVISIONS));
    EXPECT_FALSE(village.population.RequiresSupply(ResourceType::HOUSEHOLD_GOODS));
    EXPECT_EQ(village.population.foodBuffer.bufferSize, 2);

    const auto& townDefinition = findLevel(2);
    ASSERT_TRUE(townDefinition.populationCap.has_value());
    ASSERT_TRUE(townDefinition.manpowerRate.has_value());
    village.upgrade.isUpgrading = true;
    village.upgrade.upgradeRemaining = 0.1;
    village.Update(0.2);
    EXPECT_EQ(village.upgrade.level, 2);
    EXPECT_EQ(village.population.GetActivePopulationCap(), *townDefinition.populationCap);
    EXPECT_DOUBLE_EQ(village.population.manpowerRate.GetBase(), *townDefinition.manpowerRate);
    EXPECT_EQ(village.population.GetSupplyUpkeep(ResourceType::FOOD_PROVISIONS), 3);
    EXPECT_EQ(village.population.GetSupplyUpkeep(ResourceType::HOUSEHOLD_GOODS), 1);
    EXPECT_EQ(village.population.foodBuffer.bufferSize, 4);

    const auto& cityDefinition = findLevel(3);
    ASSERT_TRUE(cityDefinition.populationCap.has_value());
    ASSERT_TRUE(cityDefinition.manpowerRate.has_value());
    village.upgrade.isUpgrading = true;
    village.upgrade.upgradeRemaining = 0.1;
    village.Update(0.2);
    EXPECT_EQ(village.upgrade.level, 3);
    EXPECT_EQ(village.population.GetActivePopulationCap(), *cityDefinition.populationCap);
    EXPECT_DOUBLE_EQ(village.population.manpowerRate.GetBase(), *cityDefinition.manpowerRate);
    EXPECT_EQ(village.population.GetSupplyUpkeep(ResourceType::FOOD_PROVISIONS), 10);
    EXPECT_EQ(village.population.GetSupplyUpkeep(ResourceType::HOUSEHOLD_GOODS), 3);
    EXPECT_EQ(village.population.GetSupplyUpkeep(ResourceType::URBAN_GOODS), 1);
    EXPECT_EQ(village.population.foodBuffer.bufferSize, 11);

    village.population.urbanSupplyLevel = 0.0;
    EXPECT_EQ(village.population.GetActivePopulationCap(), *townDefinition.populationCap);
    village.population.householdSupplyLevel = 0.0;
    EXPECT_EQ(village.population.GetActivePopulationCap(), definition.village.populationCap);
}

TEST(EconomyExpansionTests, CityTierAndLocalSupplyBuffersSurviveSaveLoad)
{
    GameWorld world;
    MapParameters params;
    params.sizePreset = MapSizePreset::S;
    params.aiOpponentCount = 0;
    params.seed = 1701;
    world.InitWorld("settlement-save-test", nullptr, params);

    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    Village* village = nullptr;
    for (Building* building : player->GetTrackedBuildings())
        if ((village = dynamic_cast<Village*>(building)) != nullptr)
            break;
    ASSERT_NE(village, nullptr);

    village->upgrade.level = 3;
    village->population.SetSettlementLevel(3);
    village->population.foodSupplyLevel = 0.8;
    village->population.householdSupplyLevel = 0.7;
    village->population.urbanSupplyLevel = 0.6;
    village->population.upkeepTimer = 8.5;
    village->population.householdUpkeepTimer = 17.25;
    village->population.urbanUpkeepTimer = 41.75;
    village->population.foodBuffer.SetStoredAmount(5);
    village->population.householdGoodsBuffer.SetStoredAmount(2);
    village->population.urbanGoodsBuffer.SetStoredAmount(1);
    const int villagePosition = village->positionId;

    const auto path =
        (std::filesystem::temp_directory_path() / "rts_settlement_tier_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr));
    std::filesystem::remove(path);

    Player* loadedPlayer = loaded.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(loadedPlayer, nullptr);
    Village* loadedVillage = nullptr;
    for (Building* building : loadedPlayer->GetTrackedBuildings())
        if (building != nullptr && building->positionId == villagePosition)
            loadedVillage = dynamic_cast<Village*>(building);
    ASSERT_NE(loadedVillage, nullptr);

    EXPECT_EQ(loadedVillage->upgrade.level, 3);
    EXPECT_EQ(loadedVillage->population.settlementLevel, 3);
    const auto& villageDefinition = GetBuildingDefinition(BuildingType::Village);
    auto cityDefinition = std::find_if(
        villageDefinition.upgradeLevels.begin(), villageDefinition.upgradeLevels.end(),
        [](const BuildingUpgradeLevelDefinition& value) { return value.level == 3; });
    ASSERT_NE(cityDefinition, villageDefinition.upgradeLevels.end());
    ASSERT_TRUE(cityDefinition->populationCap.has_value());
    ASSERT_TRUE(cityDefinition->manpowerRate.has_value());
    EXPECT_EQ(loadedVillage->population.populationCap.GetBase(), *cityDefinition->populationCap);
    EXPECT_DOUBLE_EQ(loadedVillage->population.manpowerRate.GetBase(), *cityDefinition->manpowerRate);
    EXPECT_DOUBLE_EQ(loadedVillage->population.upkeepTimer, 8.5);
    EXPECT_DOUBLE_EQ(loadedVillage->population.householdUpkeepTimer, 17.25);
    EXPECT_DOUBLE_EQ(loadedVillage->population.urbanUpkeepTimer, 41.75);
    EXPECT_DOUBLE_EQ(loadedVillage->population.foodSupplyLevel, 0.8);
    EXPECT_DOUBLE_EQ(loadedVillage->population.householdSupplyLevel, 0.7);
    EXPECT_DOUBLE_EQ(loadedVillage->population.urbanSupplyLevel, 0.6);
    EXPECT_EQ(loadedVillage->population.foodBuffer.buffer.size(), 5u);
    EXPECT_EQ(loadedVillage->population.householdGoodsBuffer.buffer.size(), 2u);
    EXPECT_EQ(loadedVillage->population.urbanGoodsBuffer.buffer.size(), 1u);
}

TEST(EconomyExpansionTests, NewRosterCoversRangedCavalryCounterAndSiegeRoles)
{
    const UnitDefinition* archer = FindUnitDefinition("archer");
    const UnitDefinition* heavyArcher = FindUnitDefinition("heavy_archer");
    const UnitDefinition* cavalry = FindUnitDefinition("light_cavalry");
    const UnitDefinition* heavyCavalry = FindUnitDefinition("knight");
    const UnitDefinition* spearman = FindUnitDefinition("spearman");
    const UnitDefinition* catapult = FindUnitDefinition("catapult");

    ASSERT_NE(archer, nullptr);
    ASSERT_NE(heavyArcher, nullptr);
    ASSERT_NE(cavalry, nullptr);
    ASSERT_NE(heavyCavalry, nullptr);
    ASSERT_NE(spearman, nullptr);
    ASSERT_NE(catapult, nullptr);
    EXPECT_GT(archer->attackRange, 0.6);
    EXPECT_GT(heavyArcher->attackRange, archer->attackRange);
    EXPECT_TRUE(cavalry->cavalry);
    EXPECT_TRUE(heavyCavalry->cavalry);
    EXPECT_NE(std::find_if(heavyCavalry->cost.begin(), heavyCavalry->cost.end(),
                           [](const UnitCostEntry& cost)
                           {
                               return cost.type == ResourceType::HORSE;
                           }),
              heavyCavalry->cost.end());
    EXPECT_GT(spearman->antiCavalryMultiplier, 1.0);
    EXPECT_GT(catapult->siegePower, catapult->fieldAttack);
    EXPECT_GT(catapult->areaTargets, 1);
}

TEST(EconomyExpansionTests, NewResourcesHaveEconomicCategories)
{
    EXPECT_EQ(ResourceCategoryOf(ResourceType::CLAY), ResourceCategory::Mineral);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::CATTLE), ResourceCategory::Livestock);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::ROPE), ResourceCategory::Textile);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::HOUSEHOLD_GOODS), ResourceCategory::SettlementSupply);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::COPPERWARE), ResourceCategory::CraftedGood);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::HEAVY_BOW), ResourceCategory::Bow);
}

TEST(EconomyExpansionTests, TechnologyTreeUsesOnlyParseableActiveResourceCosts)
{
    const auto definitions =
        LoadTechnologyDefinitionsFromFile("assets/data/technologies.rtsdata");
    ASSERT_FALSE(definitions.empty());

    for (const auto& definition : definitions)
        for (const auto& cost : definition.costs)
            EXPECT_NE(cost.type, ResourceType::Null)
                << "unparseable resource cost in technology " << definition.id;

    const auto assaying = std::find_if(definitions.begin(), definitions.end(),
                                      [](const TechnologyDefinition& definition)
                                      {
                                          return definition.id == "assaying_and_cupellation";
                                      });
    ASSERT_NE(assaying, definitions.end());
    ASSERT_FALSE(assaying->modifiers.empty());
    EXPECT_EQ(assaying->costs.back().type, ResourceType::COPPERWARE);
    ASSERT_TRUE(assaying->modifiers.front().buildingType.has_value());
    EXPECT_EQ(assaying->modifiers.front().buildingType.value(), BuildingType::Copperworks);
}
