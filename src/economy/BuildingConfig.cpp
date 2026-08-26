#include "economy/BuildingConfig.h"
#include "data/RtsDataFile.h"

#include <algorithm>
#include <functional>

namespace
{
    constexpr const char* buildingDataPath = "assets/data/buildings.rtsdata";

    // Initializes MakeProduction.
    ProductionDefinition MakeProduction(
        double cycleTime,
        std::vector<ResourceAmountDefinition> inputs,
        std::vector<ResourceAmountDefinition> outputs,
        std::vector<ResourceBufferDefinition> inputBuffers,
        std::vector<ResourceBufferDefinition> outputBuffers)
    {
        return ProductionDefinition{
            cycleTime,
            std::move(inputs),
            std::move(outputs),
            std::move(inputBuffers),
            std::move(outputBuffers)};
    }

    // Initializes MakeDefaultDefinitions.
    std::vector<BuildingDefinition> MakeDefaultDefinitions()
    {
        return {
            BuildingDefinition{
                BuildingType::Headquarters,
                "Headquarters",
                "[Headquarters]",
                "assets/textures/building/generated/headquarters_idle/headquarters_idle_sheet_5x1_4x4.png",
                "Starting building",
                {},
                {4, 4},
                4,
                0.0,
                0.0,
                {},
                {},
                {
                    {ResourceType::IRON, 80, 30},
                    {ResourceType::IRON_ORE, 100, 40},
                    {ResourceType::COPPER_ORE, 80, 0},
                    {ResourceType::COPPER, 80, 0},
                    {ResourceType::WOOD, 160, 120},
                    {ResourceType::PLANKS, 100, 40},
                    {ResourceType::LEATHER, 80, 0},
                    {ResourceType::COAL, 80, 20},
                    {ResourceType::STONE, 160, 120},
                    {ResourceType::WATER, 120, 40},
                    {ResourceType::MEAT, 80, 0},
                    {ResourceType::WHEAT, 100, 20},
                    {ResourceType::FLOUR, 80, 0},
                    {ResourceType::BREAD, 80, 0},
                    {ResourceType::FOOD_PROVISIONS, 100, 20},
                    {ResourceType::PAPER, 80, 0},
                    {ResourceType::TOOLS, 60, 0},
                    {ResourceType::IRON_SWORD, 60, 0},
                    {ResourceType::BOW, 60, 0},
                    {ResourceType::ARROWS, 120, 0},
                    {ResourceType::HORSE, 60, 0},
                    {ResourceType::CLAY, 100, 0},
                    {ResourceType::SAND, 100, 0},
                    {ResourceType::CATTLE, 60, 0},
                    {ResourceType::RAW_HIDE, 80, 0},
                    {ResourceType::TALLOW, 80, 0},
                    {ResourceType::CLOTHES, 80, 0},
                    {ResourceType::POTTERY, 80, 0},
                    {ResourceType::HOUSEHOLD_GOODS, 100, 0},
                    {ResourceType::SOAP, 80, 0},
                    {ResourceType::INK, 60, 0},
                    {ResourceType::BOOKS, 80, 0},
                    {ResourceType::COPPERWARE, 80, 0},
                    {ResourceType::URBAN_GOODS, 100, 0}
                },
                {}, {}, {}, {}, {},
                HqDefinition{500.0, 10.0, 3.0, 3.0, 0.2, 60.0}},
            BuildingDefinition{
                BuildingType::Village,
                "Village",
                "[Village]",
                "assets/textures/building/generated/cottage_idle/cottage_idle_sheet_5x1_4x4_pixellab.png",
                "Cost TBD",
                {{ResourceType::WOOD, 45}, {ResourceType::STONE, 20}, {ResourceType::PLANKS, 12}},
                {4, 4},
                4,
                18.0,
                0.0,
                {},
                {},
                {},
                {},
                {0.2, 80, 60.0, 1.0}},
            BuildingDefinition{
                BuildingType::StorageBuilding,
                "Storage Building",
                "[StorageBuilding]",
                "assets/textures/building/generated/storage_idle_v2/storage_style_v2_pixellab_3x3.png",
                "Cost TBD",
                {{ResourceType::WOOD, 65}, {ResourceType::STONE, 40}, {ResourceType::PLANKS, 20}},
                {3, 3},
                4,
                14.0,
                0.0,
                {},
                {},
                {
                    {ResourceType::IRON, 80, 0},
                    {ResourceType::IRON_ORE, 80, 0},
                    {ResourceType::COPPER_ORE, 80, 0},
                    {ResourceType::COPPER, 80, 0},
                    {ResourceType::WOOD, 160, 0},
                    {ResourceType::PLANKS, 100, 0},
                    {ResourceType::LEATHER, 80, 0},
                    {ResourceType::COAL, 80, 0},
                    {ResourceType::STONE, 160, 0},
                    {ResourceType::WATER, 120, 0},
                    {ResourceType::MEAT, 80, 0},
                    {ResourceType::WHEAT, 100, 0},
                    {ResourceType::FLOUR, 80, 0},
                    {ResourceType::BREAD, 80, 0},
                    {ResourceType::FOOD_PROVISIONS, 100, 0},
                    {ResourceType::PAPER, 80, 0},
                    {ResourceType::TOOLS, 60, 0},
                    {ResourceType::IRON_SWORD, 60, 0},
                    {ResourceType::BOW, 60, 0},
                    {ResourceType::ARROWS, 120, 0},
                    {ResourceType::HORSE, 60, 0},
                    {ResourceType::CLAY, 100, 0},
                    {ResourceType::SAND, 100, 0},
                    {ResourceType::CATTLE, 60, 0},
                    {ResourceType::RAW_HIDE, 80, 0},
                    {ResourceType::TALLOW, 80, 0},
                    {ResourceType::CLOTHES, 80, 0},
                    {ResourceType::POTTERY, 80, 0},
                    {ResourceType::HOUSEHOLD_GOODS, 100, 0},
                    {ResourceType::SOAP, 80, 0},
                    {ResourceType::INK, 60, 0},
                    {ResourceType::BOOKS, 80, 0},
                    {ResourceType::COPPERWARE, 80, 0},
                    {ResourceType::URBAN_GOODS, 100, 0}
                }},
            BuildingDefinition{
                BuildingType::Woodcutter,
                "Woodcutter",
                "[Woodcutter]",
                "assets/textures/building/generated/woodcutter_idle/woodcutter_pixellab_2x2.png",
                "Cost TBD",
                {{ResourceType::WOOD, 25}, {ResourceType::STONE, 10}},
                {2, 2},
                0,
                8.0,
                0.0,
                MakeProduction(
                    5.0,
                    {},
                    {{ResourceType::WOOD, 1}},
                    {},
                    {{ResourceType::WOOD, 3}})},
            BuildingDefinition{
                BuildingType::LumberMill,
                "Lumber Mill",
                "[LumberMill]",
                "assets/textures/building/generated/lumbermill_idle/lumbermill_idle_sheet_5x1_waterwheel.png",
                "Cost TBD",
                {{ResourceType::WOOD, 55}, {ResourceType::STONE, 25}, {ResourceType::PLANKS, 15}},
                {3, 3},
                1,
                16.0,
                0.0,
                MakeProduction(
                    10.0,
                    {{ResourceType::WOOD, 1}},
                    {{ResourceType::PLANKS, 2}},
                    {{ResourceType::WOOD, 8}},
                    {{ResourceType::PLANKS, 16}})},
            BuildingDefinition{
                BuildingType::Mine,
                "Mine",
                "[Mine]",
                "assets/textures/building/generated/mine_idle_v2/mine_idle_2x2.png",
                "Cost TBD",
                {{ResourceType::WOOD, 45}, {ResourceType::STONE, 25}, {ResourceType::PLANKS, 10}},
                {2, 2},
                2,
                12.0,
                0.0,
                {},
                {
                    {TileType::IRON_ORE, MakeProduction(
                        2.0,
                        {},
                        {{ResourceType::IRON_ORE, 2}},
                        {},
                        {{ResourceType::IRON_ORE, 10}})},
                    {TileType::COAL, MakeProduction(
                        2.0,
                        {},
                        {{ResourceType::COAL, 2}},
                        {},
                        {{ResourceType::COAL, 10}})},
                    {TileType::STONE, MakeProduction(
                        4.0,
                        {},
                        {{ResourceType::STONE, 1}},
                        {},
                        {{ResourceType::STONE, 8}})}
                }},
            BuildingDefinition{
                BuildingType::Foundry,
                "Foundry",
                "[Foundry]",
                "assets/textures/building/generated/foundry_idle_v2/foundry_idle_sheet_5x1.png",
                "Cost TBD",
                {{ResourceType::WOOD, 90}, {ResourceType::STONE, 70}, {ResourceType::PLANKS, 35}, {ResourceType::IRON, 20}},
                {3, 3},
                3,
                28.0,
                0.0,
                MakeProduction(
                    2.0,
                    {{ResourceType::IRON_ORE, 1}, {ResourceType::COAL, 1}},
                    {{ResourceType::IRON, 2}},
                {{ResourceType::IRON_ORE, 8}, {ResourceType::COAL, 8}},
                {{ResourceType::IRON, 16}})},
            BuildingDefinition{
                BuildingType::Road,
                "Road",
                "[Road]",
                "assets/textures/building/road.png",
                "Cost TBD",
                {{ResourceType::STONE, 2}},
                {1, 1},
                5,
                1.0,
                1.0,
                {},
                {},
                {},
                {1, 5, 1.0}},
            BuildingDefinition{
                BuildingType::DefenseTower,
                "Defense Tower",
                "[DefenseTower]",
                "assets/textures/building/generated/guard_tower_idle/guard_tower_idle_sheet_2x1.png",
                "Cost TBD",
                {{ResourceType::WOOD, 60}, {ResourceType::STONE, 80}, {ResourceType::PLANKS, 20}},
                {2, 2},
                6,
                24.0,
                0.0,
                {},
                {},
                {{ResourceType::ARROWS, 20, 0}},
                {}, {}, {}, {}, {},
                {},
                TowerDefinition{3.5, 6.0, 1.0, ResourceType::ARROWS, 1, 2}}
        };
    }

    const BuildingDefinition fallbackDefinition{
        BuildingType::Building,
        "Building - Generic",
        "[Building]",
        "",
        "Cost TBD",
        {},
        {1, 1},
        0};

    // Initializes ParseBuildingType.
    BuildingType ParseBuildingType(const std::string& value)
    {
        if (value == "Headquarters") return BuildingType::Headquarters;
        if (value == "Village") return BuildingType::Village;
        if (value == "StorageBuilding") return BuildingType::StorageBuilding;
        if (value == "Woodcutter") return BuildingType::Woodcutter;
        if (value == "HuntersHut") return BuildingType::HuntersHut;
        if (value == "LumberMill") return BuildingType::LumberMill;
        if (value == "Mine") return BuildingType::Mine;
        if (value == "Foundry") return BuildingType::Foundry;
        if (value == "Well") return BuildingType::Well;
        if (value == "WheatFarm") return BuildingType::WheatFarm;
        if (value == "Windmill") return BuildingType::Windmill;
        if (value == "Bakery") return BuildingType::Bakery;
        if (value == "Inn") return BuildingType::Inn;
        if (value == "Paperworks") return BuildingType::Paperworks;
        if (value == "Smith") return BuildingType::Smith;
        if (value == "Mint") return BuildingType::Mint;
        if (value == "Glassworks") return BuildingType::Glassworks;
        if (value == "Powderworks") return BuildingType::Powderworks;
        if (value == "University") return BuildingType::University;
        if (value == "Barracks") return BuildingType::Barracks;
        if (value == "Road") return BuildingType::Road;
        if (value == "DefenseTower") return BuildingType::DefenseTower;
        if (value == "Bridge") return BuildingType::Bridge;
        if (value == "AnimalFarm") return BuildingType::AnimalFarm;
        if (value == "Butcher") return BuildingType::Butcher;
        if (value == "Tannery") return BuildingType::Tannery;
        if (value == "Tailor") return BuildingType::Tailor;
        if (value == "Armorer") return BuildingType::Armorer;
        if (value == "HorseStable") return BuildingType::HorseStable;
        if (value == "Kiln") return BuildingType::Kiln;
        if (value == "HouseholdWorkshop") return BuildingType::HouseholdWorkshop;
        if (value == "Soapworks") return BuildingType::Soapworks;
        if (value == "Inkworks") return BuildingType::Inkworks;
        if (value == "Scriptorium") return BuildingType::Scriptorium;
        if (value == "Copperworks") return BuildingType::Copperworks;
        if (value == "UrbanWorkshop") return BuildingType::UrbanWorkshop;
        if (value == "HempFarm") return BuildingType::HempFarm;
        if (value == "Ropery") return BuildingType::Ropery;
        if (value == "Weaver") return BuildingType::Weaver;
        if (value == "Bowyer") return BuildingType::Bowyer;
        if (value == "SpearWorkshop") return BuildingType::SpearWorkshop;
        if (value == "SiegeWorkshop") return BuildingType::SiegeWorkshop;
        return BuildingType::Building;
    }

    BuildingPlacementCategory ParsePlacementCategory(const std::string& value)
    {
        if (value == "core") return BuildingPlacementCategory::Core;
        if (value == "wood") return BuildingPlacementCategory::Wood;
        if (value == "metal") return BuildingPlacementCategory::Metal;
        if (value == "food") return BuildingPlacementCategory::Food;
        if (value == "military") return BuildingPlacementCategory::Military;
        if (value == "knowledge") return BuildingPlacementCategory::Knowledge;
        if (value == "construction") return BuildingPlacementCategory::Construction;
        if (value == "infrastructure") return BuildingPlacementCategory::Infrastructure;
        return BuildingPlacementCategory::None;
    }

    // Initializes ParseResourceType.
    ResourceType ParseResourceType(const std::string& value)
    {
        if (value == "WOOD") return ResourceType::WOOD;
        if (value == "PLANKS") return ResourceType::PLANKS;
        if (value == "COAL") return ResourceType::COAL;
        if (value == "STONE") return ResourceType::STONE;
        if (value == "IRON_ORE") return ResourceType::IRON_ORE;
        if (value == "IRON") return ResourceType::IRON;
        if (value == "COPPER_ORE") return ResourceType::COPPER_ORE;
        if (value == "COPPER") return ResourceType::COPPER;
        if (value == "SILVER_ORE") return ResourceType::SILVER_ORE;
        if (value == "SILVER") return ResourceType::SILVER;
        if (value == "GOLD_ORE") return ResourceType::GOLD_ORE;
        if (value == "GOLD") return ResourceType::GOLD;
        if (value == "LEATHER") return ResourceType::LEATHER;
        if (value == "MEAT") return ResourceType::MEAT;
        if (value == "WHEAT") return ResourceType::WHEAT;
        if (value == "BREAD") return ResourceType::BREAD;
        if (value == "FLOUR") return ResourceType::FLOUR;
        if (value == "WATER") return ResourceType::WATER;
        if (value == "BEER") return ResourceType::BEER;
        if (value == "COINS") return ResourceType::COINS;
        if (value == "FOOD_PROVISIONS") return ResourceType::FOOD_PROVISIONS;
        if (value == "PAPER") return ResourceType::PAPER;
        if (value == "TOOLS") return ResourceType::TOOLS;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "STEEL_SWORD") return ResourceType::STEEL_SWORD;
        if (value == "BOW") return ResourceType::BOW;
        if (value == "ARROWS") return ResourceType::ARROWS;
        if (value == "HORSE") return ResourceType::HORSE;
        if (value == "BRONZE_SWORD") return ResourceType::BRONZE_SWORD;
        if (value == "SPEAR") return ResourceType::SPEAR;
        if (value == "CROSSBOW") return ResourceType::CROSSBOW;
        if (value == "BOLTS") return ResourceType::BOLTS;
        if (value == "WOODEN_SHIELD") return ResourceType::WOODEN_SHIELD;
        if (value == "IRON_SHIELD") return ResourceType::IRON_SHIELD;
        if (value == "LEATHER_ARMOR") return ResourceType::LEATHER_ARMOR;
        if (value == "IRON_ARMOR") return ResourceType::IRON_ARMOR;
        if (value == "TIN_ORE") return ResourceType::TIN_ORE;
        if (value == "TIN") return ResourceType::TIN;
        if (value == "SAND") return ResourceType::SAND;
        if (value == "SULFUR") return ResourceType::SULFUR;
        if (value == "SALTPETER") return ResourceType::SALTPETER;
        if (value == "BRONZE") return ResourceType::BRONZE;
        if (value == "COKE") return ResourceType::COKE;
        if (value == "STEEL") return ResourceType::STEEL;
        if (value == "GLASS") return ResourceType::GLASS;
        if (value == "GUNPOWDER") return ResourceType::GUNPOWDER;
        if (value == "MUSKET") return ResourceType::MUSKET;
        if (value == "CARTRIDGE") return ResourceType::CARTRIDGE;
        if (value == "CLAY") return ResourceType::CLAY;
        if (value == "CATTLE") return ResourceType::CATTLE;
        if (value == "RAW_HIDE") return ResourceType::RAW_HIDE;
        if (value == "TALLOW") return ResourceType::TALLOW;
        if (value == "CLOTHES") return ResourceType::CLOTHES;
        if (value == "POTTERY") return ResourceType::POTTERY;
        if (value == "HOUSEHOLD_GOODS") return ResourceType::HOUSEHOLD_GOODS;
        if (value == "SOAP") return ResourceType::SOAP;
        if (value == "INK") return ResourceType::INK;
        if (value == "BOOKS") return ResourceType::BOOKS;
        if (value == "COPPERWARE") return ResourceType::COPPERWARE;
        if (value == "URBAN_GOODS") return ResourceType::URBAN_GOODS;
        if (value == "HEMP") return ResourceType::HEMP;
        if (value == "FIBRE") return ResourceType::FIBRE;
        if (value == "ROPE") return ResourceType::ROPE;
        if (value == "COPPER_VESSEL") return ResourceType::COPPER_VESSEL;
        if (value == "COPPER_PIPE") return ResourceType::COPPER_PIPE;
        if (value == "MECHANICAL_PARTS") return ResourceType::MECHANICAL_PARTS;
        if (value == "HEAVY_BOW") return ResourceType::HEAVY_BOW;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "HEAVY_ARMOR") return ResourceType::HEAVY_ARMOR;
        if (value == "BRICKS") return ResourceType::BRICKS;
        if (value == "CLOTH") return ResourceType::CLOTH;
        if (value == "BALLISTA") return ResourceType::BALLISTA;
        if (value == "BATTERING_RAM") return ResourceType::BATTERING_RAM;
        if (value == "CATAPULT") return ResourceType::CATAPULT;
        return ResourceType::Null;
    }

    // Initializes ParseTileType.
    TileType ParseTileType(const std::string& value)
    {
        if (value == "GRASS") return TileType::GRASS;
        if (value == "WOOD") return TileType::WOOD;
        if (value == "COAL") return TileType::COAL;
        if (value == "IRON_ORE") return TileType::IRON_ORE;
        if (value == "STONE") return TileType::STONE;
        if (value == "COPPER_ORE") return TileType::COPPER_ORE;
        if (value == "TIN_ORE") return TileType::TIN_ORE;
        if (value == "SILVER_ORE") return TileType::SILVER_ORE;
        if (value == "GOLD_ORE") return TileType::GOLD_ORE;
        if (value == "SAND") return TileType::SAND;
        if (value == "SULFUR") return TileType::SULFUR;
        if (value == "SALTPETER") return TileType::SALTPETER;
        if (value == "CLAY") return TileType::CLAY;
        return TileType::GRASS;
    }

    // Initializes FormatBuildCostText.
    std::string FormatBuildCostText(const std::vector<ResourceAmountDefinition>& costs)
    {
        if (costs.empty())
            return "Free";

        std::string text;
        for (size_t i = 0; i < costs.size(); i++)
        {
            if (i > 0)
                text += ", ";
            text += rt2s(costs[i].type) + " " + std::to_string(costs[i].amount);
        }
        return text;
    }

    // Initializes DefinitionSeed.
    BuildingDefinition DefinitionSeed(BuildingType type)
    {
        for (const auto& definition : MakeDefaultDefinitions())
        {
            if (definition.type == type)
                return definition;
        }

        return fallbackDefinition;
    }

    // Initializes ParseProduction.
    void ParseProduction(
        const std::vector<std::vector<std::string>>& lines,
        size_t& index,
        ProductionDefinition& production,
        std::vector<std::string>* requiredTechnologies = nullptr,
        std::vector<std::string>* requiredFocuses = nullptr)
    {
        production = ProductionDefinition{};

        while (++index < lines.size())
        {
            const auto& tokens = lines[index];
            const auto& command = tokens[0];
            if (command == "end")
                return;

            if (command == "cycle_time" && tokens.size() >= 2)
                production.cycleTime = RtsDataDoubleOr(tokens[1]);
            else if (command == "input" && tokens.size() >= 3)
                production.inputs.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2])});
            else if (command == "output" && tokens.size() >= 3)
                production.outputs.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2])});
            else if (command == "input_buffer" && tokens.size() >= 3)
                production.inputBuffers.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2])});
            else if (command == "output_buffer" && tokens.size() >= 3)
                production.outputBuffers.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2])});
            else if (command == "workers" && tokens.size() >= 2)
                production.workerCapacity = RtsDataIntOr(tokens[1]);
            else if (command == "requires_tech" && tokens.size() >= 2 && requiredTechnologies != nullptr)
                requiredTechnologies->push_back(tokens[1]);
            else if (command == "requires_focus" && tokens.size() >= 2 && requiredFocuses != nullptr)
                requiredFocuses->push_back(tokens[1]);
        }
    }

    ProductionRecipeRuntime MakeRuntimeRecipe(const ProductionRecipeDefinition& definition)
    {
        ProductionRecipeRuntime recipe;
        recipe.name = definition.name;
        recipe.cycleTime = definition.production.cycleTime;
        recipe.workerCapacity = definition.production.workerCapacity;
        recipe.requiredTechnologies = definition.requiredTechnologies;
        recipe.requiredFocuses = definition.requiredFocuses;
        for (const auto& input : definition.production.inputs)
            recipe.inputs[input.type] = input.amount;
        for (const auto& output : definition.production.outputs)
            recipe.outputs[output.type] = output.amount;
        for (const auto& buffer : definition.production.inputBuffers)
            recipe.inputBufferCapacities[buffer.type] = buffer.capacity;
        for (const auto& buffer : definition.production.outputBuffers)
            recipe.outputBufferCapacities[buffer.type] = buffer.capacity;
        return recipe;
    }

    // Initializes ParseKeyValueLine.
    void ParseKeyValueLine(const std::vector<std::string>& tokens, size_t start, const std::function<void(const std::string&, const std::string&)>& setter)
    {
        for (size_t i = start; i + 1 < tokens.size(); i += 2)
            setter(tokens[i], tokens[i + 1]);
    }

    // Initializes ParseBuilding.
    BuildingDefinition ParseBuilding(
        const std::vector<std::vector<std::string>>& lines,
        size_t& index)
    {
        BuildingType type = ParseBuildingType(lines[index][1]);
        BuildingDefinition definition = DefinitionSeed(type);
        definition.type = type;
        definition.production = {};
        definition.recipes.clear();
        definition.terrainProductions.clear();
        definition.storageBuffers.clear();
        definition.buildCosts.clear();
        definition.requiredTechnologies.clear();
        definition.requiredFocuses.clear();
        definition.upgradeLevels.clear();

        while (++index < lines.size())
        {
            const auto& tokens = lines[index];
            const auto& command = tokens[0];
            if (command == "end")
                return definition;

            if (command == "name" && tokens.size() >= 2)
                definition.name = tokens[1];
            else if (command == "tag" && tokens.size() >= 2)
                definition.tag = tokens[1];
            else if (command == "texture" && tokens.size() >= 2)
                definition.texturePath = tokens[1];
            else if (command == "placement_category" && tokens.size() >= 2)
                definition.placementCategory = ParsePlacementCategory(tokens[1]);
            else if (command == "build_cost" && tokens.size() >= 2)
            {
                if (tokens.size() >= 3)
                    definition.buildCosts.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2])});
                else
                    definition.buildCostText = tokens[1];
            }
            else if (command == "requires_tech" && tokens.size() >= 2)
                definition.requiredTechnologies.push_back(tokens[1]);
            else if (command == "requires_focus" && tokens.size() >= 2)
                definition.requiredFocuses.push_back(tokens[1]);
            else if (command == "footprint" && tokens.size() >= 3)
                definition.footprint = {RtsDataIntOr(tokens[1]), RtsDataIntOr(tokens[2])};
            else if (command == "texture_id" && tokens.size() >= 2)
                definition.textureId = RtsDataIntOr(tokens[1]);
            else if (command == "build_time" && tokens.size() >= 2)
                definition.buildTime = RtsDataDoubleOr(tokens[1]);
            else if (command == "transport_time" && tokens.size() >= 2)
                definition.transportTime = RtsDataDoubleOr(tokens[1]);
            else if (command == "dispatch_delay" && tokens.size() >= 2)
                definition.dispatchDelay = std::max(0.0, RtsDataDoubleOr(tokens[1]));
            else if (command == "storage" && tokens.size() >= 3)
            {
                int initialAmount = tokens.size() >= 4 ? RtsDataIntOr(tokens[3]) : 0;
                definition.storageBuffers.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2]), initialAmount});
            }
            else if (command == "production")
                ParseProduction(lines, index, definition.production);
            else if (command == "recipe" && tokens.size() >= 2)
            {
                ProductionRecipeDefinition recipe;
                recipe.name = tokens[1];
                ParseProduction(lines, index, recipe.production,
                                &recipe.requiredTechnologies, &recipe.requiredFocuses);
                definition.recipes.push_back(std::move(recipe));
            }
            else if (command == "terrain_production" && tokens.size() >= 2)
            {
                TerrainProductionDefinition terrainProduction;
                terrainProduction.tileType = ParseTileType(tokens[1]);
                ParseProduction(lines, index, terrainProduction.production);
                definition.terrainProductions.push_back(std::move(terrainProduction));
            }
            else if (command == "road")
            {
                ParseKeyValueLine(tokens, 1, [&](const std::string& key, const std::string& value)
                {
                    if (key == "upgrade_level") definition.road.upgradeLevel = RtsDataIntOr(value);
                    else if (key == "max_capacity") definition.road.maxCapacity = RtsDataIntOr(value);
                    else if (key == "speed_modifier") definition.road.speedModifier = RtsDataDoubleOr(value);
                });
            }
            else if (command == "upgrade" && tokens.size() >= 3 && tokens[1] == "level")
            {
                // Generic player-triggered upgrade tier (Building-level
                // feature, not road-specific — see UpgradeComponent). Named
                // "upgrade level N ..." rather than reusing "upgrade_level" to
                // avoid colliding with the unrelated static `road
                // upgrade_level` key above (that one describes a building
                // TYPE's fixed variant tier; this describes a live,
                // player-triggered per-instance progression).
                //
                // Not a flat key-value line (ParseKeyValueLine assumes
                // single-token values) — "cost" needs a resource type AND an
                // amount.
                BuildingUpgradeLevelDefinition levelDef;
                levelDef.level = RtsDataIntOr(tokens[2]);
                for (size_t i = 3; i < tokens.size(); i++)
                {
                    if (tokens[i] == "cost" && i + 2 < tokens.size())
                    {
                        levelDef.cost.push_back({ParseResourceType(tokens[i + 1]), RtsDataIntOr(tokens[i + 2])});
                        i += 2;
                    }
                    else if (tokens[i] == "build_time" && i + 1 < tokens.size())
                    {
                        levelDef.buildTime = RtsDataDoubleOr(tokens[i + 1]);
                        i += 1;
                    }
                    else if (tokens[i] == "capacity_additive" && i + 1 < tokens.size())
                    {
                        BalanceModifier modifier;
                        modifier.stat = BalanceStat::RoadCapacity;
                        modifier.additive = RtsDataDoubleOr(tokens[i + 1]);
                        levelDef.modifiers.push_back(modifier);
                        i += 1;
                    }
                    else if (tokens[i] == "speed_multiplier" && i + 1 < tokens.size())
                    {
                        BalanceModifier modifier;
                        modifier.stat = BalanceStat::RoadSpeed;
                        modifier.multiplier = RtsDataDoubleOr(tokens[i + 1]);
                        levelDef.modifiers.push_back(modifier);
                        i += 1;
                    }
                    else if (tokens[i] == "population_cap" && i + 1 < tokens.size())
                    {
                        levelDef.populationCap = RtsDataIntOr(tokens[i + 1]);
                        i += 1;
                    }
                    else if (tokens[i] == "manpower_rate" && i + 1 < tokens.size())
                    {
                        levelDef.manpowerRate = RtsDataDoubleOr(tokens[i + 1]);
                        i += 1;
                    }
                }
                definition.upgradeLevels.push_back(std::move(levelDef));
            }
            else if (command == "village")
            {
                ParseKeyValueLine(tokens, 1, [&](const std::string& key, const std::string& value)
                {
                    if (key == "manpower_rate") definition.village.manpowerRate = RtsDataDoubleOr(value);
                    else if (key == "population_cap") definition.village.populationCap = RtsDataIntOr(value);
                    else if (key == "upkeep_interval") definition.village.upkeepInterval = RtsDataDoubleOr(value);
                    else if (key == "food_package_upkeep") definition.village.foodPackageUpkeep = RtsDataDoubleOr(value);
                });
            }
            else if (command == "hq")
            {
                ParseKeyValueLine(tokens, 1, [&](const std::string& key, const std::string& value)
                {
                    if (key == "max_hp") definition.hq.maxHp = RtsDataDoubleOr(value);
                    else if (key == "hard_defense") definition.hq.hardDefense = RtsDataDoubleOr(value);
                    else if (key == "thorns_damage") definition.hq.thornsDamage = RtsDataDoubleOr(value);
                    else if (key == "thorns_interval") definition.hq.thornsInterval = RtsDataDoubleOr(value);
                    else if (key == "capture_stock_fraction") definition.hq.captureStockFraction = RtsDataDoubleOr(value);
                    else if (key == "conquest_ramp_duration") definition.hq.conquestRampDuration = RtsDataDoubleOr(value);
                });
            }
            else if (command == "tower")
            {
                ParseKeyValueLine(tokens, 1, [&](const std::string& key, const std::string& value)
                {
                    if (key == "damage") definition.tower.damage = RtsDataDoubleOr(value);
                    else if (key == "range") definition.tower.range = RtsDataDoubleOr(value);
                    else if (key == "attack_speed") definition.tower.attackSpeed = RtsDataDoubleOr(value);
                    else if (key == "ammo_resource") definition.tower.ammoResource = ParseResourceType(value);
                    else if (key == "ammo_per_shot") definition.tower.ammoPerShot = RtsDataIntOr(value);
                    else if (key == "worker_capacity") definition.tower.workerCapacity = RtsDataIntOr(value);
                });
            }
        }

        return definition;
    }

    // Builds runtime definitions from tokenized data lines.
    std::vector<BuildingDefinition> ParseBuildingDefinitions(const std::vector<std::vector<std::string>>& lines)
    {
        if (lines.empty())
        {
            auto definitions = MakeDefaultDefinitions();
            for (auto& definition : definitions)
                if (!definition.buildCosts.empty())
                    definition.buildCostText = FormatBuildCostText(definition.buildCosts);
            return definitions;
        }

        std::vector<BuildingDefinition> definitions;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i][0] == "building" && lines[i].size() >= 2)
                definitions.push_back(ParseBuilding(lines, i));
        }

        if (definitions.empty())
            definitions = MakeDefaultDefinitions();

        for (auto& definition : definitions)
            if (!definition.buildCosts.empty())
                definition.buildCostText = FormatBuildCostText(definition.buildCosts);

        return definitions;
    }
}

// Loads building definitions from a specific data file.
std::vector<BuildingDefinition> LoadBuildingDefinitionsFromFile(const std::string& path)
{
    return ParseBuildingDefinitions(ReadRtsDataLines(path));
}

// Returns all loaded building definitions.
const std::vector<BuildingDefinition>& GetBuildingDefinitions()
{
    static const std::vector<BuildingDefinition> definitions = LoadBuildingDefinitionsFromFile(buildingDataPath);
    return definitions;
}

int GetMaximumBuildingFootprintOverhang()
{
    int overhang = 0;
    for (const auto& definition : GetBuildingDefinitions())
    {
        overhang = std::max(overhang, std::max(definition.footprint.x, definition.footprint.y) - 1);
    }
    return overhang;
}

// Returns the definition for one building type, or fallback data.
const BuildingDefinition& GetBuildingDefinition(BuildingType type)
{
    for (const auto& definition : GetBuildingDefinitions())
    {
        if (definition.type == type)
            return definition;
    }

    return fallbackDefinition;
}

// Returns building types available in the build panel.
const std::vector<BuildingType>& GetBuildableBuildingTypes()
{
    static const std::vector<BuildingType> types{
        BuildingType::Woodcutter,
        BuildingType::HuntersHut,
        BuildingType::LumberMill,
        BuildingType::Mine,
        BuildingType::Foundry,
        BuildingType::Well,
        BuildingType::WheatFarm,
        BuildingType::Windmill,
        BuildingType::Bakery,
        BuildingType::Inn,
        BuildingType::Paperworks,
        BuildingType::Smith,
        BuildingType::AnimalFarm,
        BuildingType::Butcher,
        BuildingType::Tannery,
        BuildingType::Tailor,
        BuildingType::Armorer,
        BuildingType::HorseStable,
        BuildingType::Kiln,
        BuildingType::HouseholdWorkshop,
        BuildingType::Soapworks,
        BuildingType::Inkworks,
        BuildingType::Scriptorium,
        BuildingType::Copperworks,
        BuildingType::UrbanWorkshop,
        BuildingType::HempFarm,
        BuildingType::Ropery,
        BuildingType::Weaver,
        BuildingType::Bowyer,
        BuildingType::SpearWorkshop,
        BuildingType::SiegeWorkshop,
        BuildingType::University,
        BuildingType::StorageBuilding,
        BuildingType::Village,
        BuildingType::Barracks,
        BuildingType::DefenseTower};
    return types;
}

// Returns road types available in road-build mode.
const std::vector<BuildingType>& GetBuildableRoadTypes()
{
    // B6 (docs/work_plan_2026-07-13.md): Bridge shares the road build panel —
    // it's a selectable option there, placeable only on isMilitaryRoad tiles
    // (TileMap::CanBuildFootprint), same as any other build option's
    // placement rule is enforced through the existing generic machinery.
    static const std::vector<BuildingType> types{
        BuildingType::Road,
        BuildingType::Bridge};
    return types;
}

// Finds the best matching runtime object.
const TerrainProductionDefinition* FindTerrainProductionDefinition(BuildingType type, TileType tileType)
{
    const auto& definition = GetBuildingDefinition(type);
    for (const auto& terrainProduction : definition.terrainProductions)
    {
        if (terrainProduction.tileType == tileType)
            return &terrainProduction;
    }

    return nullptr;
}

// Applies parsed configuration to runtime state.
void ApplyBuildingDefinition(Building& building, const BuildingDefinition& definition)
{
    building.name = definition.name;
    building.tag = definition.tag;
    building.buildingType = definition.type;
    building.textureId = definition.textureId;
    building.footprint = definition.footprint;
    building.transportTime = definition.transportTime;
    building.dispatchDelay = definition.dispatchDelay;
    building.buildTime = definition.buildTime;
}

// Applies parsed configuration to runtime state.
void ApplyProductionDefinition(Building& building, const ProductionDefinition& definition)
{
    auto* production = building.GetComponent<ProductionComponent>();
    auto* workers    = building.GetComponent<WorkerComponent>();
    auto* logistics  = building.GetComponent<LogisticsComponent>();
    if (production == nullptr || workers == nullptr || logistics == nullptr)
        return;

    production->cycleTime = definition.cycleTime;
    production->ingredients.clear();
    production->products.clear();
    production->inputBuffers.clear();
    production->outputBuffers.clear();
    logistics->pendingRequests.clear();

    for (const auto& input : definition.inputs)
        production->ingredients[input.type] = input.amount;

    for (const auto& output : definition.outputs)
        production->products[output.type] = output.amount;

    for (const auto& buffer : definition.inputBuffers)
        production->inputBuffers[buffer.type] = ResourceBuffer{buffer.type, buffer.capacity};

    for (const auto& buffer : definition.outputBuffers)
        production->outputBuffers[buffer.type] = ResourceBuffer{buffer.type, buffer.capacity};

    workers->capacity = std::max(0, definition.workerCapacity);
    workers->assigned = std::min(workers->assigned, workers->capacity.GetBase());
}

void ApplyProductionRecipes(Building& building, const BuildingDefinition& definition)
{
    auto* recipeComponent = building.GetComponent<RecipeComponent>();
    auto* production = building.GetComponent<ProductionComponent>();
    auto* logistics  = building.GetComponent<LogisticsComponent>();
    auto* workers    = building.GetComponent<WorkerComponent>();
    if (recipeComponent == nullptr || production == nullptr ||
        logistics == nullptr || workers == nullptr)
        return;

    std::vector<ProductionRecipeRuntime> recipes;
    if (!definition.recipes.empty())
    {
        for (const auto& recipe : definition.recipes)
            recipes.push_back(MakeRuntimeRecipe(recipe));
    }
    else if (!definition.production.outputs.empty() || !definition.production.inputs.empty())
    {
        ProductionRecipeDefinition recipe;
        recipe.name = "Default";
        recipe.production = definition.production;
        recipes.push_back(MakeRuntimeRecipe(recipe));
    }

    recipeComponent->SetRecipes(std::move(recipes), building, *production, *logistics, *workers);
}

// Applies parsed configuration to runtime state.
void ApplyStorageDefinition(Building& building, const BuildingDefinition& definition)
{
    std::map<ResourceType, ResourceBuffer>* buffers = nullptr;
    if (auto* storage = building.GetComponent<StorageComponent>())
        buffers = &storage->buffers;
    else if (auto* local = building.GetComponent<LocalResourceBufferComponent>())
        buffers = &local->buffers;
    if (buffers == nullptr)
        return;

    buffers->clear();
    for (const auto& buffer : definition.storageBuffers)
    {
        ResourceBuffer resourceBuffer{buffer.type, buffer.capacity};
        resourceBuffer.SetStoredAmount(buffer.initialAmount);
        (*buffers)[buffer.type] = std::move(resourceBuffer);
    }
}

// Applies parsed configuration to runtime state.
void ApplyHqDefinition(Building& building, const BuildingDefinition& definition)
{
    auto* hq = building.GetComponent<HqComponent>();
    if (hq == nullptr)
        return;

    hq->maxHp = definition.hq.maxHp;
    hq->currentHp = definition.hq.maxHp;
    hq->hardDefense = definition.hq.hardDefense;
    hq->thornsDamage = definition.hq.thornsDamage;
    hq->thornsInterval = definition.hq.thornsInterval;
    hq->thornsTimer = definition.hq.thornsInterval;
    hq->captureStockFraction = definition.hq.captureStockFraction;
    hq->conquestRampDuration = definition.hq.conquestRampDuration;
}

// Applies parsed configuration to runtime state.
void ApplyTowerDefinition(Building& building, const BuildingDefinition& definition)
{
    auto* tower = building.GetComponent<TowerCombatComponent>();
    auto* workers = building.GetComponent<WorkerComponent>();
    if (tower == nullptr || workers == nullptr)
        return;

    tower->damage = definition.tower.damage;
    tower->range = definition.tower.range;
    tower->attackSpeed = definition.tower.attackSpeed;
    tower->ammoResource = definition.tower.ammoResource;
    tower->ammoPerShot = definition.tower.ammoPerShot;
    workers->capacity = definition.tower.workerCapacity;
}

